#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "MAC.h"
#include <stdio.h>
#include "driver/gpio.h"

#define button GPIO_NUM_4

uint8_t *slaves[10]; //MACs dos slaves
char msn[20] = ""; //Mensagem

void WhoIsWho(void) {
    int j = 0;
    for (int i = 0; i < NUM_ESPS; i++) {
        if (!ESP[i].Iam) {
            slaves[j] = ESP[i].mac;
            j++;
        }
    }
}

void app_main(void) {
    nvs_flash_init();
    mac_init();
    WhoIsWho();

    //Configuração do botão
    gpio_reset_pin(button);
    gpio_set_direction(button, GPIO_MODE_INPUT);
    gpio_set_pull_mode(button, GPIO_PULLUP_ONLY);

    // Configurando a ESP como estação
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // Iniciando o ESP-NOW
    esp_now_init();

    // Estrutura de configuração
    esp_now_peer_info_t slaveInfo = {};
    memcpy(slaveInfo.peer_addr, slaves[0], 6);  // Endereço do escravo
    slaveInfo.channel = 0;                  // Channel de comunicação
    slaveInfo.encrypt = false;              // Desabilita a criptografia
    esp_now_add_peer(&slaveInfo);           // Registro das informações de slaveInfo

    // Loop principal
    while (1) {
        if (gpio_get_level(button) == 0) {              // LOW = botão pressionado
            strcpy(msn, "Toggle");                      // Se button for pressionado, aponte "toggle"
            esp_now_send(slaves[0], (uint8_t *)msn, strlen(msn)); // Envia a mensagem
            vTaskDelay(pdMS_TO_TICKS(300));             // Delay
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}