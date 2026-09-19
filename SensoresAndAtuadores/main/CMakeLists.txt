idf_component_register(SRCS "humidity.c" "temperature.c" "main.c" "receiver.c" "emiter.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver freertos esp_event esp_driver_gpio esp_driver_rmt esp_driver_spi led_strip)