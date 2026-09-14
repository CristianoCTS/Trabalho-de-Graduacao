#include <string.h>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

#include "esp_event.h"
#include "esp_log.h"
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "emiter.h"

static const char *TAG = "ATUADOR_IR";
static QueueHandle_t filaComandosAr;
static rmt_channel_handle_t s_ir_tx_channel;
static rmt_encoder_handle_t s_ir_copy_encoder;


#define IR_TX_GPIO_NUM      7
#define IR_RESOLUTION_HZ    1000000
#define IR_CARRIER_FREQ_HZ  38000
#define IR_CARRIER_DUTY     0.33f

#define IR_BTN_GPIO_NUM          GPIO_NUM_0
#define IR_BTN_DEBOUNCE_MS       50
#define IR_BTN_POLL_INTERVAL_MS  20
#define IR_BTN_CICLO_LEN         4

typedef enum {
    CMD_ON_OFF = 0,
    CMD_MODE,
    CMD_TEMP_BAIXO,
    CMD_TEMP_ALTO
} ir_comando_t;

static const char *ir_comando_nome(ir_comando_t cmd)
{
    switch (cmd) {
        case CMD_ON_OFF:     return "ON/OFF";
        case CMD_MODE:       return "MODE";
        case CMD_TEMP_BAIXO: return "TEMP BAIXO";
        case CMD_TEMP_ALTO:  return "TEMP ALTO";
        default:             return "DESCONHECIDO";
    }
}

static const int s_cicloComandos[IR_BTN_CICLO_LEN] = { CMD_ON_OFF, CMD_MODE, CMD_TEMP_BAIXO, CMD_TEMP_ALTO };

// static const uint16_t rawOnOff[198] = {6147, 7379, 558, 1610, 558, 1610, 558, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1611, 557, 1612, 558, 520, 558, 521, 558, 521, 557, 521, 558, 521, 557, 521, 558, 521, 557, 520, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1611, 557, 1611, 557, 1612, 558, 521, 558, 520, 558, 521, 557, 522, 557, 521, 558, 521, 557, 522, 557, 520, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1612, 557, 521, 558, 521, 557, 521, 558, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 522, 557, 520, 557, 1612, 556, 1612, 557, 1611, 557, 1612, 556, 1613, 557, 520, 557, 1611, 557, 1613, 557, 521, 557, 522, 557, 522, 556, 522, 557, 521, 556, 1613, 556, 522, 556, 1612, 557, 521, 556, 1613, 556, 522, 556, 1635, 533, 1613, 556, 522, 556, 1637, 533, 522, 555, 1613, 556, 522, 556, 1636, 533, 523, 555, 546, 533, 545, 533, 1637, 532, 522, 556, 1636, 533, 544, 534, 1636, 533, 546, 532, 545, 533, 1636, 533, 545, 532, 1637, 533, 544, 533, 1637, 532, 546, 532, 1658, 533, 1637, 533, 7410, 533, 0};
// static const uint16_t rawMode[198] = {6148, 7378, 559, 1609, 559, 1610, 558, 1610, 558, 1610, 559, 1609, 559, 1610, 558, 1609, 559, 1611, 558, 520, 558, 520, 559, 520, 559, 519, 559, 521, 558, 520, 558, 521, 558, 519, 585, 1607, 558, 1610, 558, 1610, 558, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 521, 558, 520, 558, 521, 559, 520, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 557, 1612, 557, 522, 557, 521, 558, 521, 558, 521, 557, 522, 557, 521, 557, 521, 558, 1611, 557, 521, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 521, 557, 1613, 557, 520, 557, 1612, 557, 521, 557, 1612, 557, 520, 557, 1612, 557, 1612, 557, 521, 557, 521, 558, 1611, 557, 1612, 557, 520, 557, 1611, 557, 1613, 556, 521, 557, 1612, 556, 1612, 557, 522, 557, 521, 556, 1613, 557, 522, 556, 521, 556, 1614, 555, 546, 533, 545, 533, 1613, 556, 545, 533, 1636, 533, 544, 534, 1636, 532, 546, 533, 545, 533, 1636, 533, 545, 533, 1636, 533, 544, 534, 1636, 533, 544, 533, 1636, 533, 1636, 533, 7411, 533, 0};
// static const uint16_t rawTempBaixo[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
// static const uint16_t rawTempAlto[198] = {6122, 7377, 559, 1609, 558, 1611, 558, 1609, 559, 1609, 559, 1610, 559, 1610, 558, 1610, 558, 1611, 558, 521, 557, 521, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1610, 559, 1610, 558, 1610, 558, 1611, 558, 521, 557, 521, 558, 521, 558, 521, 557, 521, 558, 521, 557, 521, 558, 520, 558, 1611, 558, 520, 558, 1610, 558, 1610, 557, 1612, 557, 1611, 557, 1611, 557, 1612, 557, 521, 557, 1612, 557, 521, 558, 521, 558, 521, 557, 521, 557, 521, 558, 520, 557, 1612, 557, 521, 557, 1612, 557, 520, 558, 1611, 557, 1611, 557, 1611, 557, 1613, 557, 520, 558, 1611, 558, 520, 558, 1611, 558, 521, 557, 521, 557, 522, 557, 521, 557, 521, 557, 1613, 557, 520, 557, 1612, 557, 1612, 557, 521, 557, 520, 558, 1611, 557, 1612, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 557, 1612, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1613, 557, 520, 558, 1611, 558, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 557, 520, 557, 1613, 556, 521, 557, 1611, 557, 1613, 557, 7387, 556, 0};
static const uint16_t rawOnOff[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawMode[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawTempBaixo[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawTempAlto[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};

#define RAW_LEN(arr) (sizeof(arr) / sizeof((arr)[0]))


static void ir_tx_init(void)
{
    rmt_tx_channel_config_t tx_chan_config = {
        .gpio_num = IR_TX_GPIO_NUM,
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .trans_queue_depth = 4,
    };
    ESP_ERROR_CHECK(rmt_new_tx_channel(&tx_chan_config, &s_ir_tx_channel));

    rmt_carrier_config_t carrier_cfg = {
        .frequency_hz = IR_CARRIER_FREQ_HZ,
        .duty_cycle = IR_CARRIER_DUTY,
    };
    ESP_ERROR_CHECK(rmt_apply_carrier(s_ir_tx_channel, &carrier_cfg));

    rmt_copy_encoder_config_t copy_encoder_config = {};
    ESP_ERROR_CHECK(rmt_new_copy_encoder(&copy_encoder_config, &s_ir_copy_encoder));

    ESP_ERROR_CHECK(rmt_enable(s_ir_tx_channel));
}

static void ir_send_raw(const uint16_t *raw, size_t len)
{
    size_t num_symbols = (len + 1) / 2;
    rmt_symbol_word_t *symbols = calloc(num_symbols, sizeof(rmt_symbol_word_t));
    if (symbols == NULL) {
        ESP_LOGE(TAG, "Sem memoria para montar simbolos RMT");
        return;
    }

    for (size_t i = 0; i < num_symbols; i++) {
        size_t idx = i * 2;
        symbols[i].level0 = 1;
        symbols[i].duration0 = raw[idx];
        symbols[i].level1 = 0;
        symbols[i].duration1 = (idx + 1 < len) ? raw[idx + 1] : 0;
    }

    rmt_transmit_config_t transmit_config = {
        .loop_count = 0,
    };
    ESP_ERROR_CHECK(rmt_transmit(s_ir_tx_channel, s_ir_copy_encoder,
                                  symbols, num_symbols * sizeof(rmt_symbol_word_t),
                                  &transmit_config));
    ESP_ERROR_CHECK(rmt_tx_wait_all_done(s_ir_tx_channel, portMAX_DELAY));

    free(symbols);
}

static void ir_btn_init(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << IR_BTN_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
}

static void TarefaBotaoIR(void *pvParameters)
{
    int estadoAnterior = 1;
    int contadorClique = 0;

    while (1) {
        int estadoAtual = gpio_get_level(IR_BTN_GPIO_NUM);
        if (estadoAnterior == 1 && estadoAtual == 0) {
            vTaskDelay(pdMS_TO_TICKS(IR_BTN_DEBOUNCE_MS));
            if (gpio_get_level(IR_BTN_GPIO_NUM) == 0) {
                if (contadorClique < IR_BTN_CICLO_LEN) {
                    int comando = s_cicloComandos[contadorClique];
                    ESP_LOGI(TAG, "[BOTAO] Clique #%d -> comando %s", contadorClique + 1, ir_comando_nome((ir_comando_t)comando));
                    xQueueSend(filaComandosAr, &comando, portMAX_DELAY);
                    contadorClique++;
                } else {
                    ESP_LOGI(TAG, "[BOTAO] Clique #%d -> reset da contagem", contadorClique + 1);
                    contadorClique = 0;
                }
                estadoAtual = 0;
            }
        }

        estadoAnterior = estadoAtual;
        vTaskDelay(pdMS_TO_TICKS(IR_BTN_POLL_INTERVAL_MS));
    }
}

static void TarefaDisparoIR(void *pvParameters)
{
    int comandoAlvo;

    while (1) {
        if (xQueueReceive(filaComandosAr, &comandoAlvo, portMAX_DELAY) == pdTRUE) {
            ESP_LOGI(TAG, "[RTOS] Processando disparo para: %s", ir_comando_nome((ir_comando_t)comandoAlvo));

            for (int tentativa = 1; tentativa <= 2; tentativa++) {
                ESP_LOGI(TAG, "[RTOS] Disparo IR #%d", tentativa);

                switch ((ir_comando_t)comandoAlvo) {
                    case CMD_ON_OFF:     ir_send_raw(rawOnOff, RAW_LEN(rawOnOff)); break;
                    case CMD_MODE:       ir_send_raw(rawMode, RAW_LEN(rawMode)); break;
                    case CMD_TEMP_BAIXO: ir_send_raw(rawTempBaixo, RAW_LEN(rawTempBaixo)); break;
                    case CMD_TEMP_ALTO:  ir_send_raw(rawTempAlto, RAW_LEN(rawTempAlto)); break;
                    default: break;
                }

                if (tentativa < 2) {
                    vTaskDelay(pdMS_TO_TICKS(1500));
                }
            }
        }
    }
}


void emiter_init(void)
{
    ir_tx_init();
    ir_btn_init();

    filaComandosAr = xQueueCreate(5, sizeof(int));
    if (filaComandosAr == NULL) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao alocar memoria para a Fila!");
        esp_restart();
    }

    TaskHandle_t taskDisparoHandle;
    BaseType_t resultadoTask = xTaskCreate(
        TarefaDisparoIR,
        "TarefaIR",
        8192,
        NULL,
        2,
        &taskDisparoHandle
    );

    if (resultadoTask != pdPASS) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao criar a Tarefa de Disparo IR!");
        esp_restart();
    } else {
        ESP_LOGI(TAG, "[RTOS] Tarefa IR iniciada com sucesso.");
    }

    TaskHandle_t taskBotaoHandle;
    BaseType_t resultadoTaskBotao = xTaskCreate(
        TarefaBotaoIR,
        "TarefaBotaoIR",
        2048,
        NULL,
        1,
        &taskBotaoHandle
    );

    if (resultadoTaskBotao != pdPASS) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao criar a Tarefa do Botao!");
        esp_restart();
    } else {
        ESP_LOGI(TAG, "[RTOS] Tarefa do Botao iniciada com sucesso.");
    }
}