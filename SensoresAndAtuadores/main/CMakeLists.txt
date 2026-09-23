idf_component_register(SRCS "emiter.cpp" "curtain.c" "humidity.c" "temperature.c" "main.c" "identify.cpp" 
                        INCLUDE_DIRS "."
                        REQUIRES driver freertos esp_event esp_driver_gpio esp_driver_rmt esp_driver_spi led_strip IRremoteIDF)