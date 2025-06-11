idf_component_register(SRCS "main.c"
                            "bridge.c"
                            "ethernet.c"
                            "http_server.c"
                            "nv_storage.c"
                            "serial.c"
                            "setting_items.c"
                            "ssdp.c"
                            "tcp_client.c"
                            "tcp_server.c"
                            "wifi_apsta.c"
                            "sys_info.c"
                            "efuse_table.c"
                            "rs485_control.c"
                            "mio_control.c"
                            "leds_control.c"
                        INCLUDE_DIRS "."
                            "include"
                        EMBED_TXTFILES
                            frontend/dist/favicon.webp.gz
                            frontend/dist/index.css.gz
                            frontend/dist/index.html.gz
                            frontend/dist/index.js.gz
)
