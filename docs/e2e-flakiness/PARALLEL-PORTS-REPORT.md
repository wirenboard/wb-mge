# Dynamic QEMU ports + parallelism as a flake stressor

Branch: `fix/dynamic-qemu-ports` (on top of `fix/vvzvlad-e2e-test-stabilization` @94cc98b,
which pins the test deps — pytest 9.1.1 / pluggy 1.6.0).

Two parts, strictly ordered. Part 1 makes the QEMU host ports dynamic so N full runs can
coexist on one host; Part 2 uses parallel full runs as a flake stressor.

---

## Part 1 — dynamic host ports

### The defect (ours, not infra)

Every QEMU host port was hardcoded: `conftest.QEMU_HOST_PORTS`, the `-nic` hostfwd string
and the two `-serial` UART chardev args, plus ~20 test modules' own copies of
`8080/8081/50502/50503/50504/5561/5562/5570`. Two runs on one host could not coexist — the
second run's tests connected to the **first** run's QEMU, which showed up in CI as
`Cannot connect to UART1 chardev TCP port 5561` and 19/22 identical failures across two
builds of the same branch.

### Design

New `api_tests/qemu_ports.py` is the single source of truth. Every host port is derived from
one integer **slot** (env `WB_MGE_PORT_SLOT`, default 0; falls back to the
`PYTEST_XDIST_WORKER` id so the scheme also serves `pytest -n`).

**Offset source — why env-var:** reproducible (unlike a pid), explicitly controllable (a
launcher sets one env var per instance), and free of the bind(0) race (bind-then-close leaves
a window in which another process can grab the just-released port before QEMU binds it).

**Layout — contiguous block, not per-port offset:** a slot maps to a contiguous 16-wide block
(`base 21000 + slot*16`). The legacy ports sit only 1 apart (8080/8081, 50502/50503/50504,
5561/5562), so *adding an offset to each* would alias one run's port onto another's; a block
cannot. Slots 0..~2700 keep every port < 65536; the handful actually run land in 21000..21175,
below the Linux ephemeral range (32768+), so the OS won't hand a client socket one of ours.

**Guest ports stay fixed.** Each slot is a separate QEMU with its own network stack, so guest
ports never collide across slots. The hostfwd rule forwards `host:<dynamic> -> guest:<fixed>`.
This split matters (see the first bug below): a socket **connect** uses the dynamic *host*
port, while a firmware **setting** that names a port (`cache_modbus_port`, `bridge.port`,
`web_port`) must use the fixed *guest* port, because that is what the forward targets.

### Artifact-path decision (explicit)

**Each parallel run requires its own working tree.** `build/qemu_flash.bin`,
`build/qemu_efuse.bin` and `build/qemu_test_report.xml` are per-tree make outputs, and QEMU
writes NVS back into `qemu_flash.bin` — so two runs in one tree would corrupt each other's
flash. Ports being disjoint is necessary but not sufficient; the tree must be separate too.
The QEMU log stays at the fixed `build/qemu_test.log` (per-tree; a slot suffix broke a test —
see the second bug).

### Two bugs the parallel validation caught (both mine, both from Part 1)

The whole point of the exercise — parallelism as a flake stressor — proved itself immediately
by catching two defects a single run never would:

1. **guest/host port conflation** (fixed in 693a8a8). The blanket rewrite made some firmware
   *settings* dynamic too. In the legacy scheme host == guest (50504 == 50504), so one
   constant served both; making everything dynamic told the cache firmware to listen on the
   dynamic host port while the forward still hit guest 50504 → connection refused. Diagnosed
   from the log: `Starting 3 parallel Modbus TCP clients on localhost:50504 → FAILED` — the
   test connected to the guest value, which is not on the host under a non-zero slot. Broke the
   cache subsystem (11/20/31/37/41/42/43) identically on every slot (identical-across-slots =
   deterministic bug, not contention). Fix: `qemu_ports` exposes guest-port constants; cache
   tests set `cache_modbus_port` to the guest port and connect to the dynamic host port.

2. **slot-suffixed QEMU log** (fixed in f21daa5). I had suffixed the log
   (`qemu_test.slotN.log`) as a same-tree safety. But `33_test_auth_settings.py` stats the
   hardcoded `build/qemu_test.log` to get a baseline offset for detecting a SW reboot; with the
   suffix that path did not exist on slots 1/2/3, so `au05`/`au06` asserted out with
   `Cannot stat the QEMU serial log`. Reverted to the fixed name (the suffix was pointless —
   separate trees already prevent clobber).

### Proof

Three full 4/6-way parallel runs, `--network host` (shared host netns — the faithful
reproduction of the CI collision; a default docker bridge would isolate the netns and not
test the fix), each instance its own tree + slot:

| run  | N | result |
|------|---|--------|
| par2 | 4 | slot0 STRICT-GREEN; slots1-3 failed only au05/au06 = the log-path bug (now fixed) |
| par3 | 4 | 3/4 STRICT-GREEN 228/1/0; slot1's only failure = known au05 session flake |
| par6 | 6 | **6/6 STRICT-GREEN 228/1/0** |

Across all 14 instance-runs, **every gateway/transparent/cache/sniffer/device test passed on
every concurrent slot** — zero cross-contamination (no `got=''`, no `Cannot connect`, no
foreign device data). The port split holds. The only failures ever seen were the two Part-1
bugs above (fixed) and the known au05 flake (below).

---

## Part 2 — parallelism as a flake stressor

### Degradation curve (wall-clock, full suite)

| concurrency | wall-clock |
|-------------|-----------|
| single (ref) | ~44 min |
| N=4 (par3)   | 49:21 – 54:38 |
| N=6 (par6)   | 54:02 – 59:54 |

Sub-linear; no cliff at 6×. QEMU is single-threaded (icount, deterministic per-instruction),
and the host has 50 cores, so CPU is not the bottleneck at these N (6 QEMUs + 6 pytest = 12
active threads). The slowdown is host-side contention on HTTP waits (`/settings` takes tens of
seconds under load). CPU saturation would begin near ~25 parallel instances.

### au05 (known session-eviction flake) under parallelism

`33_test_auth_settings.py::test_au05_full_buffer_preserved_after_sw_reboot`:
```
33_test_auth_settings.py:280: assert resp.status_code == 200
E  AssertionError: Session 0 (sid=...) was lost after SW reboot with full buffer
E  assert 401 == 200
```
This is the known zero-margin auth session-ring flake (a session is evicted / lost across the
SW-reboot window under load), **orthogonal to ports**. It **reproduces under parallelism** but
stochastically, not monotonically in N: 1/4 slots in par3 (N=4), 0/6 in par6 (N=6). Parallel
CPU starvation widens the reboot-timing window that the flake rides, the same way jitter hogs
did in the prior task — but the rate is timing-dependent, not simply "more load = more fails".

### Shared state beyond ports (audit + test)

Static audit of the test code: **no `/tmp`, `tempfile`, `mkstemp`, `/dev/shm`, `.lock`, or
fixed-path host writes.** Host-side echo/reconnect servers `bind((host, 0))` → OS-assigned
ephemeral ports (32768+, clear of the 21000-range slot blocks). The only host-side state is
`build/` artifacts and `.pytest_cache` — both per-tree. Conclusion: with dynamic ports +
separate trees, there is **zero residual host-side shared state**.

### Deinit deadlock hunt (Exp B) — NOT reproduced, but surfaced NVS temporal coupling

Hammered the two deinit tests (`36_test_tcp_server_deinit_hang`,
`37_test_cache_server_deinit_hang`) in 6 parallel instances × 5 rounds = 30 slot-runs, to try
to trigger the never-reproduced deinit deadlock under CPU starvation.

**The deadlock did NOT reproduce.** Across all 30 runs the deinit call always completed well
under its 3 s bound — zero hangs, zero pytest-timeouts. Parallel CPU starvation is **not** the
missing condition for the 36_/37_ deadlock.

What the hunt *did* surface — a real finding, and exactly the "shared state beyond ports" to
look for: **the persisted NVS flash couples sequential runs in the same tree.** Round 1 (fresh
flash) passed 2/2 on all 6 slots. Rounds 2-5 failed 36_ on all 6 slots, deterministically:
```
36_test_tcp_server_deinit_hang.py:110: assert resp.status_code == 200
E  AssertionError: set_port_mode(1, tcp_bridge) failed: 409
```
Mechanism: `37_`'s module-scoped `_baseline` fixture sets `cache_modbus_port = 50504` (guest)
and never resets it to the firmware default (504). The per-test `finally` captures "original
settings" *after* that baseline ran, so it restores back to 50504, not 504. QEMU writes NVS
back into `qemu_flash.bin`, so the next run on that same flash starts with
`cache_modbus_port = 50504`; the following run's `36_` then configures `rs485_1.bridge.port =
50504`, which the firmware rejects (409) because a bridge port equal to `cache_modbus_port` is
a collision (the very rule `11_test_cache_bridge_port_collision_rejected` asserts).

This is **benign under CI**, which regenerates a fresh flash per build (`make
qemu-create-flash-image`), and benign within a single full-suite run (later cache tests
re-enable 50504 anyway; par2/par3/par6 all passed 36_ and 37_). It only bites when a flash is
**reused across runs** (`--qemu-skip-build` without regenerating). It is nonetheless genuine
temporal shared-state beyond ports; if flash reuse is ever wanted, `37_`'s baseline should
restore `cache_modbus_port` to the default (or each run should start from a fresh flash).
The au05 sweep below resets each tree's flash to a golden clean image before every round to
avoid this confound.

### au05 flake-rate sweep (Exp C) — au05 does NOT reproduce in isolation

Ran `33_test_auth_settings.py -k "au05 or au06 or au01"` (the SW-reboot session-preservation
tests) in 6 parallel instances × 6 rounds = 36 slot-runs, resetting each tree's flash to a
golden clean image before every round (so the NVS coupling from Exp B cannot confound).

**Result: 36/36 passed — au05_fail=0, au06_fail=0, au01_fail=0.**

Cross-referenced with the full-suite runs:

| context | au05 outcome |
|---------|--------------|
| full suite, jitter hogs (prior task) | ~1/10 |
| full suite, N=4 parallel (par3) | 1/4 |
| full suite, N=6 parallel (par6) | 0/6 |
| isolated auth subset, 6× parallel × 6 rounds | **0/36** |

The refinement this gives: au05 flakes **only in the full-suite context**, never when the auth
tests are isolated and repeated — even at 6× parallel with a fresh flash. So the trigger is not
"reboot under CPU load" on its own; it is the reboot-timing window as it lands after ~30 min of
prior full-suite tests, whose accumulated session/timing state under parallel (or jitter)
contention occasionally loses `Session 0` across the RTC_NOINIT hand-off. Parallelism widens
that window stochastically (not monotonically in N), the same way jitter hogs did. A focused
repro therefore needs the full suite under load, not the auth tests alone — useful for whoever
takes the au05 firmware ticket.

---

## Summary of findings

1. **Part 1 goal met:** dynamic ports let N full runs coexist on one host with zero
   cross-contamination (14 instance-runs; par6 N=6 all STRICT-GREEN). Separate tree per instance
   is required alongside disjoint ports.
2. **Parallelism is a real flake stressor** — it caught two defects a single run never would
   (both mine, from Part 1: guest/host conflation; slot-suffixed log), each diagnosed to
   mechanism from the logs and fixed.
3. **Degradation is sub-linear, no cliff at 6×** (icount + 50 cores → host HTTP-wait
   contention, not CPU).
4. **Deinit deadlock does NOT reproduce** under 6× CPU starvation (30 runs) — parallelism is
   not its missing condition.
5. **Temporal shared state beyond ports exists: the persisted NVS flash** (37_'s baseline leaves
   `cache_modbus_port=50504`) — benign under CI's fresh-flash-per-build, real under flash reuse.
6. **au05 is a genuine low-rate flake tied to the full-suite reboot window**, reproducible under
   parallel/jitter load but not in isolation — a Part 2 data point for the separate au05 ticket.


---

## Commits

- `de3cd2f` — derive all host ports from `WB_MGE_PORT_SLOT` (qemu_ports.py + wiring).
- `693a8a8` — split guest vs host port for firmware settings (fix cache subsystem).
- `f21daa5` — keep QEMU log at fixed `build/qemu_test.log` (unbreak reboot detection).

## How to run parallel

Each instance needs its own tree and a distinct slot, sharing the host netns:
```
WB_MGE_PORT_SLOT=0 make qemu-test    # tree A, ports 21000-21007
WB_MGE_PORT_SLOT=1 make qemu-test    # tree B, ports 21016-21023
...
```
Harness used here: `e2e-parN.sh N tag [pytest-target]` (N docker `--network host` instances,
one tree per slot).
