# WB-MGE API Tests

## Overview

Automated integration tests for the WB-MGE HTTP API. Uses **pytest**; execution order is determined by numeric file name prefixes (`01_`, `02_`, …). Tests cover:

- **Auth and sessions** — login, logout, password change, endpoint protection
- **Device info** — structure, data types (heap, PSRAM, cache), field formats
- **Settings** — read, write, validation, partial update
- **Modbus TCP** — bridge parameters, limit validation
- **WiFi** — scanning, edge cases, network fields, AP clients
- **Cache** — /cache/status, /cache/csv, /cache/json, server toggle, multimaster
- **Ports** — port modes, sniffer, WB test endpoint
- **Misc** — uptime, hostname, static files, HTTP method guard, commands
- **Reboot** — device reboot with uptime verification

---

## File Structure

```text
api_tests/
├── conftest.py              # pytest fixtures (api client, --ip, --qemu, connection check)
├── api_client.py            # WBMGEAPI class — HTTP client for all endpoints
├── modbus_helpers.py        # Modbus TCP utilities (encode/decode, worker threads, staleness)
├── pytest.ini               # pytest configuration
├── requirements.txt         # Dependencies
│
├── 01_test_auth.py          # Auth, sessions, password change
├── 02_test_info.py          # Device info
├── 03_test_settings.py      # Settings, validation, partial update
├── 04_test_uptime.py        # Uptime
├── 05_test_modbus.py        # Modbus TCP parameters
├── 06_test_wifi.py          # WiFi scanner, edge cases, AP clients
├── 07_test_static_files.py  # Static files
├── 08_test_http.py          # HTTP method guard
├── 09_test_commands.py      # Commands (set_default_settings)
├── 10_test_hostname.py      # Hostname endpoint
├── 11_test_cache.py         # Cache endpoints, multimaster
├── 12_test_sniffer_ws.py    # WebSocket sniffer
├── 13_test_ports.py         # Ports, sniffer, WB test
├── 14_test_reboot.py        # Reboot, uptime verification
├── 15_test_ws_pong_race.py  # WebSocket pong race condition (long-running)
└── 16_test_uart_teardown_crash.py  # UART teardown crash (long-running, always last)
```

---

## Running Against a Real Device

### 1. Install dependencies

```bash
pip install -r api_tests/requirements.txt
```

### 2. Run tests

```bash
# Default: --ip already points at this run's own web port, which is derived from
# WB_MGE_PORT_SLOT (see api_tests/qemu_ports.py). `make qemu-ports` prints the block.
pytest api_tests/

# All tests with explicit IP (21000 is the slot-0 web port)
pytest api_tests/ --ip localhost:21000
```

> **Ports follow a slot.** Every host port the suite uses — web, Modbus gateway, transparent
> bridge, cache Modbus server, both UART chardevs, the UDP IO bus — comes from one integer,
> `WB_MGE_PORT_SLOT` (default 0 → the `21000` block; in Jenkins it defaults to the
> executor number). `make qemu-ports` prints the resolved block, and pytest prints it in its
> report header. The slot separates PORTS only: `make qemu-test` (and `make qemu-web` /
> `make qemu-run`) takes an exclusive lock on its WORKING TREE (`.e2e-tree.lock` in the
> repo root) for the whole run, **build included**, and a `--qemu` pytest started
> directly takes the same lock in `pytest_configure`. The reason is that
> `build/qemu_flash.bin`, `build/qemu_efuse.bin`, `build/qemu_test.log` and
> `build/qemu_test_report.xml` are per-tree. Two suites at once = two checkouts, two slots.

**Additional options:**

```bash
# Stop on first failure
pytest api_tests/ -x

# Run a specific file
pytest api_tests/01_test_auth.py

# Run a specific test by name
pytest api_tests/ -k test_cache_multimaster

# Quiet output (no print)
pytest api_tests/ --no-header -q
```

`--ip <host>:<port>` overrides the target; omit it to use this slot's own web port.

---

## Running Against QEMU (e2e without a real device)

QEMU runs the firmware in an emulator and tests the HTTP API without physical hardware. All hardware-specific features are mocked:

- **WiFi**: scanning returns two fake networks (`QEMU-TestNetwork-1`, `QEMU-TestNetwork-2`)
- **RS-485 / Modbus RTU**: a mock task injects synthetic packets into the sniffer to populate the cache
- **Cache Modbus TCP server**: listens on guest port 50504; QEMU forwards this slot's
  `cache/bridge1` host port to it (`localhost:21004 → ESP32:50504` for slot 0)

> **CI:** Jenkins **does** run this suite by default — the `RUN_E2E` build parameter defaults to
> on, enabling the `E2E tests (QEMU)` stage. Untick it to skip that stage.
> `Coverage (QEMU e2e)` is gated on `RUN_COVERAGE` (default **off**) *alone*, independently of
> `RUN_E2E`: it re-runs the same suite on an instrumented build with the reboot tests deselected.
> So `RUN_COVERAGE=true` + `RUN_E2E=false` is a coverage-only build that runs the suite once
> (~2 h), while ticking both runs it twice (~4 h).
> On a branch Jenkins has never built, the checkboxes are not there yet — a `parameters` block only
> takes effect from the second build onwards, so build once, then rebuild with the parameters set.

### 1. Build firmware

```bash
make qemu-build
```

### 2. Run tests via make (recommended)

```bash
make qemu-test
```

This boots QEMU, runs the full pytest suite, and stops QEMU automatically.

> **Note:** The `--qemu` flag means "launch and manage QEMU yourself".
> To run tests against an **already-running** QEMU instance (e.g. started with `make qemu-web`),
> do **not** use `--qemu`. Use the same `WB_MGE_PORT_SLOT` in both shells and the default
> `--ip` already matches:
> ```bash
> cd api_tests && .venv/bin/python -m pytest            # --ip defaults to this slot's web port
> cd api_tests && .venv/bin/python -m pytest --ip localhost:21000   # or spell out slot 0
> ```

Filter by test name:

```bash
make qemu-test PYTEST_ARGS="-k test_cache_multimaster"
```

### 3. Run QEMU manually with web UI

```bash
make qemu-web
```

Then in a separate terminal:

```bash
cd api_tests && .venv/bin/python -m pytest
```

`make qemu-web` derives its port forwarding from the same `api_tests/qemu_ports.py`, so as
long as both shells share `WB_MGE_PORT_SLOT` the default `--ip` reaches it.

Wait for the QEMU console to print:

```text
I (XXXX) http_server: HTTP server started on port: 80
```

---

## QEMU-Specific Test Behaviour

| Test | Behaviour in QEMU |
| ---- | ----------------- |
| `test_wifi_scanner` | Completes immediately with 2 fake networks |
| `test_cache_multimaster` | Switches port 1 to `cache_bus`, waits for cache fill (~2 s), connects to this slot's cache host port (guest 50504) |
| `test_reboot` | Reboots the QEMU emulator, waits for it to come back |

---

## Adding a New Test

1. Add a function to a suitable file, or create a new file with an appropriate numeric prefix (e.g. `11a_test_cache_extra.py`)
2. Use the `api` fixture — it provides an authenticated `WBMGEAPI` client
3. Execution order is determined by the file's numeric prefix; within a file, by function definition order

```python
def test_new_feature(api):
    response = api.session.get(f"{api.base_url}/new_endpoint", timeout=10)
    assert response.status_code == 200
    data = response.json()
    assert "expected_field" in data
    print("✓ New feature works")
```
