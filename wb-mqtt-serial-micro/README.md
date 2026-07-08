# wb-mqtt-serial-micro

Host unit-test harness for the shared mqtt-serial modules that live in
`main/mqtt_serial/` (firmware). The harness compiles those firmware sources
directly — there are no copies of the shared modules in this tree.

The standalone `wb_bridge` proof-of-concept application was retired: the
firmware `main/mqtt_serial/mqtt_serial_bridge.c` fully replaces it. The former
`src/` directory has been removed.

## Layout

```
wb-mqtt-serial-micro/
|-- tests/                # Unity-based unit tests + Makefile
|   |-- Makefile
|   |-- test_crc.c
|   |-- test_value_conv.c
|   |-- test_template.c
|   |-- test_condition.c
|   |-- test_enum.c
|   |-- test_fast_modbus.c
|   |-- test_poll_planner.c
|   `-- unity/            # Unity test framework (ThrowTheSwitch)
`-- thirdparty/
    `-- cJSON/            # cJSON.c / cJSON.h, used by template.c
```

## Running the tests

Run all tests:

```sh
make -C tests
```

Run a single test target:

```sh
make -C tests <target>
```

Available targets:

| Target              | Module under test                          | What it covers                                  |
|---------------------|--------------------------------------------|-------------------------------------------------|
| `test_crc`          | `modbus_frame.c`                           | CRC16 computation and frame building            |
| `test_value_conv`   | `value_conv.c`                             | Register-to-value conversion (BCD, byte/word order, strings, error values) |
| `test_template`     | `template.c`                               | JSON template parsing (requires fixture files under `templates/`) |
| `test_condition`    | `template.c`                               | Conditional channel evaluation                  |
| `test_enum`         | `template.c`                               | Channel enum parsing and lookup                 |
| `test_fast_modbus`  | `fast_modbus_events.c`, `modbus_frame.c`   | WB Fast/Continuous Read event build and parse   |
| `test_poll_planner` | `poll_planner.c`                           | Register-range merging and poll-group planning  |

## Host build of firmware sources

`main/mqtt_serial/template.c` is the only shared module with ESP-IDF
dependencies. It guards them behind `#ifdef ESP_PLATFORM`: on the firmware
target (where ESP-IDF auto-defines `ESP_PLATFORM`) the real ESP logging and
heap-query calls are used; on the host the shim maps ESP logging to `stderr`
and the heap query to 0, so the same source compiles unchanged for unit tests.

All other shared modules (`modbus_frame`, `value_conv`, `fast_modbus_events`,
`poll_planner`) are pure C99 and compile on the host without any shim.
