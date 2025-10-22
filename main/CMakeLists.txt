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
                   "wb_app_desc/wb_app_desc.c"
                   "settings_update.c"
                   "settings_save_timer.c"
                   "debug_log.c")

# Conditionally add files based on build type
if(CONFIG_ETH_USE_OPENETH)
    # QEMU build - add QEMU-specific files only
    set(CONDITIONAL_SRCS
        "qemu/ethernet_qemu.c"
        "qemu/wifi_qemu_mock.c"
        "qemu/hardware_mocks_qemu.c")
    message(STATUS "Building for QEMU - including QEMU-specific files only")
else()
    # Hardware build - add hardware-specific files only
    set(CONDITIONAL_SRCS
        "gpio_expander.c"
        "rs485_control.c"
        "mio_control.c"
        "update_rs485_mio_gpio_states.c"
        "config_button.c"
        "indication/indication.c"
        "indication/leds_control.c"
        "voltage_monitor/system_voltage.c"
        "voltage_monitor/voltage_monitor.c"
        "copy_protection/copy_protection.c"
        "copy_protection/port_expander_tests.c")
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
                            "copy_protection"
                        EMBED_TXTFILES
                            frontend/dist/favicon.webp.gz
                            frontend/dist/index.css.gz
                            frontend/dist/index.html.gz
                            frontend/dist/index.js.gz
)

target_link_libraries(${COMPONENT_TARGET} "-u wb_app_desc")
