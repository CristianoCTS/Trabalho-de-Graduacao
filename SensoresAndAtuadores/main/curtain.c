#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "curtain.h"

#define BTN1_GPIO GPIO_NUM_7
#define BTN2_GPIO GPIO_NUM_6
#define led_gpio 8
#define blink_ms 200
#define poll 20
#define debounce 50

int16_t carga_termica = 0;

static led_strip_handle_t strip;

typedef enum {
    BTN_N = 0,
    BTN_1,
    BTN_2,
    BTN_A,
} btn_combo_t;

static btn_combo_t BTN_read(void)
{
    bool b1 = (gpio_get_level(BTN1_GPIO) == 0);
    bool b2 = (gpio_get_level(BTN2_GPIO) == 0);

    if (b1 && b2) return BTN_A;
    if (b1) return BTN_1;
    if (b2) return BTN_2;
    return BTN_N;
}

static void led_blink(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(strip, 0, r, g, b);
    led_strip_refresh(strip);
    vTaskDelay(pdMS_TO_TICKS(blink_ms));
    led_strip_clear(strip);
}

static void curtain_task(void *params)
{
    btn_combo_t anterior = BTN_N;

    while (true) {
        btn_combo_t atual = BTN_read();

        if (anterior == BTN_N && atual != BTN_N) {
            vTaskDelay(pdMS_TO_TICKS(debounce));
            atual = BTN_read();
            switch (atual) {
                case BTN_1:
                    printf("Botao 7\n");
                    carga_termica = 1;
                    led_blink(255, 0, 0);
                    break;
                case BTN_2:
                    printf("Botao 6\n");
                    carga_termica = -1;
                    led_blink(0, 0, 255);
                    break;
                case BTN_A:
                    printf("Botoes 7+6\n");
                    carga_termica = 0;
                    led_blink(128, 0, 128);
                    break;
                default:
                    carga_termica = 0;
                    break;
            }
        }
        anterior = atual;
        vTaskDelay(pdMS_TO_TICKS(poll));
    }
}

void curtain_init(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << BTN1_GPIO) | (1ULL << BTN2_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));

    led_strip_config_t led_cfg = {
        .strip_gpio_num = led_gpio,
        .max_leds = 1,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .resolution_hz = 10000000,
    };
    led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &strip);
    led_strip_clear(strip);

    if (xTaskCreate(curtain_task, "curtain", 6144, NULL, 1, NULL) != pdPASS) {
        printf("Falha ao criar a cortina\n");
    }
}