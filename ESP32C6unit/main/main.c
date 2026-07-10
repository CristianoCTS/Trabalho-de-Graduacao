#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "nvs_flash.h"
#include "MAC.h"
#include "coms.h"
#include <stdio.h>
#include "driver/gpio.h"
#include "led_strip.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

#define LED GPIO_NUM_8 //LED
#define LED_all GPIO_NUM_10 //GPIO para mandar para all
#define LED_one GPIO_NUM_11 //GPIO para mandar para apenas um
#define LED_two GPIO_NUM_12 //GPIO para mandar para apenas um

static bool led_status = false; //Led apagado ou acesso
static led_strip_handle_t led_strip; // Handle para o controle do LED
char received_msg[20] = "";
char buf[12];
char instrucao[20] = "";
int brokeri = 0;

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

void app_main(void) {
    //setup das conexões
    coms_init();
    configure_led();


    //Configuração dos botões
    gpio_reset_pin(LED_all);
    gpio_set_direction(LED_all, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_all, GPIO_PULLUP_ONLY);
    gpio_reset_pin(LED_one);
    gpio_set_direction(LED_one, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_one, GPIO_PULLUP_ONLY);
    gpio_reset_pin(LED_two);
    gpio_set_direction(LED_two, GPIO_MODE_INPUT);
    gpio_set_pull_mode(LED_two, GPIO_PULLUP_ONLY);
    
    while (1) {
        snprintf(buf, sizeof(buf), "%d", brokeri);
        intercom_send(buf);
        brokeri++;
        if (brokeri > 50) {
            brokeri = 0;
        }
        intercom_read(instrucao);
        if (gpio_get_level(LED_all) == 0 || strcmp(instrucao, "all") == 0) {
            intracom_send("Toggle", -1);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        if (gpio_get_level(LED_one) == 0 || strcmp(instrucao, "one") == 0) {
            intracom_send("Toggle", 0);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        if (gpio_get_level(LED_two) == 0 || strcmp(instrucao, "two") == 0) {
            intracom_send("Toggle", 1);
            vTaskDelay(pdMS_TO_TICKS(300));
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        intracom_read(received_msg);
        printf("Received: %s\n", received_msg);
        if (strcmp(received_msg, "Toggle") == 0) {
            memset(received_msg, 0, sizeof(received_msg));
            led_status = !led_status;
            set_led(led_status);
            printf("LED: %s\n", led_status ? "ON" : "OFF");
        }
    }
}