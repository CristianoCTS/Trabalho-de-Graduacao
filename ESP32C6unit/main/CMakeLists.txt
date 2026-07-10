idf_component_register(
    SRCS "coms.c" "MAC.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        esp_timer
        driver esp_driver_gpio
)