#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "led_strip.h"
#include "esp_err.h"
#include "coms.h"
#include "PIRcurtain.h"


#define sensorA GPIO_NUM_0
#define sensorB GPIO_NUM_1
#define aquecimento 60
#define janela 1500
#define bloqueio 1000
#define LED_GPIO 8
#define piscadas 3
#define blink 400

typedef struct {
    uint8_t sensor;
    int64_t t_us;
} evento_t;

static QueueHandle_t fila;
static char error_code[64];
bool ambos = false;
bool estado_led = false;
float direcao = 0.0f;
static led_strip_handle_t led = NULL;

static void led_set(bool ligado, int r, int g, int b)
{
    if (led == NULL) {
        return;
    }
    if (ligado) {
        led_strip_set_pixel(led, 0, r, g, b);
        led_strip_refresh(led);
    } else {
        led_strip_clear(led);
    }
}

static void led_blink(int r, int g, int b)
{
    led_set(true, r, g, b);
    vTaskDelay(pdMS_TO_TICKS(blink));
    led_set(false, 0, 0, 0);
}

static void led_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &led);
    if (err != ESP_OK) {
        snprintf(error_code, sizeof(error_code), "LED init: %s", esp_err_to_name(err));
        printf("curtain: %s\n", error_code);
        led = NULL;
        return;
    }
    led_strip_clear(led);
}

static void IRAM_ATTR isr_sensor(void *arg)
{
    evento_t ev = {
        .sensor = (uint8_t)(uintptr_t)arg,
        .t_us = esp_timer_get_time(),
    };
    BaseType_t acordou = pdFALSE;
    xQueueSendFromISR(fila, &ev, &acordou);
    portYIELD_FROM_ISR(acordou);
}

static void monitora_psg(void *arg)
{
    vTaskDelay(pdMS_TO_TICKS(500)); 
    printf("Aquecendo sensores (%d s)\n", aquecimento);
    estado_led = false;
    for (int i = 0; i < aquecimento ; i++) {
        estado_led = !estado_led;
        led_set(estado_led, 60, 20, 0);
        printf("Espere: %d s\n", (aquecimento -i));
        vTaskDelay(pdMS_TO_TICKS(1000));
        xQueueReset(fila);
        printf("\033[1A\033[2K\r");
    }
    led_set(false, 0, 0, 0);
    printf("Monitorando\n");

    evento_t primeiro, segundo;

    while (1) {
        ambos = false;
        if (xQueueReceive(fila, &primeiro, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (xQueueReceive(fila, &segundo, pdMS_TO_TICKS(janela)) == pdTRUE &&
            segundo.sensor != primeiro.sensor) {
            ambos = true;
        }
        direcao = 0.0f;

        if (ambos) {
            int32_t delta_ms = (int32_t)((segundo.t_us - primeiro.t_us) / 1000);
            if (primeiro.sensor == 0) {
                direcao = 1.0f;
                printf("ENTRADA (A->B)(intervalo %ld ms)\n", (long)delta_ms);
                intracom_send(&direcao, -1);
                led_blink(60, 0, 0);
            } else {
                direcao = -1.0f;
                printf("SAIDA (B->A)(intervalo %ld ms)\n", (long)delta_ms);
                intracom_send(&direcao, -1);
                led_blink(0, 0, 60);
            }
        } else {
            printf("Passagem detectada só no sensor %c\n", primeiro.sensor == 0 ? 'A' : 'B');
            led_blink(0, 60, 0);
        }
        vTaskDelay(pdMS_TO_TICKS(bloqueio));
        xQueueReset(fila);
    }
}

void curtain_init(void)
{
    led_init();
    coms_init(false);
    
    fila = xQueueCreate(8, sizeof(evento_t));

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << sensorA) | (1ULL << sensorB),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_POSEDGE,
    };
    gpio_config(&cfg);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(sensorA, isr_sensor, (void *)0);
    gpio_isr_handler_add(sensorB, isr_sensor, (void *)1);

    xTaskCreate(monitora_psg, "passagem", 4096, NULL, 5, NULL);
}