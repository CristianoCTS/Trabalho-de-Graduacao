#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "temperature.h"

#define DS18B20_GPIO GPIO_NUM_0
#define wait_time 800
#define skip_rom 0xCC
#define convert_temp 0x44
#define read_scratchpad 0xBE

static portMUX_TYPE mutex = portMUX_INITIALIZER_UNLOCKED;

static float old_temp = 0.0f;
static float temp = 0.0f;
static char error_code[64];

static void pin_config(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(DS18B20_GPIO, 1);
}

static bool t_wellnesscheck(void)
{
    bool presenca;
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(480);
    portENTER_CRITICAL(&mutex);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(70);
    presenca = (gpio_get_level(DS18B20_GPIO) == 0);
    portEXIT_CRITICAL(&mutex);
    esp_rom_delay_us(410);
    return presenca;
}

static void t_write_bit(int bit)
{
    portENTER_CRITICAL(&mutex);
    if (bit) {
        gpio_set_level(DS18B20_GPIO, 0);
        esp_rom_delay_us(6);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(64);
    } else {
        gpio_set_level(DS18B20_GPIO, 0);
        esp_rom_delay_us(60);
        gpio_set_level(DS18B20_GPIO, 1);
        esp_rom_delay_us(10);
    }
    portEXIT_CRITICAL(&mutex);
}

static int t_read_bit(void)
{
    int bit;
    portENTER_CRITICAL(&mutex);
    gpio_set_level(DS18B20_GPIO, 0);
    esp_rom_delay_us(6);
    gpio_set_level(DS18B20_GPIO, 1);
    esp_rom_delay_us(9);
    bit = gpio_get_level(DS18B20_GPIO);
    portEXIT_CRITICAL(&mutex);
    esp_rom_delay_us(55);
    return bit;
}

static void t_write_byte(uint8_t valor)
{
    for (int i = 0; i < 8; i++) {
        t_write_bit(valor & 0x01);
        valor >>= 1;
    }
}

static uint8_t t_read_byte(void)
{
    uint8_t valor = 0;
    for (int i = 0; i < 8; i++) {
        valor >>= 1;
        if (t_read_bit()) {
            valor |= 0x80;
        }
    }
    return valor;
}

static uint8_t confirm_Sdigit(const uint8_t *dados, size_t len)
{
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t byte = dados[i];
        for (int b = 0; b < 8; b++) {
            uint8_t mistura = (crc ^ byte) & 0x01;
            crc >>= 1;
            if (mistura) {
                crc ^= 0x8C;
            }
            byte >>= 1;
        }
    }
    return crc;
}

static bool t_wake_up(void)
{
    if (!t_wellnesscheck()) {
        return false;
    }
    t_write_byte(skip_rom);
    t_write_byte(convert_temp);
    return true;
}

static bool t_read_sensor(float *out_celsius)
{
    uint8_t scratchpad[9];
    if (!t_wellnesscheck()) {
        return false;
    }
    t_write_byte(skip_rom);
    t_write_byte(read_scratchpad);

    for (int i = 0; i < 9; i++) {
        scratchpad[i] = t_read_byte();
    }
    if (confirm_Sdigit(scratchpad, 8) != scratchpad[8]) {
        snprintf(error_code, sizeof(error_code), "CRC invalido");
        return false;
    }

    if (scratchpad[0] == 0xFF && scratchpad[1] == 0xFF) {
        snprintf(error_code, sizeof(error_code), "Scratchpad vazio");
        return false;
    }

    int16_t bruto = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    *out_celsius = bruto / 16.0f;
    return true;
}

bool DS18B20_read(float *out_celsius)
{
    if (!t_wake_up()) {
        snprintf(error_code, sizeof(error_code), "Sensor nao respondeu");
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(wait_time));
    if (t_read_sensor(&temp)) {
        old_temp = temp;
        *out_celsius = temp;
        printf("DS18B20: %.2f C\n", temp);
        return true;
    }
    else {
        printf("Falha ao ler temperatura: %s", error_code);
        return false;
    }
}

void DS18B20_init(void)
{
    pin_config();
    if (t_wellnesscheck()) {
        printf("DS18B20 no GPIO %d", DS18B20_GPIO);
    } else {
        printf("DS18B20 não no GPIO %d", DS18B20_GPIO);
    }
}