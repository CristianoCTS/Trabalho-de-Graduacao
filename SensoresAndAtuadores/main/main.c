#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "curtain.h"
#include "emiter.h"
#include "humidity.h"
#include "temperature.h"

float DS18B20 = 0.0f;
dht22_reading_t DHT22 = { 0.0f, 0.0f };
int16_t ocupacao = 0;

void app_main(void)
{
    curtain_init();
    emiter_init();
    DHT22_init();
    DS18B20_init();
    while (true) {
        ocupacao += carga_termica;
        carga_termica = 0;
        if (DS18B20_read(&DS18B20)) {
            printf("=====DS18B20: lido=====\n");
        }
        if (DHT22_read(&DHT22)) {
            printf("======DHT22: lido======\n");
        }
        printf("Ocupacao: %d\n", ocupacao);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

}