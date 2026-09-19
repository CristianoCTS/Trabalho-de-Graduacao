#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "temperature.h"

static const char *TAG = "TEMP";

#define DS18B20_GPIO_NUM        GPIO_NUM_5

#define DS18B20_INTERVALO_MS    2000   // periodo entre leituras
#define DS18B20_CONV_WAIT_MS    800    // conversao de 12 bits leva ate 750ms

// Comandos do DS18B20
#define CMD_SKIP_ROM            0xCC
#define CMD_CONVERT_T           0x44
#define CMD_READ_SCRATCHPAD     0xBE

static portMUX_TYPE s_ow_mux = portMUX_INITIALIZER_UNLOCKED;

static float s_ultima_temp = 0.0f;
static bool  s_temp_valida = false;

static void ow_init_pin(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DS18B20_GPIO_NUM),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,   // dreno aberto, permite ler e escrever
        .pull_up_en = GPIO_PULLUP_ENABLE,    // reforca o pull-up externo de 4.7k
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(DS18B20_GPIO_NUM, 1);     // linha liberada em repouso
}

// Pulso de reset. Retorna true se algum sensor respondeu com o pulso de presenca.
static bool ow_reset(void)
{
    bool presenca;

    // Fase baixa de 480us: nao precisa de secao critica, um atraso maior que o
    // minimo nao quebra o protocolo.
    gpio_set_level(DS18B20_GPIO_NUM, 0);
    esp_rom_delay_us(480);

    // A janela de amostragem da presenca e curta e precisa de tempo exato.
    portENTER_CRITICAL(&s_ow_mux);
    gpio_set_level(DS18B20_GPIO_NUM, 1);
    esp_rom_delay_us(70);
    presenca = (gpio_get_level(DS18B20_GPIO_NUM) == 0);
    portEXIT_CRITICAL(&s_ow_mux);

    esp_rom_delay_us(410);   // completa o slot de reset
    return presenca;
}

static void ow_write_bit(int bit)
{
    portENTER_CRITICAL(&s_ow_mux);
    if (bit) {
        gpio_set_level(DS18B20_GPIO_NUM, 0);
        esp_rom_delay_us(6);
        gpio_set_level(DS18B20_GPIO_NUM, 1);
        esp_rom_delay_us(64);
    } else {
        gpio_set_level(DS18B20_GPIO_NUM, 0);
        esp_rom_delay_us(60);
        gpio_set_level(DS18B20_GPIO_NUM, 1);
        esp_rom_delay_us(10);
    }
    portEXIT_CRITICAL(&s_ow_mux);
}

static int ow_read_bit(void)
{
    int bit;

    portENTER_CRITICAL(&s_ow_mux);
    gpio_set_level(DS18B20_GPIO_NUM, 0);
    esp_rom_delay_us(6);
    gpio_set_level(DS18B20_GPIO_NUM, 1);
    esp_rom_delay_us(9);                 // o sensor apresenta o bit nessa janela
    bit = gpio_get_level(DS18B20_GPIO_NUM);
    portEXIT_CRITICAL(&s_ow_mux);

    esp_rom_delay_us(55);                // completa o slot de leitura
    return bit;
}

static void ow_write_byte(uint8_t valor)
{
    for (int i = 0; i < 8; i++) {
        ow_write_bit(valor & 0x01);      // 1-Wire transmite o bit menos significativo primeiro
        valor >>= 1;
    }
}

static uint8_t ow_read_byte(void)
{
    uint8_t valor = 0;
    for (int i = 0; i < 8; i++) {
        valor >>= 1;
        if (ow_read_bit()) {
            valor |= 0x80;
        }
    }
    return valor;
}

// CRC-8 da Maxim (polinomio X^8 + X^5 + X^4 + 1), usado para validar o scratchpad.
static uint8_t ow_crc8(const uint8_t *dados, size_t len)
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

// ============================= CAMADA DS18B20 ==============================

// Dispara a conversao de temperatura. Nao espera o resultado.
static bool ds18b20_start_conversion(void)
{
    if (!ow_reset()) {
        return false;
    }
    ow_write_byte(CMD_SKIP_ROM);      // um unico sensor no barramento
    ow_write_byte(CMD_CONVERT_T);
    return true;
}

// Le o scratchpad e converte para graus Celsius.
static bool ds18b20_read_temp(float *out_celsius)
{
    uint8_t scratchpad[9];

    if (!ow_reset()) {
        return false;
    }
    ow_write_byte(CMD_SKIP_ROM);
    ow_write_byte(CMD_READ_SCRATCHPAD);

    for (int i = 0; i < 9; i++) {
        scratchpad[i] = ow_read_byte();
    }

    // O byte 8 e o CRC dos 8 primeiros; se nao bater, a leitura veio corrompida.
    if (ow_crc8(scratchpad, 8) != scratchpad[8]) {
        ESP_LOGW(TAG, "CRC invalido, leitura descartada");
        return false;
    }

    // Um scratchpad todo 0xFF indica sensor ausente ou linha em curto.
    if (scratchpad[0] == 0xFF && scratchpad[1] == 0xFF) {
        ESP_LOGW(TAG, "Scratchpad vazio, sensor nao respondeu");
        return false;
    }

    int16_t bruto = (int16_t)((scratchpad[1] << 8) | scratchpad[0]);
    *out_celsius = bruto / 16.0f;   // resolucao padrao de 12 bits: 1/16 de grau
    return true;
}

// ============================== TAREFA RTOS ================================

static void TarefaTemperatura(void *pvParameters)
{
    float temp;

    while (1) {
        if (!ds18b20_start_conversion()) {
            ESP_LOGW(TAG, "Sensor nao respondeu ao reset (verifique pull-up e ligacao)");
            vTaskDelay(pdMS_TO_TICKS(DS18B20_INTERVALO_MS));
            continue;
        }

        // Espera a conversao terminar. vTaskDelay libera a CPU para as outras
        // tarefas, diferente de um busy-wait.
        vTaskDelay(pdMS_TO_TICKS(DS18B20_CONV_WAIT_MS));

        if (ds18b20_read_temp(&temp)) {
            s_ultima_temp = temp;
            s_temp_valida = true;
            ESP_LOGI(TAG, "Temperatura: %.2f C", temp);
        }

        vTaskDelay(pdMS_TO_TICKS(DS18B20_INTERVALO_MS));
    }
}

bool temperature_get(float *out_celsius)
{
    if (!s_temp_valida || out_celsius == NULL) {
        return false;
    }
    *out_celsius = s_ultima_temp;
    return true;
}

void temperature_init(void)
{
    ow_init_pin();

    // Um reset inicial confirma se o sensor esta no barramento.
    if (ow_reset()) {
        ESP_LOGI(TAG, "DS18B20 detectado no GPIO %d", DS18B20_GPIO_NUM);
    } else {
        ESP_LOGW(TAG, "Nenhum DS18B20 respondeu no GPIO %d", DS18B20_GPIO_NUM);
        ESP_LOGW(TAG, "Confira: alimentacao em 3.3V, pull-up de 4.7k para 3.3V e GND comum");
    }

    BaseType_t r = xTaskCreate(TarefaTemperatura, "TarefaTemp", 3072, NULL, 3, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao criar a Tarefa de Temperatura!");
    } else {
        ESP_LOGI(TAG, "[RTOS] Tarefa de Temperatura iniciada com sucesso.");
    }
}