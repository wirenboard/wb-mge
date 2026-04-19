# KNX IP Secure on ESP32 (WB-MGU)

Port of knxd's KNX IP Secure implementation to ESP32 for the WB-MGU v.3 gateway with WBE2-I-KNX extension module.

## Overview

This port provides a KNX TP to KNX/IP Secure tunnel, allowing ETS6 and other KNX IP clients to communicate with KNX TP bus devices through the ESP32. The implementation reuses the actual knxd codebase with minimal ESP32-specific adaptations (ifdefs, abstraction layers).

**Architecture:**
```
ETS6 / XKNX / KNX client
    │ TCP port 3671
    │ KNX IP Secure (X25519 ECDH + AES-128-CCM)
    ▼
ESP32 (wb-mge firmware)
    │ knxd Router + TcpTunServer + IPSecure
    │ NCN5120 driver
    │ UART1 (GPIO4 RX, GPIO10 TX, 38400 baud 8N1)
    ▼
KNX TP bus
    │
    ▼
KNX devices (actuators, sensors, etc.)
```

## Hardware

- **Device:** WB-MGU v.3 (ESP32-U4WDH, 4MB flash)
- **KNX module:** WBE2-I-KNX (NCN5121 transceiver)
- **UART:** UART1, GPIO4 (RX), GPIO10 (TX), 38400 baud, 8N1
- **Serial number:** derived from ESP32 Ethernet MAC (`esp_read_mac`)

## Building

### Prerequisites

- ESP-IDF v5.4 (stored in `_tools/esp-idf/`)
- After VM restart: `bash _tools/setup.sh` to restore environment

### Build command

```bash
cd wb-mge
export IDF_PATH=$(pwd)/_tools/esp-idf
source $IDF_PATH/export.sh

idf.py -DDEVICE_SIGNATURE=mge_v3 \
       -DMODEL_DEFINE=MODEL_mge_v3 \
       -DFIRMWARE_VERSION=1.2.0-knx \
       -DTARGET_PROJECT_NAME=wb_mge \
       -DFIRMWARE_GIT_INFO=knx-ipsecure \
       -DINTERNAL_BUILD=1 \
       build
```

Binary output: `build/wb_mge.bin` (~1.5MB, 11% free in app partition).

## Flashing

The ESP32 is connected via USB-UART (CH343) to a Wirenboard at 192.168.1.111 (`/dev/ttyACM0`).

### Via the Wirenboard

```bash
# Copy firmware to WB
sshpass -p wirenboard scp -o StrictHostKeyChecking=no \
    build/wb_mge.bin root@192.168.1.111:/tmp/esp_fw/

# Flash (app partition only, fast)
sshpass -p wirenboard ssh root@192.168.1.111 '
PYTHONPATH=/tmp/esptool_bundle python3 /tmp/esptool_bundle/esptool_run.py \
    --chip esp32 -p /dev/ttyACM0 -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x90000 /tmp/esp_fw/wb_mge.bin'
```

### Full flash (first time)

```bash
# Copy all binaries
sshpass -p wirenboard scp -o StrictHostKeyChecking=no \
    build/bootloader/bootloader.bin \
    build/partition_table/partition-table.bin \
    build/ota_data_initial.bin \
    build/wb_mge.bin \
    root@192.168.1.111:/tmp/esp_fw/

# Flash everything
sshpass -p wirenboard ssh root@192.168.1.111 '
PYTHONPATH=/tmp/esptool_bundle python3 /tmp/esptool_bundle/esptool_run.py \
    --chip esp32 -p /dev/ttyACM0 -b 460800 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m \
    0x1000 /tmp/esp_fw/bootloader.bin \
    0x8000 /tmp/esp_fw/partition-table.bin \
    0xd000 /tmp/esp_fw/ota_data_initial.bin \
    0x90000 /tmp/esp_fw/wb_mge.bin'
```

### Serial monitoring

```bash
# Start serial monitor with timestamps (persistent log)
sshpass -p wirenboard ssh root@192.168.1.111 '
stty -F /dev/ttyACM0 115200 raw -echo
cat /dev/ttyACM0 | while IFS= read -r line; do
    echo "$(date +%H:%M:%S.%3N) $line"
done > /tmp/esp32_console.log &'

# View log
sshpass -p wirenboard ssh root@192.168.1.111 'tail -f /tmp/esp32_console.log'

# Filter for KNX events
sshpass -p wirenboard ssh root@192.168.1.111 \
    'grep -v "fd=6" /tmp/esp32_console.log | grep "IPSEC\|TCPTUN\|SERVER\|session"'
```

## Configuration

### Default settings (in `main/config.h`)

| Setting | Default | Description |
|---------|---------|-------------|
| `knx_enabled` | `true` | Enable KNX IP Secure server |
| `knx_port` | `3671` | TCP listening port |
| `knx_dev_auth` | `trustme` | Device authentication password |
| `knx_user_pass` | `secret` | User/commissioning password (what ETS prompts for) |

Settings are configurable via the web UI at `http://<device-ip>/` under the KNX section.

### knxd INI configuration (built in `knx_server.cpp`)

```ini
[main]
addr = 1.1.1
client-addrs = 1.1.200:4    # 4 tunnel client addresses
connections = A.ncn5120,B.tcptun
name = ESP32-KNX

[A.ncn5120]
device = /dev/uart/1
driver = ncn5120
baudrate = 38400

[B.tcptun]
server = tcptunsrv
tunnel = B.tunnel
port = 3671
device-fdsk = <hex>          # pre-derived device auth key
user-password-key = <hex>    # pre-derived user password key
serial-number = <mac-hex>
```

### PBKDF2 key caching

PBKDF2-SHA256 derivation takes ~10s on ESP32 (65536 iterations). Keys are cached in NVS after first derivation. Subsequent boots load keys instantly from cache.

Cache keys: `kda_<hash>` (device auth), `kup_<hash>` (user password).

## Testing

### XKNX (Python)

Tests run from the Wirenboard (192.168.1.111) which can reach the ESP32 WiFi (192.168.1.112).

```bash
# Install
pip3 install xknx

# Basic connection test
python3 -c "
import asyncio
from xknx import XKNX
from xknx.io import ConnectionConfig, ConnectionType, SecureConfig

async def test():
    xknx = XKNX(connection_config=ConnectionConfig(
        connection_type=ConnectionType.TUNNELING_TCP_SECURE,
        gateway_ip='192.168.1.112', gateway_port=3671,
        secure_config=SecureConfig(user_id=2, user_password='secret')))
    await xknx.start()
    print('Connected!')
    await asyncio.sleep(1)
    await xknx.stop()
    print('Disconnected')
asyncio.run(test())
"
```

### XKNX test suite (`_tools/test_xknx.py`)

```bash
python3 _tools/test_xknx.py --host 192.168.1.112 --cycles 10
```

Tests: connect/disconnect cycles, group write, sustained connection, concurrent connections.

### Verified test results

| Test | Result |
|------|--------|
| Connect/disconnect x20 (rapid) | PASS |
| Authentication user_id=1 | PASS |
| Authentication user_id=2 | PASS |
| GroupValueWrite to KNX bus | PASS |
| GroupValueRead from KNX bus | PASS |
| Bidirectional traffic (WB knxd → ESP32 → XKNX) | PASS |
| 3 concurrent connections | PASS |
| ETS6 Device Info read (1.1.2) | PASS |
| ETS6 Individual Address Check | PASS |
| 30+ connection cycles without crash | PASS |

### Bus traffic verification

Telegrams confirmed on KNX TP bus via NCN5120:
```
Send L_Data from 1.1.200 to 0/0/1 A_GroupValue_Write (small) 01
Send L_Data from 1.1.200 to 0/0/1 A_GroupValue_Read
Recv L_Data from 1.2.7 to 0/0/2 A_GroupValue_Write 01
```

## ETS6 Testing

### Connection via ETS6

ETS6 connects to the ESP32 via TCP IP Secure. On first connection, ETS tries 3 simultaneous TCP connections. The first 1-2 may fail authentication (ETS tries without password first), then prompts the user for the "commissioning password". Enter: `secret`.

After entering the password, ETS caches it for subsequent connections.

### Remote GUI control of ETS6

The ETS6 VM (Windows 11) is accessible via SSH on port 2222:
```bash
sshpass -p test123 ssh -p 2222 user@localhost
```

GUI automation uses PowerShell scripts executed via Windows Task Scheduler (to run in the interactive session):

#### Screenshot
```bash
# Take screenshot
sshpass -p test123 ssh -p 2222 user@localhost \
    'cmd /c "schtasks /run /tn CaptureScreen 2>&1"'
sleep 3
sshpass -p test123 scp -P 2222 'user@localhost:screen.png' /tmp/screen.png
```

#### Click & type via automate.ps1

The automation script (`C:\Users\user\automate.ps1`) supports actions: `screenshot`, `click`, `keys`, `focusclick`, `focuskeys`, `windows`, `focus`, `launch`, `kill`.

It reads action from a control file (`C:\Users\user\auto_cmd.txt`):
```bash
# Write action
sshpass -p test123 ssh -p 2222 user@localhost \
    'cmd /c "(echo click & echo 60,1083) > C:\Users\user\auto_cmd.txt"'
# Execute
sshpass -p test123 ssh -p 2222 user@localhost \
    'cmd /c "schtasks /run /tn Auto 2>&1"'
sleep 4
# Get result
sshpass -p test123 scp -P 2222 'user@localhost:screen.png' /tmp/screen.png
```

#### Key ETS6 left panel coordinates (1959x1644 screen)

These are pixel coordinates for the Monitors panel items when the Monitors tab is active:

| Item | Y coordinate |
|------|-------------|
| Group Monitor | ~1047 |
| Bus Monitor | ~1077 |
| ETS Bus Activity | ~1107 |
| Diagnostics (header) | ~1047 (after collapse) |
| **Device Info** | **~1083** |
| Individual Addresses | ~1113 |
| Programming Mode | ~1143 |
| Individual Address Check | ~1173 |
| Line Scan | ~1203 |

**Note:** Y coordinates shift when panels are collapsed/expanded. The values above are for the default layout with the Monitors bottom panel visible.

## Bugs Found and Fixed

### 1. `reset_timer()` — immediate connection timeout (ROOT CAUSE of all connection failures)

**File:** `tcptunserver.cpp`, `eibnetserver.cpp`

**Problem:** `TcpTunConn::reset_timer()` called `timeout.set(keepalive, 0)` which overwrites the timer's absolute `at` value with a relative one (e.g., 120 seconds). Our select()-based ev_loop stores absolute timestamps in `w->at`. The timer check `w->at <= loop->now_` evaluates to `120 <= 1713470000` → true, so the timeout fires **immediately** after every packet, destroying the connection.

**Fix:**
```cpp
void TcpTunConn::reset_timer() {
    timeout.stop();
    timeout.start(parent->keepalive, 0);
}
```

**Impact:** This bug caused 100% connection failure — every connection was killed immediately after the first packet exchange.

### 2. `~TcpTunConn` destructor crash (shared_ptr chain)

**File:** `tcptunserver.cpp`

**Problem:** When `TcpTunConn` is destroyed during async cleanup, the implicit destruction of `TracePtr t` triggers a `shared_ptr<IniSection>` destructor chain that accesses freed memory (LoadProhibited at EXCVADDR=0x8).

**Fix:** Explicitly reset shared_ptrs before implicit member destruction:
```cpp
TcpTunConn::~TcpTunConn() {
    channels.clear();
    t.reset();  // Release Trace before implicit destruction
    if (fd >= 0) { close(fd); fd = -1; }
}
```

### 3. VFS UART CR→LF translation corrupting KNX frames

**File:** `knx_server.cpp`

**Problem:** ESP-IDF's VFS UART layer converts 0x0D (CR) to 0x0A (LF) by default on the RX path. Any KNX frame containing byte 0x0D (e.g., PropertyValue_Read with PID=13) gets corrupted. The checksum fails, the frame is dropped, ETS retries with 5s timeout. This caused 36 frame drops and inflated Device Info read time from ~11s to ~55s.

**Fix:** Disable line ending translation after UART VFS init:
```cpp
uart_vfs_dev_port_set_rx_line_endings(cfg->uart_num, ESP_LINE_ENDINGS_LF);
uart_vfs_dev_port_set_tx_line_endings(cfg->uart_num, ESP_LINE_ENDINGS_LF);
```

**Impact:** ETS Device Info read: 55s → 11s. Frame drops: 36 → 0.

### 4. Member initialization order (`fd` before `SendBuf`/`RecvBuf`)

**File:** `tcptunserver.h`

**Problem:** `int fd` was declared after `SendBuf sendbuf` and `RecvBuf recvbuf`. C++ initializes members in declaration order, so `sendbuf(fd)` used an uninitialized `fd`.

**Fix:** Moved `int fd` before `SendBuf`/`RecvBuf` in the class declaration.

### 4. FreeRTOS `queue.h` collision

**File:** `knxd_queue.h` (renamed from `queue.h`)

**Problem:** knxd's `queue.h` used `#ifndef QUEUE_H` guard, same as FreeRTOS. Include order caused wrong header to be used.

**Fix:** Renamed to `knxd_queue.h` with `KNXD_QUEUE_H` guard.

### 5. `fmt` shim for `std::string` → `printf`

**File:** `components/knxd_core/fmt/format.h`

**Problem:** knxd's `TRACEPRINTF` passes `std::string` to printf `%s`, causing garbled output (object bytes printed instead of string).

**Fix:** Added `to_printf_arg()` helper that calls `.c_str()`:
```cpp
namespace fmt::detail {
    inline const char* to_printf_arg(const std::string& s) { return s.c_str(); }
    template<typename T> inline const T& to_printf_arg(const T& v) { return v; }
}
```

### 6. Missing factory registrations

**Problem:** knxd's `AutoRegister` statics get stripped from `.a` archives by the linker. Drivers/servers/filters not found at runtime.

**Fix:** Manual registration in `knx_server.cpp`:
```cpp
static Maker<NCN5120, Driver> ncn5120_maker;
Factory<Driver>::Instance().reg(ncn5120_maker, "ncn5120");
static Maker<TcpTunServer, Server> tcptunsrv_maker;
Factory<Server>::Instance().reg(tcptunsrv_maker, "tcptunsrv");
static Maker<RetryFilter, Filter> retry_maker;
Factory<Filter>::Instance().reg(retry_maker, "retry");
static Maker<DummyL2Driver, Driver> dummy_maker;
Factory<Driver>::Instance().reg(dummy_maker, "dummy");
```

### 7. Dangling pointer in `knx_server_config_t`

**Problem:** Config struct had `const char*` members pointing to stack locals in `main.c`. The knxd task reads them asynchronously after `main()` returns.

**Fix:** Changed to fixed-size `char[64]` arrays.

### 8. PBKDF2 watchdog timeout

**Problem:** PBKDF2 blocks for ~10s, WDT was 10s. Crash on first boot.

**Fix:** Increased WDT to 30s (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=30`), removed knxd task from WDT (`esp_task_wdt_delete()`).

### 9. `select()` timeout cap

**File:** `ev_loop.c`

**Problem:** Without cap, `select()` blocks up to 10s (NCN5120 keepalive timer). New TCP connections aren't accepted promptly.

**Fix:** Capped select timeout to 100ms.

## Architecture Details

### Components

| Component | Description |
|-----------|-------------|
| `main/knx_server.cpp` (271 lines) | KNX server wrapper — builds IniData config, registers factories, caches PBKDF2 keys, inits UART, runs ev_run() |
| `main/knx_server.h` (35 lines) | C API: `knx_server_start()`, `knx_server_config_t` |
| `components/knxd_core/` | knxd source files (router, ncn5120, tcptunserver, ipsecure, etc.) |
| `components/libev_shim/` | Drop-in libev replacement using `select()` |
| `main/frontend/` | Vue.js web UI with KNX settings page |

### libev shim (`components/libev_shim/`)

Drop-in replacement for libev's C and C++ APIs:

- `ev.h` — C API (ev_io, ev_timer, ev_async)
- `ev++.h` — C++ wrapper (ev::io, ev::timer, ev::async, template callbacks)
- `ev_loop.c` — select()-based event loop implementation

Key differences from real libev:
- Uses `select()` instead of `epoll()`
- Timer `at` values are absolute timestamps (converted by `ev_timer_start`)
- **`timer.set()` must NOT be used to reset active timers** — use `stop()` + `start()`
- Snapshot-based callback dispatch (handles list modifications during callbacks)
- 100ms select timeout cap for responsive TCP accept

### IP Secure implementation

Uses knxd's `ipsecure.cpp` with dual crypto backend:
- `#ifdef HAVE_OPENSSL` — OpenSSL (desktop/Linux)
- `#else` — mbedTLS (ESP32)

Crypto operations:
- **X25519 ECDH:** ~400ms per handshake on ESP32 (software, no hardware accelerator)
- **AES-128-CCM:** Hardware-accelerated AES on ESP32
- **SHA-256:** Hardware-accelerated
- **PBKDF2-SHA256:** ~10s for 65536 iterations (cached in NVS)

### Performance

| Operation | Time |
|-----------|------|
| Boot to ready | ~5s (with cached PBKDF2 keys) |
| First boot PBKDF2 derivation | ~20s (two keys) |
| X25519 ECDH handshake | ~400ms |
| Session authentication | <1ms |
| KNX bus telegram round-trip | ~150ms |

### Benchmark: ESP32 vs Wirenboard (same knxd, same IP Secure)

Tested with both gateways running knxd with IP Secure (XKNX client, 3 runs averaged):

**Connection time (IP Secure handshake):**

| Gateway | Connect time | Notes |
|---------|-------------|-------|
| WB (ARM Cortex-A7 @ 1GHz) | **0.105s** | X25519 fast on ARM |
| ESP32 (Xtensa @ 240MHz) | **0.968s** | X25519 software-only |
| **Ratio** | **9.2x** | ECDH is the bottleneck |

**Message throughput (10 group writes, burst):**

| Gateway | 10 writes | Per message |
|---------|----------|-------------|
| WB | 2.004s | 200ms |
| ESP32 | 2.004s | 200ms |
| **Ratio** | **1.0x** | **Identical throughput** |

**Device Info read (4 P2P property reads via T_Connect + T_Data_Connected):**

| Gateway | 4 property reads | Total (incl. connect) |
|---------|-----------------|----------------------|
| WB | 8.027s | 8.161s |
| ESP32 | 8.306s | 9.386s |
| **Ratio** | **1.03x** | **1.15x** |

**ETS Device Info read (via IP Secure):**

| Gateway | Time | Drops | Ratio |
|---------|------|-------|-------|
| WB (knxd, ARM Cortex-A7) | **7.1s** | 0 | 1.0x |
| ESP32 (before CR→LF fix) | ~55s | 36 | 7.7x |
| ESP32 (after CR→LF fix) | **~11s** | **0** | **1.5x** |

The 3x slowdown for full device reads is caused by T_Data_Connected response forwarding delays: the device responds within ~500ms, but Calimero's transport layer times out (~5s) waiting for the response to come back through the tunnel. This causes retries for each property read. The WB handles the same responses without timeout.

**Root cause of ETS slowdown (fixed):**

The ESP-IDF VFS UART layer was performing CR→LF translation (0x0D→0x0A) on the NCN5120 RX data. This corrupted any KNX frame containing byte 0x0D (e.g., PID=13), causing checksum failures and frame drops. ETS then retried with 5s timeouts, multiplying the total read time.

Fix: `uart_vfs_dev_port_set_rx_line_endings(uart_num, ESP_LINE_ENDINGS_LF)` disables the translation.

**Key finding:** Group communication (broadcast) has identical throughput (1.0x). After the CR→LF fix, ETS Device Info reads are 1.5x slower than WB, entirely due to the ECDH handshake overhead.

### Known limitations

- ECDH is ~400ms per handshake (ESP32 has no X25519 hardware). ETS opens 3 connections, so initial connection takes ~1.5s.
- Maximum 4 concurrent tunnel clients (`client-addrs = 1.1.200:4`)
- No UDP discovery/multicast — clients must connect directly via TCP to the IP address
- Debug printf logging is currently enabled (can be removed for production)

## File inventory

### New files

```
main/knx_server.cpp              — KNX server wrapper (271 lines)
main/knx_server.h                — KNX server C API (35 lines)
main/setting_items.{h,c}         — KNX settings keys (+6 lines each)
main/settings_manager.c          — KNX settings JSON API (+22 lines)
main/frontend/src/views/Settings.vue — KNX web UI section
main/frontend/src/assets/eye.svg — Password show icon
main/frontend/src/assets/eyeOff.svg — Password hide icon
components/libev_shim/ev.h       — libev C API shim (123 lines)
components/libev_shim/ev++.h     — libev C++ API shim (157 lines)
components/libev_shim/ev_loop.c  — select()-based event loop (284 lines)
components/knxd_core/config.h    — ESP32 feature flags (37 lines)
components/knxd_core/fmt/format.h — printf shim for std::string
components/knxd_core/knxd/       — knxd source tree (copied from wirenboard-knxd)
_tools/test_xknx.py              — XKNX reliability test script
_tools/flash_monitor.sh          — Flash and monitor helper
```

### Modified knxd files (ESP32 adaptations)

```
src/libserver/ipsecure.cpp       — mbedTLS backend (#ifdef HAVE_OPENSSL), debug prints
src/libserver/tcptunserver.cpp   — reset_timer() fix, destructor fix, debug prints
src/libserver/tcptunserver.h     — member order fix (fd before sendbuf/recvbuf)
src/libserver/server.cpp         — debug prints
src/libserver/eibnetserver.cpp   — reset_timer() fix
src/common/iobuf.h               — pre-set fd/events in SendBuf::init()
src/common/iobuf.cpp             — debug prints
src/common/queue.h → knxd_queue.h — renamed to avoid FreeRTOS collision
```

## Known bugs and TODO

### Bugs

1. **`~TcpTunConn` destructor crash on ETS disconnect** — When ETS opens multiple TCP connections and some fail authentication, the cleanup path occasionally triggers a LoadProhibited crash in the shared_ptr destructor chain. The `t.reset()` fix handles the common case but not all multi-connection teardown scenarios. Workaround: ESP32 reboots automatically and reconnects within ~5s.

2. **ECDH pre-generation not working** — The `pregenECDHKeypair()` code compiles and the keypair is generated, but `removeSession()` doesn't reliably trigger regeneration (printf after `sessions.erase()` never appears in serial log). The pregen from boot works for the first connection but subsequent connections don't benefit. Needs investigation into the removeSession code path.

3. **NatL2Filter not in driver chain** — `filter = single` is registered in the factory but `findFilter("single")` returns null during `TPUARTwrap::setup()` because the filter chain isn't built yet at that point. The fallback reads the address from config (`addr` key). This means the NatL2 address rewriting doesn't work — frames go out with the tunnel client address instead of the router address.

### Performance improvements TODO

1. **ECDH handshake optimization** — X25519 takes ~400ms on ESP32 (software-only). Options:
   - Pre-generate keypairs in a background FreeRTOS task (attempted, OOM on boot — needs deferred generation)
   - Use the second ESP32 core for ECDH computation
   - Cache session keys for reconnecting clients (KNX spec may not allow this)

2. **Remove debug printf logging** — The `[IPSEC]`, `[TCPTUN]`, `[SERVER]`, `[SENDBUF]`, `[RECVBUF]` debug prints add latency and fill the serial log. Remove or gate behind a runtime flag for production.

3. **Consider using raw `uart_read_bytes()` instead of VFS** — The VFS layer adds overhead (line ending translation was one issue, select() polling is another). Direct UART driver API would give lower latency for bus frame processing.

### Feature TODO

1. **Production defaults** — KNX disabled by default, empty WiFi credentials, remove hardcoded test SSID/password from `config.h`

2. **Dashboard KNX statistics** — Show client count, telegram count, unique addresses on the web UI

3. **Disable RS-485 port 1 in UI** when KNX is enabled (shares UART1)

4. **UDP multicast discovery** — ETS can currently only connect by entering the IP address manually. Adding KNXnet/IP discovery (SEARCH_REQUEST on 224.0.23.12:3671) would allow automatic detection.

5. **KNX Data Secure** — The `fdatasecure` filter from the `tmp/datasecure` branch could be integrated to support group data encryption/decryption.

6. **OTA firmware update** — The wb-mge firmware supports OTA but the KNX component hasn't been tested with it.

