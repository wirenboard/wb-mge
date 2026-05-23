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
# All tests with explicit IP
pytest api_tests/ --ip localhost:8080

# Default (localhost:8080)
pytest api_tests/
```

**Additional options:**

```bash
# Stop on first failure
pytest api_tests/ --ip localhost:8080 -x

# Run a specific file
pytest api_tests/01_test_auth.py --ip localhost:8080

# Run a specific test by name
pytest api_tests/ --ip localhost:8080 -k test_cache_multimaster

# Quiet output (no print)
pytest api_tests/ --ip localhost:8080 --no-header -q
```

---

## Running Against QEMU (e2e without a real device)

QEMU runs the firmware in an emulator and tests the HTTP API without physical hardware. All hardware-specific features are mocked:

- **WiFi**: scanning returns two fake networks (`QEMU-TestNetwork-1`, `QEMU-TestNetwork-2`)
- **RS-485 / Modbus RTU**: a mock task injects synthetic packets into the sniffer to populate the cache
- **Cache Modbus TCP server**: runs on port 50504 (QEMU forwards `localhost:50504 → ESP32:50504`)

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
> do **not** use `--qemu` — just point `--ip` at it:
> ```bash
> cd api_tests && .venv/bin/python -m pytest --ip localhost:8080
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
cd api_tests && .venv/bin/python -m pytest --ip localhost:8080
```

Wait for the QEMU console to print:

```text
I (XXXX) http_server: HTTP server started on port: 80
```

---

## QEMU-Specific Test Behaviour

| Test | Behaviour in QEMU |
| ---- | ----------------- |
| `test_wifi_scanner` | Completes immediately with 2 fake networks |
| `test_cache_multimaster` | Switches port 1 to `cache_bus`, waits for cache fill (~2 s), connects to `localhost:50504` |
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
