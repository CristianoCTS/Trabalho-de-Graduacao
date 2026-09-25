#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "humidity.h"

#define DHT22_GPIO GPIO_NUM_1

#define start_up 1200
#define timeout 200

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static dht22_reading_t old_data = { 0.0f, 0.0f };
static dht22_reading_t data = { 0.0f, 0.0f };
static char error_code[64];

static void pin_config(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DHT22_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(DHT22_GPIO, 1);
}

static int bit_length(int nivel)
{
    int us = 0;
    while (gpio_get_level(DHT22_GPIO) != nivel) {
        if (++us > timeout) {
            snprintf(error_code, sizeof(error_code), "timeout no bit_length");
            return -1;
        }
        esp_rom_delay_us(1);
    }
    return us;
}

static bool h_read_data(uint8_t dados[5])
{
    memset(dados, 0, 5);
    gpio_set_level(DHT22_GPIO, 0);
    esp_rom_delay_us(start_up);

    portENTER_CRITICAL(&mux);

    gpio_set_level(DHT22_GPIO, 1);
    if (bit_length(0) < 0 || bit_length(1) < 0 || bit_length(0) < 0) {
        portEXIT_CRITICAL(&mux);
        return false;
    }

    for (int i = 0; i < 40; i++) {
        if (bit_length(1) < 0) {
            portEXIT_CRITICAL(&mux);
            return false;
        }

        int largura = bit_length(0);
        if (largura < 0) {
            portEXIT_CRITICAL(&mux);
            return false;
        }

        dados[i / 8] <<= 1;
        if (largura > 45) {
            dados[i / 8] |= 1;
        }
    }
    portEXIT_CRITICAL(&mux);
    return true;
}

static bool confirm_Sdigit(const uint8_t dados[5], dht22_reading_t *out)
{
    uint8_t soma = dados[0] + dados[1] + dados[2] + dados[3];
    if (soma != dados[4]) {
        snprintf(error_code, sizeof(error_code), "Checksum invalido");
        return false;
    }

    uint16_t umid_bruta = ((uint16_t)dados[0] << 8) | dados[1];
    uint16_t temp_bruta = ((uint16_t)dados[2] << 8) | dados[3];

    out->humidity = umid_bruta / 10.0f;
    if (temp_bruta & 0x8000) {
        out->temperature = -((temp_bruta & 0x7FFF) / 10.0f);
    } else {
        out->temperature = temp_bruta / 10.0f;
    }
    return true;
}

bool DHT22_read(dht22_reading_t *out)
{
    uint8_t dados[5];
    if (h_read_data(dados) && confirm_Sdigit(dados, &data)) {
        old_data = data;
        *out = data;
        printf("DHT22: %.1f C | %.1f %% de umidade\n", data.temperature, data.humidity);
        return true;
    } else {
        printf("Sensor nao respondeu: %s\n", error_code);
        return false;
    }
}

void DHT22_init(void)
{
    pin_config();
}