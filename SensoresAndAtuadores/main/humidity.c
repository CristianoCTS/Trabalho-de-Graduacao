#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_rom_sys.h"

#include "humidity.h"

static const char *TAG = "HUMID";

// ---------------------------------------------------------------------------
// HARDWARE:
//   DHT22 (AM2302) alimentado em 5V, com pull-up de 10k do DATA para 3.3V.
//   Isso so e seguro porque a linha e dreno aberto: o sensor apenas puxa para
//   GND e o pull-up define o nivel alto em 3.3V. Se ao medir a linha em repouso
//   voce encontrar 5V, troque a alimentacao do sensor para 3.3V.
//
//   O DHT22 exige no minimo 2 segundos entre leituras.
// ---------------------------------------------------------------------------
#define DHT22_GPIO_NUM          GPIO_NUM_1

#define DHT22_INTERVALO_MS      2500   // respeita o minimo de 2s do sensor
#define DHT22_START_LOW_US      1200   // pulso inicial do mestre (min. 800us)
#define DHT22_TIMEOUT_US        200    // limite de espera por transicao de nivel

static portMUX_TYPE s_dht_mux = portMUX_INITIALIZER_UNLOCKED;

static dht22_reading_t s_ultima_leitura = { 0.0f, 0.0f };
static bool s_leitura_valida = false;

static void dht_init_pin(void)
{
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << DHT22_GPIO_NUM),
        .mode = GPIO_MODE_INPUT_OUTPUT_OD,   // dreno aberto, le e escreve no mesmo pino
        .pull_up_en = GPIO_PULLUP_ENABLE,    // reforca o pull-up externo de 10k
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&cfg));
    gpio_set_level(DHT22_GPIO_NUM, 1);       // linha liberada em repouso
}

// Espera a linha atingir o nivel desejado e devolve quantos microssegundos
// levou. Retorna -1 se estourar o timeout.
static int dht_wait_level(int nivel)
{
    int us = 0;
    while (gpio_get_level(DHT22_GPIO_NUM) != nivel) {
        if (++us > DHT22_TIMEOUT_US) {
            return -1;
        }
        esp_rom_delay_us(1);
    }
    return us;
}

// Le um quadro completo de 40 bits.
// Precisa rodar inteiro em secao critica: o protocolo do DHT22 codifica os bits
// pela largura do pulso alto, entao qualquer preempcao no meio corrompe a leitura.
static bool dht_read_frame(uint8_t dados[5])
{
    // Pulso de inicio: o mestre segura a linha em nivel baixo.
    // Fica fora da secao critica porque um atraso maior que o minimo nao atrapalha.
    gpio_set_level(DHT22_GPIO_NUM, 0);
    esp_rom_delay_us(DHT22_START_LOW_US);

    portENTER_CRITICAL(&s_dht_mux);

    gpio_set_level(DHT22_GPIO_NUM, 1);   // solta a linha, o sensor assume o barramento

    // Resposta do sensor: ~80us em nivel baixo, depois ~80us em nivel alto.
    if (dht_wait_level(0) < 0 || dht_wait_level(1) < 0 || dht_wait_level(0) < 0) {
        portEXIT_CRITICAL(&s_dht_mux);
        return false;
    }

    memset(dados, 0, 5);

    // 40 bits: cada um comeca com ~50us em nivel baixo, e a duracao do nivel
    // alto seguinte define o valor (~26us para 0, ~70us para 1).
    for (int i = 0; i < 40; i++) {
        if (dht_wait_level(1) < 0) {
            portEXIT_CRITICAL(&s_dht_mux);
            return false;
        }

        int largura = dht_wait_level(0);
        if (largura < 0) {
            portEXIT_CRITICAL(&s_dht_mux);
            return false;
        }

        dados[i / 8] <<= 1;
        if (largura > 45) {              // limiar entre 26us e 70us
            dados[i / 8] |= 1;
        }
    }

    portEXIT_CRITICAL(&s_dht_mux);
    return true;
}

// Converte o quadro bruto e valida o checksum.
static bool dht_parse(const uint8_t dados[5], dht22_reading_t *out)
{
    // O byte 4 e a soma dos quatro primeiros, truncada em 8 bits.
    uint8_t soma = dados[0] + dados[1] + dados[2] + dados[3];
    if (soma != dados[4]) {
        ESP_LOGW(TAG, "Checksum invalido, leitura descartada");
        return false;
    }

    uint16_t umid_bruta = ((uint16_t)dados[0] << 8) | dados[1];
    uint16_t temp_bruta = ((uint16_t)dados[2] << 8) | dados[3];

    out->humidity = umid_bruta / 10.0f;

    // O bit mais significativo da temperatura indica sinal negativo.
    if (temp_bruta & 0x8000) {
        out->temperature = -((temp_bruta & 0x7FFF) / 10.0f);
    } else {
        out->temperature = temp_bruta / 10.0f;
    }

    return true;
}

static void TarefaUmidade(void *pvParameters)
{
    uint8_t dados[5];
    dht22_reading_t leitura;

    // O DHT22 precisa de ~1s apos energizar antes de responder.
    vTaskDelay(pdMS_TO_TICKS(1500));

    while (1) {
        if (dht_read_frame(dados) && dht_parse(dados, &leitura)) {
            s_ultima_leitura = leitura;
            s_leitura_valida = true;
            ESP_LOGI(TAG, "Temperatura: %.1f C | Umidade: %.1f %%",
                     leitura.temperature, leitura.humidity);
        } else {
            ESP_LOGW(TAG, "Sensor nao respondeu (verifique pull-up e ligacao)");
        }

        vTaskDelay(pdMS_TO_TICKS(DHT22_INTERVALO_MS));
    }
}

bool humidity_get(dht22_reading_t *out)
{
    if (!s_leitura_valida || out == NULL) {
        return false;
    }
    *out = s_ultima_leitura;
    return true;
}

void humidity_init(void)
{
    dht_init_pin();

    BaseType_t r = xTaskCreate(TarefaUmidade, "TarefaUmid", 3072, NULL, 3, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao criar a Tarefa de Umidade!");
    } else {
        ESP_LOGI(TAG, "[RTOS] Tarefa de Umidade iniciada com sucesso (GPIO %d).",
                 DHT22_GPIO_NUM);
    }
}