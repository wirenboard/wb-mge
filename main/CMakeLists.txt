set(COMPONENT_SRCS "main.c"
                   "auth.c"
                   "http_server.c"
                   "info_handlers.c"
                   "nv_storage.c"
                   "setting_items.c"
                   "setting_validators.c"
                   "sys_info.c"
                   "json_utils.c"
                   "wifi_scan.c"
                   "settings_manager.c"
                   "cmd_handler.c"
                   "ota_handler.c"
                   "network/network.c"
                   "network/ethernet.c"
                   "network/wifi_apsta.c"
                   "bridge/bridge.c"
                   "bridge/serial.c"
                   "bridge/tcp_client.c"
                   "bridge/tcp_server.c"
                   "bridge/transparent_tcp.c"
                   "bridge/modbus_tcp.c"
                   "bridge/packet_queue.c"
                   "bridge/modbus_helpers.c"
                   "bridge/rs485_stats.c"
                   "bridge/fast_modbus.c"
                   "bridge/sniffer.c"
                   "bridge/stream_splitter.c"
                   "bridge/cache_multimaster.c"
                   "bridge/cache_modbus_server.c"
                   "bridge/mb_device.c"
                   "bridge/port_manager.c"
                   "bridge/repeater.c"
                   "wb_app_desc/wb_app_desc.c"
                   "settings_update.c"
                   "settings_save_timer.c"
                   "debug_log.c"
                   "wb_test.c"
                   "coverage_dump.c"
                   "update_rs485_mio_gpio_states.c")

# Conditionally add files based on build type
if($ENV{CONFIG_ETH_USE_OPENETH})
    # QEMU build - run the REAL hardware-logic modules against the virtual IO bus
    # (virtual_io_qemu.c provides the gpio_expander.h symbols backed by a RAM shadow;
    # do NOT compile the real gpio_expander.c here). voltage_monitor stays excluded.
    set(CONDITIONAL_SRCS
        "qemu/ethernet_qemu.c"
        "qemu/wifi_qemu_mock.c"
        "qemu/hardware_mocks_qemu.c"
        "qemu/virtual_io_qemu.c"
        "qemu/gpio_shim_qemu.c"
        "qemu/gpio_dir_map.c"
        "indication/indication.c"
        "indication/leds_control.c"
        "rs485_control.c"
        "mio_control.c"
        "config_button.c")
    message(STATUS "Building for QEMU - including QEMU-specific files only")

    # QEMU-only: transparently intercept the real ESP-IDF GPIO/UART API via the
    # linker so the SAME firmware code drives both real and emulated pins. Each
    # --wrap=NAME redirects calls to __wrap_NAME (defined in gpio_shim_qemu.c)
    # while __real_NAME still reaches the original IDF implementation. These are
    # build-level LINK_OPTIONS applied ONLY in this QEMU branch (the hardware
    # build below is untouched). One option per wrapped function.
    # Guarded against the early component-requirements expansion pass, where
    # idf_build_set_property() is not yet defined (this file is also include()d
    # in script mode to collect REQUIRES).
    if(NOT CMAKE_BUILD_EARLY_EXPANSION)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=gpio_config" APPEND)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=gpio_set_direction" APPEND)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=gpio_reset_pin" APPEND)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=gpio_set_level" APPEND)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=gpio_get_level" APPEND)
        idf_build_set_property(LINK_OPTIONS "-Wl,--wrap=uart_set_pin" APPEND)
    endif()
else()
    # Hardware build - add hardware-specific files only
    set(CONDITIONAL_SRCS
        "gpio_expander.c"
        "rs485_control.c"
        "mio_control.c"
        "config_button.c"
        "indication/indication.c"
        "indication/leds_control.c"
        "voltage_monitor/system_voltage.c"
        "voltage_monitor/voltage_monitor.c")
    message(STATUS "Building for hardware - including hardware-specific files only")
endif()

# Add conditional sources to main sources
list(APPEND COMPONENT_SRCS ${CONDITIONAL_SRCS})

idf_component_register(SRCS ${COMPONENT_SRCS}
                        INCLUDE_DIRS "."
                            "bridge"
                            "indication"
                            "network"
                            "wb_app_desc"
                            "voltage_monitor"
                            "qemu"
                        EMBED_TXTFILES
                            frontend/dist/favicon.webp.gz
                            frontend/dist/index.css.gz
                            frontend/dist/index.html.gz
                            frontend/dist/index.js.gz
                            frontend/dist/roboto-latin-wght-normal.woff2.gz
                            frontend/dist/roboto-cyrillic-wght-normal.woff2.gz
                            frontend/dist/roboto-cyrillic-ext-wght-normal.woff2.gz
)

target_link_libraries(${COMPONENT_TARGET} "-u wb_app_desc")

# Optional gcov coverage instrumentation for the application code (this component only).
# Enable with: idf.py -DCOVERAGE=1 build  (or `make build-idf-project COVERAGE=1`); off by default.
# Adds --coverage (-fprofile-arcs -ftest-coverage): emits .gcno at compile time and .gcda at
# runtime. It increases flash/RAM footprint, so it must NOT be part of release firmware.
if(COVERAGE)
    message(STATUS "Coverage instrumentation ENABLED for main component (--coverage)")
    target_compile_options(${COMPONENT_TARGET} PRIVATE --coverage -fprofile-info-section)
    target_compile_definitions(${COMPONENT_TARGET} PRIVATE COVERAGE_BUILD=1)
    # Plain signature here to match the existing target_link_libraries() call above
    # (CMake forbids mixing plain and keyword signatures on the same target).
    target_link_libraries(${COMPONENT_TARGET} "--coverage")
    # coverage.ld holds an `INSERT AFTER .flash.rodata` for the .gcov_info section.
    # GNU ld only resolves the INSERT anchor when the augmenting script appears on the
    # link line BEFORE the script that defines the anchor (esp_system's sections.ld).
    # target_linker_script() on this component would emit -T coverage.ld AFTER sections.ld
    # (main links after esp_system) and ld would fail with ".flash.rodata not found for
    # insert". Register it as a build-level LINK_OPTION instead: build-level options are
    # applied to the project ELF before the per-component (esp_system) link options, so
    # -T coverage.ld lands ahead of -T sections.ld on the command line.
    # Pass "-T" and the script as two separate options: a single "-T coverage.ld" token
    # reaches ld as one argument with a leading space and fails to open.
    idf_build_set_property(LINK_OPTIONS "-L${CMAKE_CURRENT_SOURCE_DIR}" APPEND)
    idf_build_set_property(LINK_OPTIONS "-T" APPEND)
    idf_build_set_property(LINK_OPTIONS "coverage.ld" APPEND)
    set_property(TARGET ${COMPONENT_TARGET} APPEND PROPERTY INTERFACE_LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/coverage.ld")
endif()
