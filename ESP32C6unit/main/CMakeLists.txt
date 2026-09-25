idf_component_register(
    SRCS "main.c" "coms.c" "MAC.c" "emiter.cpp" "curtain.c" "humidity.c" "temperature.c" "identify.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        esp_timer
        driver 
        esp_driver_gpio
        freertos 
        esp_driver_rmt
        esp_driver_spi 
        led_strip 
        irremoteidf
)