idf_component_register(SRCS "main.c"
                            "wifi_apsta.c"
                            "ethernet.c"
                            "tcp_server.c"
                            "tcp_client.c"
                            "serial.c"
                            "http_server.c"
                            "nv_storage.c"
                            "ssdp.c"
                            "setting_items.c"
                            "bridge.c"
                        INCLUDE_DIRS "."
                        EMBED_TXTFILES
                        frontend/index.html
                        frontend/favicon.ico
)
