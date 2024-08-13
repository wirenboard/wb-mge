idf_component_register(SRCS "main.c"
                            "wifi_apsta.c"
                            "tcp_server.c"
                            "serial.c"
                            "http_server.c"
                            "nv_storage.c"
                        INCLUDE_DIRS "."
                        EMBED_TXTFILES
                        frontend/index.html
)
