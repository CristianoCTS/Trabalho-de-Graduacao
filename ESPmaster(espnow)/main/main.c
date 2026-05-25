#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "MAC.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "led_strip.h"



#define LED GPIO_NUM_8 //LED
#define LED_all GPIO_NUM_10 //GPIO para mandar para all
#define LED_one GPIO_NUM_11 //GPIO para mandar para apenas um

static bool led_status = false; //Led apagado ou acesso
static led_strip_handle_t led_strip; // Handle para o controle do LED
uint8_t *slaves[10]; //MACs dos slaves
char msn[20] = ""; //Mensagem

void configure_led(void) {
    led_strip_config_t strip_config = {
        .strip_gpio_num = LED,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000,
        .flags.with_dma = false,
    };
    led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip);
    led_strip_clear(led_strip);
}

void set_led(bool state) {
    if (state) {
        led_strip_set_pixel(led_strip, 0, 16, 16, 16);
        led_strip_refresh(led_strip);
    } else {
        led_strip_clear(led_strip);
    }
}

void receive_msg(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    char msg[20] = "";
    memset(msg, 0, sizeof(msg));
    if (len < sizeof(msg)) {
        memcpy(msg, data, len);
        msg[len] = '\0';
    }
    printf("Mensagem recebida: '%s'\n", msg);
    if (strcmp(msg, "Toggle") == 0) {
        led_status = !led_status;
        set_led(led_status);
        printf("LED: %s\n", led_status ? "ON" : "OFF");
    }
}

void WhoIsWho(void) { //Criando os slaves
    int j = 0;
    for (int i = 0; i < NUM_ESPS; i++) {
        if (!ESP[i].Iam) {
            slaves[j] = ESP[i].mac;
            j++;
        }
    }
}

void app_main(void) {
    //setup das conexões
    nvs_flash_init();
    configure_led();
    mac_init();
    WhoIsWho();

    //Configuração dos botões
    gpio_reset_pin(LED_all);
    gpio_set_direction(LED_all, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_all, GPIO_PULLUP_ONLY);
    gpio_reset_pin(LED_one);
    gpio_set_direction(LED_one, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_one, GPIO_PULLUP_ONLY);

    // Configurando a ESP como estação
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    // Iniciando o ESP-NOW
    esp_now_init();
    //Inciando a recepção de mensagens
    esp_now_register_recv_cb(receive_msg);

    //Configurando os envios para os slaves
    for (int i = 0; i < (NUM_ESPS-1); i++){
        esp_now_peer_info_t slaveInfo = {};
        memcpy(slaveInfo.peer_addr, slaves[i], 6);  // Endereço do slave
        slaveInfo.channel = 0;
        slaveInfo.encrypt = false;
        esp_now_add_peer(&slaveInfo);
    }

    while (1) {
        if (gpio_get_level(LED_all) == 0) {  // LOW = botão pressionado
            for (int i = 0; i < (NUM_ESPS-1); i++) {
                printf("Button for all Pressed! Sent to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                    slaves[i][0], slaves[i][1], slaves[i][2],
                    slaves[i][3], slaves[i][4], slaves[i][5]);
                strcpy(msn, "Toggle"); //Atribuindo a mensagem à variavel
                esp_now_send(slaves[i], (uint8_t *)msn, strlen(msn)); // Envia a mensagem
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        if (gpio_get_level(LED_one) == 0) {  // LOW = botão pressionado
            printf("Button for one Pressed! Sent to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                slaves[0][0], slaves[0][1], slaves[0][2],
                slaves[0][3], slaves[0][4], slaves[0][5]);
            strcpy(msn, "Toggle"); //Atribuindo a mensagem à variavel
            esp_now_send(slaves[0], (uint8_t *)msn, strlen(msn)); // Envia a mensagem
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}