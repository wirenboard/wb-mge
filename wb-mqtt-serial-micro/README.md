# wb_modbus_bridge

Minimal Modbus RTU -> MQTT bridge for a **single WB-style device**,
written in C99 with an eye toward running on an MCU (ESP32, RP2040, ...).

Proof of concept that reads the same JSON templates as wb-mqtt-serial
and does a bidirectional Modbus <-> MQTT bridge for one device.

## What it does

- Parses a `wb-mqtt-serial` JSON device template (the same files in `../templates/`).
- Sequentially polls every enabled channel via standard Modbus RTU
  (FC01/02/03/04 for reads).
- Publishes values to MQTT under `/devices/<device_id>/controls/<name>`.
- Subscribes to `/devices/<device_id>/controls/<name>/on` and writes
  values back via FC05 (coils) or FC06/FC16 (holding registers).

## Supported from template format

| Feature | Status |
|---------|--------|
| reg_type: holding / input / coil / discrete | OK |
| format: u8/s8/u16/s16/u32/s32/u64/s64/float | OK |
| format: string (ASCII, big-endian bytes) | OK |
| format: bcd8/bcd16/bcd24/bcd32 | OK |
| scale, offset | OK |
| error_value (hex string or number) | OK, publishes "Error" on match |
| word_order: big_endian (default) / little_endian | OK |
| byte_order: big_endian (default) / little_endian | OK |
| enabled: false -- channel skipped | OK |
| consists_of (RGB composite channels) | skipped silently |
| condition fields | ignored, all channels polled unconditionally |
| parameters / setup sections | ignored |
| Jinja2 templates (.json.jinja) | not supported; render first with dump_templates.py |
| WB Fast/Continuous Read | excluded by design |
| Multiple devices / ports | one slave per process |
| Write to string/BCD registers | not supported (those channels are readonly anyway) |

## Quick start on Linux / Wirenboard

### 1. Install prerequisites

```sh
sudo apt install libmosquitto-dev mosquitto gcc make curl
```

### 2. Fetch cJSON and build

```sh
make deps   # downloads cJSON.c / cJSON.h from GitHub
make
```

### 3. Run

```sh
systemctl stop wb-mqtt-serial
./wb_bridge /dev/ttyRS485-1 9600 2 131 \
    ../templates/config-wb-msw_v4.json \
    localhost 1883
```

Arguments:
| # | Argument | Example |
|---|----------|---------|
| 1 | Serial port | `/dev/ttyRS485-1` |
| 2 | Baud rate | `9600` |
| 3 | Stop bits | `1` or `2` |
| 4 | Modbus slave ID | `131` |
| 5 | Template JSON path | `../templates/config-wb-msw_v4.json` |
| 6 | MQTT broker host | `localhost` |
| 7 | MQTT broker port | `1883` (optional, default 1883) |

> **Note:** parity is hardcoded to `N` (no parity). Most WB devices use 8N1 or 8N2.
> Response timeout is 300 ms (suitable for 9600 baud).

### 4. Test template parsing without a device

```sh
./wb_bridge --test-template ../templates/config-wb-msw_v4.json
```

### 5. Watch MQTT topics

```sh
mosquitto_sub -h localhost -t '/devices/#' -v
```

### 6. Write a value

```sh
# Turn on buzzer on WB-MSW
mosquitto_pub -h localhost -t '/devices/wb-msw-v4/controls/Buzzer/on' -m '1'

# Turn off
mosquitto_pub -h localhost -t '/devices/wb-msw-v4/controls/Buzzer/on' -m '0'
```

## Code structure

```
wb-mqtt-serial-micro/
|-- Makefile
|-- README.md
|-- src/
|   |-- template.h / template.c     # JSON template parser
|   |-- modbus_frame.h / modbus_frame.c  # CRC16, frame building
|   |-- modbus_rtu.h / modbus_rtu.c  # Modbus RTU transport (POSIX serial)
|   |-- value_conv.h / value_conv.c  # Format conversion (BCD, byte/word order)
|   |-- mqtt_client.h / mqtt_client.c  # MQTT (libmosquitto on Linux)
|   `-- bridge.c                     # Main: poll loop + publish + subscribe
`-- tests/
    |-- test_crc.c          # CRC16 and frame tests
    |-- test_value_conv.c   # Format conversion tests (50+ cases)
    |-- test_template.c     # Template parser tests
    `-- unity/              # Unity test framework
```

## Rendering Jinja2 templates

Devices like WB-MR6C, WB-MAI6, WB-MR6LV have `.json.jinja` templates.
Render them first:

```sh
pip install jinja2
python3 dump_templates.py templates/config-wb-mr6c.json.jinja
# produces templates/config-wb-mr6c.json
```

## Porting to an MCU

Platform-specific code is isolated in `#ifdef POSIX_PLATFORM` blocks:

| Layer | Linux | MCU replacement |
|-------|-------|-----------------|
| Serial port | POSIX termios | `uart_driver_install()` / `uart_write_bytes()` / `uart_read_bytes()` |
| MQTT | libmosquitto | `esp_mqtt_client` or PubSubClient |
| File I/O (template) | `fopen`/`fread` | SPIFFS / LittleFS / embedded const array |
| Sleep | `nanosleep` | `vTaskDelay` |

Everything else -- CRC16, Modbus framing, FC functions, value conversion,
template parsing (cJSON is available for ESP-IDF), poll loop -- is pure C99
and compiles unchanged.

## MQTT topic scheme

```
/devices/<device_id>/meta/name              -> device name string (published once)
/devices/<device_id>/controls/<name>        -> current value (published on change)
/devices/<device_id>/controls/<name>/on     -> write target (subscribed)
```

Compatible with wb-rules and wb-dashboard.
