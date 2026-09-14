idf_component_register(SRCS "main.c" "receiver.c" "emiter.c"
                        INCLUDE_DIRS "."
                        REQUIRES driver freertos esp_event esp_driver_gpio esp_driver_rmt)