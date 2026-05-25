idf_component_register(SRCS "MAC.c" "intracom.c" "intercom.c" "main.c"
                        INCLUDE_DIRS "."
                        REQUIRES nvs_flash esp_wifi driver esp_driver_gpio esp_netif esp_event led_strip)