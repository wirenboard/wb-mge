"""Single source of truth for the host-side QEMU ports.

Every host port the test suite touches — the QEMU hostfwd targets, the two UART
chardev TCP ports, and the UDP IO-bus port — is derived here from ONE integer
"slot", so that runs in DIFFERENT WORKING TREES can coexist on one host without
fighting over the same host ports. Guest-side ports never change (they live
inside each QEMU); only the host side moves.

DISJOINT PORTS ARE NECESSARY BUT NOT SUFFICIENT. A slot says nothing about the
files a run writes: build/qemu_flash.bin (QEMU writes NVS back into it),
build/qemu_efuse.bin, build/qemu_test.log and build/qemu_test_report.xml are all
per-TREE. Two runs in ONE tree therefore corrupt each other whatever their slots
are, and conftest._acquire_tree_lock() refuses the second one outright. "One run
per tree, one slot per concurrent run" is the whole contract.

Slot source, in priority order — the first one that carries a usable value wins,
and the winner is published as SLOT_SOURCE so a CI log can show which fired:
  1. WB_MGE_PORT_SLOT — the explicit request. A value that is present but not a
     valid slot RAISES; refusing to guess is the point of an explicit knob. An
     EMPTY value is treated as absent, because `WB_MGE_PORT_SLOT=$SLOT` with an
     unset SLOT is a normal shell accident and must not turn into "ImportError
     while loading conftest".
  2. EXECUTOR_NUMBER — Jenkins sets this per executor on a node, so two DIFFERENT
     jobs sharing one node land in different blocks. (Two builds of the SAME
     branch are already serialised by disableConcurrentBuilds() in the
     Jenkinsfile; what the slot buys is cross-JOB isolation.) Unusable values are
     warned about and skipped rather than raised: this variable is not our knob,
     and a machine that happens to export it must not become unable to run the
     suite at all.
  3. PYTEST_XDIST_WORKER (gw0 -> 0, gw1 -> 1, ...) — kept only so xdist workers
     cannot alias each other's ports. It does NOT make `pytest -n` a supported
     way to run this suite: xdist workers share one working tree by construction,
     so each would bring up its own session-scoped QEMU against the same
     qemu_flash.bin and qemu_test.log. conftest.pytest_configure() refuses
     `--qemu` together with xdist for exactly that reason (and pytest-xdist is
     not in api_tests/requirements.txt).
  4. Default 0.

Rationale for an env var over the alternatives:
  * Reproducible — the same slot always yields the same ports, unlike a pid.
  * Explicitly controllable — a parallel launcher sets one env var per instance.
  * No bind(0) race — allocating a free port with bind(0) then closing it leaves
    a window in which another process (or the sibling QEMU) can grab the just-
    released port before our QEMU binds it. A fixed slot has no such window.

Layout: a slot maps to a CONTIGUOUS block of ports, NOT an offset added to each
legacy base port. The legacy base ports sit only one apart (8080/8081,
50502/50503/50504, 5561/5562), so adding a small per-instance offset to each
would alias one instance's port onto another's. A contiguous block per slot
cannot alias.

    block_start = _BLOCK_BASE + slot * _BLOCK_SIZE

_BLOCK_BASE is 21000 and _BLOCK_SIZE is 16 (> the 8 ports assigned per block), so
the ports for the slots actually run (0..~10) land in 21000..21175 — comfortably
below the ephemeral range, so the OS will not hand a random client socket one of
our reserved numbers. _MAX_SLOT enforces that same property for EVERY accepted
slot: it is the last block that fits entirely below the ephemeral floor (734 with
the Linux default 32768), not merely the last one below 65536. The previous
ceiling of 2782 admitted slots whose ports land inside the ephemeral range, which
contradicts the rationale above. The one host where it enforces nothing is one
whose ip_local_port_range starts BELOW 21000 (some container images ship
`1024 65535`): there is no conforming slot there at all, so the ceiling falls back
to the 32768 convention and says so in the range error — see _ephemeral_floor().
"""
import os
import warnings

_BLOCK_BASE = 21000
_BLOCK_SIZE = 16

# Fallback when the running kernel does not tell us (macOS and any non-Linux have no
# /proc; macOS's own default range starts at 49152, i.e. HIGHER, so this is the
# conservative answer there too).
_DEFAULT_EPHEMERAL_FLOOR = 32768


def _ephemeral_floor():
    """(floor the slot ceiling is derived from, floor this host actually reports or None).

    Read from /proc/sys/net/ipv4/ip_local_port_range when available, so a host or
    container that LOWERED the range gets a correspondingly lower slot ceiling instead
    of a silent overlap between our reserved ports and the kernel's random client ports.

    A range that starts at or below _BLOCK_BASE (some container images ship
    `1024 65535`) is ignored rather than obeyed: obeying it would make _MAX_SLOT
    negative and the suite unrunnable on that host, while the whole 21000 block is
    inside the ephemeral range there no matter which slot is picked — so there is no
    better slot to fail over to, and failing the import would be pure damage.

    TWO values, not one, because that fallback makes them differ: on such a host the
    ceiling is computed from 32768 while the real floor is 1024, and publishing only the
    first made EPHEMERAL_FLOOR — and the range error below, which quotes it — state a
    number this kernel never agreed to. The observed value is kept so the message can say
    what is actually true there: the ceiling is a convention, not a guarantee.
    The second element is None when the host does not publish a range at all (macOS and
    everything else without /proc; macOS's own default starts at 49152, i.e. HIGHER, so the
    32768 assumption is conservative there).
    """
    try:
        with open("/proc/sys/net/ipv4/ip_local_port_range") as fh:
            low = int(fh.read().split()[0])
    except (OSError, ValueError, IndexError):
        return _DEFAULT_EPHEMERAL_FLOOR, None
    if low > _BLOCK_BASE + _BLOCK_SIZE:
        return low, low
    return _DEFAULT_EPHEMERAL_FLOOR, low


EPHEMERAL_FLOOR, EPHEMERAL_FLOOR_OBSERVED = _ephemeral_floor()
# The last block that fits ENTIRELY below the ephemeral floor. `- 1` because the block
# is _BLOCK_SIZE wide: slot _MAX_SLOT + 1 would start at (floor - _BLOCK_SIZE) and its
# block would run past the floor.
_MAX_SLOT = (EPHEMERAL_FLOOR - _BLOCK_BASE) // _BLOCK_SIZE - 1


def _ceiling_reason() -> str:
    """Why _MAX_SLOT is where it is — worded so it is true on THIS host."""
    if EPHEMERAL_FLOOR_OBSERVED is None:
        return (f"the ceiling keeps every port of the block below {EPHEMERAL_FLOOR}, the "
                f"assumed ephemeral floor — this host publishes no "
                f"/proc/sys/net/ipv4/ip_local_port_range")
    if EPHEMERAL_FLOOR_OBSERVED == EPHEMERAL_FLOOR:
        return (f"the ceiling keeps every port of the block below this host's ephemeral "
                f"floor {EPHEMERAL_FLOOR} (ip_local_port_range)")
    return (f"the ceiling keeps every port of the block below {EPHEMERAL_FLOOR}, the assumed "
            f"floor; this host's ip_local_port_range actually starts at "
            f"{EPHEMERAL_FLOOR_OBSERVED}, i.e. below the whole {_BLOCK_BASE} block, so NO "
            f"slot avoids the ephemeral range here and the ceiling is only a convention")


def _parse_slot(raw: str) -> int:
    """Parse and range-check one slot value. Raises ValueError with the reason."""
    slot = int(raw)  # ValueError for non-integers, propagated to the caller
    if slot < 0 or slot > _MAX_SLOT:
        raise ValueError(
            f"out of range: {slot} (allowed 0..{_MAX_SLOT}; {_ceiling_reason()})"
        )
    return slot


def _slot_from_env():
    """Resolve this run's slot -> (slot, source). See the module docstring for the order."""
    raw = os.environ.get("WB_MGE_PORT_SLOT", "").strip()
    if raw:
        try:
            return _parse_slot(raw), "WB_MGE_PORT_SLOT"
        except ValueError as exc:
            raise ValueError(f"WB_MGE_PORT_SLOT={raw!r} is not a usable slot: {exc}")

    raw = os.environ.get("EXECUTOR_NUMBER", "").strip()
    if raw:
        try:
            return _parse_slot(raw), "EXECUTOR_NUMBER"
        except ValueError as exc:
            # Not our variable: warn and fall through instead of taking the run down.
            warnings.warn(
                f"EXECUTOR_NUMBER={raw!r} is not a usable port slot ({exc}); ignoring it "
                f"and falling back. Set WB_MGE_PORT_SLOT explicitly to choose a block.",
                stacklevel=2,
            )

    worker = os.environ.get("PYTEST_XDIST_WORKER", "")
    if worker.startswith("gw") and worker[2:].isdigit():
        try:
            return _parse_slot(worker[2:]), "PYTEST_XDIST_WORKER"
        except ValueError:
            pass  # absurd worker index; the default block is still better than dying

    return 0, "default"


SLOT, SLOT_SOURCE = _slot_from_env()
_BLOCK_START = _BLOCK_BASE + SLOT * _BLOCK_SIZE

HOST = "127.0.0.1"
GATEWAY_HOST = HOST  # alias used across the test modules

# Host ports, one per index within this slot's block. The guest port each one
# forwards to is noted in the comment (guest ports are fixed inside QEMU).
HTTP_HOST_PORT = _BLOCK_START + 0                 # guest 80    (web API)
ALT_WEB_HOST_PORT = _BLOCK_START + 1              # guest 8081  (alt web-port test)
GATEWAY_HOST_PORT = _BLOCK_START + 2              # guest 502   (Modbus gateway)
TRANSPARENT_PORT2_HOST_PORT = _BLOCK_START + 3    # guest 503   (transparent bridge, port 2)
# Guest 50504 is shared by the cache Modbus server AND transparent bridge port 1,
# so all these names alias one host port.
CACHE_MODBUS_HOST_PORT = _BLOCK_START + 4         # guest 50504
TRANSPARENT_PORT1_HOST_PORT = CACHE_MODBUS_HOST_PORT
TRANSPARENT_HOST_PORT = CACHE_MODBUS_HOST_PORT    # 25_'s name for the same port
QEMU_CACHE_MODBUS_PORT = CACHE_MODBUS_HOST_PORT   # 20_/31_/37_/42_/43_'s name
MODBUS_TCP_HOST_PORT = GATEWAY_HOST_PORT          # 26_'s name for the gateway port
IO_BUS_UDP_PORT = _BLOCK_START + 5                # guest 5570  (UDP IO state bus)
UART1_TCP_PORT = _BLOCK_START + 6                 # QEMU -serial chardev, UART1 (RS485-1)
UART2_TCP_PORT = _BLOCK_START + 7                 # QEMU -serial chardev, UART2 (RS485-2)

# Web-port test (40_) names: the "default" web port is the HTTP port; the "alt"
# is the alternate. Guest side stays 80 / 8081; only host moves.
DEFAULT_PORT_HOST = HTTP_HOST_PORT
ALT_PORT_HOST = ALT_WEB_HOST_PORT
ALT_PORT_GUEST = 8081  # guest port unchanged

# Fixed guest ports — the '-:<N>' side of each hostfwd, i.e. the port the FIRMWARE
# listens on INSIDE the guest. These do NOT move per slot: each slot is a separate QEMU
# with its own network stack, so guest ports never collide across slots. A test that
# WRITES a firmware port setting (cache_modbus_port, rs485_N.bridge.port, web_port) must
# use the GUEST value, because that is what the hostfwd forwards to; the host/connect side
# uses the dynamic *_HOST_PORT above. (The two coincided in the legacy fixed-port scheme —
# host 50504 == guest 50504 — which is why one constant used to serve both.)
_GUEST_HTTP = 80
_GUEST_ALT_WEB = 8081
_GUEST_GATEWAY = 502
_GUEST_TRANSPARENT_P2 = 503
_GUEST_CACHE_MODBUS = 50504
_GUEST_IO_BUS = 5570

# Public guest-port names for firmware settings writes.
CACHE_MODBUS_GUEST_PORT = _GUEST_CACHE_MODBUS       # firmware cache_modbus_port setting
TRANSPARENT_P1_GUEST_PORT = _GUEST_CACHE_MODBUS     # firmware bridge.port for transparent port 1
GATEWAY_GUEST_PORT = _GUEST_GATEWAY                 # firmware bridge.port for the Modbus gateway
# The firmware web_port setting for the alt-port test is published as ALT_PORT_GUEST above,
# next to the 40_ host-port names that go with it.

# The host ports THIS run reserves, split by TRANSPORT because the stale-port preflight
# has to probe them differently: a TCP port answers connect(), a UDP port does not (it is
# connectionless, so connect_ex() succeeds against a closed port and proves nothing — the
# UDP entry in the old single list was checking exactly nothing). conftest probes the UDP
# one by trying to bind() it instead. Scoped to this run's block so a sibling run in
# another tree/slot never trips it.
MY_TCP_HOST_PORTS = [
    HTTP_HOST_PORT,
    ALT_WEB_HOST_PORT,
    GATEWAY_HOST_PORT,
    TRANSPARENT_PORT2_HOST_PORT,
    CACHE_MODBUS_HOST_PORT,
    UART1_TCP_PORT,
    UART2_TCP_PORT,
]
MY_UDP_HOST_PORTS = [IO_BUS_UDP_PORT]
# There is deliberately NO combined MY_HOST_PORTS list. It existed "for reporting", nothing
# reported from it (port_summary() below is what the header and `make qemu-ports` print),
# and a list that erases the TCP/UDP distinction is exactly the shape that let the old
# preflight probe a UDP port with connect() and check nothing.


def qemu_nic_arg() -> str:
    """The '-nic user,...' value with hostfwd rules pointing at this slot's host ports."""
    return (
        "user,model=open_eth,"
        f"hostfwd=tcp:{HOST}:{HTTP_HOST_PORT}-:{_GUEST_HTTP},"
        f"hostfwd=tcp:{HOST}:{ALT_WEB_HOST_PORT}-:{_GUEST_ALT_WEB},"
        f"hostfwd=tcp:{HOST}:{GATEWAY_HOST_PORT}-:{_GUEST_GATEWAY},"
        f"hostfwd=tcp:{HOST}:{TRANSPARENT_PORT2_HOST_PORT}-:{_GUEST_TRANSPARENT_P2},"
        f"hostfwd=tcp:{HOST}:{CACHE_MODBUS_HOST_PORT}-:{_GUEST_CACHE_MODBUS},"
        f"hostfwd=udp:{HOST}:{IO_BUS_UDP_PORT}-:{_GUEST_IO_BUS}"
    )


def qemu_serial_args() -> list:
    """The two -serial UART chardev args (placed after '-serial mon:stdio')."""
    return [
        "-serial", f"tcp::{UART1_TCP_PORT},server,nowait",
        "-serial", f"tcp::{UART2_TCP_PORT},server,nowait",
    ]


# ---------------------------------------------------------------------------
# Shell/Makefile entry points
#
# qemu.mk's `make qemu-web` used to hardcode the legacy hostfwd rules and
# -serial ports, so an already-running QEMU started that way listened on 8080/50502-4/
# 5561-2 while the test suite connected to this module's block — the documented
# "test against an already-running QEMU" flow was broken end to end. The Makefile now
# expands the two functions below instead, which makes this module the one source of
# truth for BOTH launchers. They print a single line each, because that is what
# $(shell ...) can consume.
# ---------------------------------------------------------------------------

def qemu_serial_args_str() -> str:
    """qemu_serial_args() as one command-line string, for $(shell ...) in a Makefile."""
    return " ".join(qemu_serial_args())


def port_summary() -> str:
    """One-line, human-readable summary of this slot's host ports (for make/CI logs)."""
    return (
        f"slot {SLOT} (from {SLOT_SOURCE}): "
        f"web={HTTP_HOST_PORT}->80 altweb={ALT_WEB_HOST_PORT}->{_GUEST_ALT_WEB} "
        f"gateway={GATEWAY_HOST_PORT}->{_GUEST_GATEWAY} "
        f"bridge2={TRANSPARENT_PORT2_HOST_PORT}->{_GUEST_TRANSPARENT_P2} "
        f"cache/bridge1={CACHE_MODBUS_HOST_PORT}->{_GUEST_CACHE_MODBUS} "
        f"io-bus/udp={IO_BUS_UDP_PORT}->{_GUEST_IO_BUS} "
        f"uart1={UART1_TCP_PORT} uart2={UART2_TCP_PORT}"
    )


if __name__ == "__main__":  # `python3 -m qemu_ports` / `python3 qemu_ports.py`
    print(port_summary())
