idf_component_register(SRCS "MAC.c" "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES nvs_flash esp_wifi esp_driver_gpio)