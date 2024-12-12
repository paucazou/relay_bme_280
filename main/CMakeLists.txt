idf_component_register(SRCS "main.c" "wifi.c" "udp_server.c"
                    PRIV_REQUIRES spi_flash esp-idf-bmx280 nvs_flash esp_wifi esp_http_client
                    INCLUDE_DIRS "")
