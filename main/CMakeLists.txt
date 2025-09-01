idf_component_register(SRCS "main.c"
                            "ethernet.c"
                            "auth.c"
                            "http_server.c"
                            "info_handlers.c"
                            "nv_storage.c"
                            "setting_items.c"
                            "wifi_apsta.c"
                            "sys_info.c"
                            "gpio_expander.c"
                            "rs485_control.c"
                            "mio_control.c"
                            "update_rs485_mio_gpio_states.c"
                            "json_utils.c"
                            "wifi_scan.c"
                            "settings_manager.c"
                            "cmd_handler.c"
                            "ota_handler.c"
                            "system_voltage.c"
                            "config_button.c"
                            "bridge/bridge.c"
                            "bridge/serial.c"
                            "bridge/tcp_client.c"
                            "bridge/tcp_server.c"
                            "bridge/transparent_tcp.c"
                            "bridge/modbus_tcp.c"
                            "bridge/packet_queue.c"
                            "bridge/modbus_helpers.c"
                            "bridge/rs485_stats.c"
                            "indication/indication.c"
                            "indication/leds_control.c"
                            "wb_app_desc/wb_app_desc.c"
                        INCLUDE_DIRS "."
                            "bridge"
                            "indication"
                            "wb_app_desc"
                        EMBED_TXTFILES
                            frontend/dist/favicon.webp.gz
                            frontend/dist/index.css.gz
                            frontend/dist/index.html.gz
                            frontend/dist/index.js.gz
)

target_link_libraries(${COMPONENT_TARGET} "-u wb_app_desc")
