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
                        INCLUDE_DIRS "."
                        EMBED_TXTFILES
                        frontend/index.html
                        frontend/favicon.ico
)
