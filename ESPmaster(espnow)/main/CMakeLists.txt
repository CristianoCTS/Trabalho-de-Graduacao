idf_component_register(
    SRCS "intercom.c" "MQTT.c" "main.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_http_client
        esp_timer
)