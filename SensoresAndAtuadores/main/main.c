#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "receiver.h"
#include "emiter.h"
#include "temperature.h"
#include "humidity.h"

void app_main(void)
{
    emiter_init();
    temperature_init();
    // humidity_init();
    receiver_init();   // retorna imediatamente, a escuta roda em tarefa propria

    float temp_ds = 0.0f;
    dht22_reading_t dht;

    while (1) {
        if (temperature_get(&temp_ds)) {
            printf("[DS18B20] Temperatura: %.2f C\n", temp_ds);
        }

        // if (humidity_get(&dht)) {
        //     printf("[DHT22]   Temperatura: %.1f C | Umidade: %.1f %%\n",
        //            dht.temperature, dht.humidity);
        // }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}