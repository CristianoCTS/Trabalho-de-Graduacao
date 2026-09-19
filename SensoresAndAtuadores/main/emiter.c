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
#include "led_strip.h"
#include "emiter.h"

static const char *TAG = "ATUADOR_IR";
static QueueHandle_t filaComandosAr;
static rmt_channel_handle_t s_ir_tx_channel;
static rmt_encoder_handle_t s_ir_copy_encoder;


// --------- Pino e parametros do LED IR (substitui IRsend) ---------
#define IR_TX_GPIO_NUM      7
#define IR_RESOLUTION_HZ    1000000   // 1 tick = 1us, mesma unidade do IRremoteESP8266
#define IR_CARRIER_FREQ_HZ  38000
#define IR_CARRIER_DUTY     0.33f

// --------- Pino e parametros do botao (substitui o gatilho por MQTT) ---------
#define IR_BTN_GPIO_NUM          GPIO_NUM_0
#define IR_BTN_DEBOUNCE_MS       50     // tempo de estabilizacao apos detectar a borda
#define IR_BTN_POLL_INTERVAL_MS  20     // intervalo de leitura do pino
#define IR_BTN_CICLO_LEN         4      // quantidade de comandos no ciclo (antes do reset)

// --------- LED RGB embutido na placa (WS2812 no ESP32-C6-DevKit) ---------
#define RGB_LED_GPIO         8
#define RGB_LED_BLINK_MS     80    // duracao do pulso de cada piscada
static led_strip_handle_t s_rgb_led;

// Comandos reais do controle (capturados em CodigosIR.txt)
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

// Ajuste aqui os comandos disparados no 1o, 2o, 3o e 4o clique.
// No 5o clique a contagem apenas reseta, sem enviar nada.
static const int s_cicloComandos[IR_BTN_CICLO_LEN] = { CMD_ON_OFF, CMD_MODE, CMD_TEMP_BAIXO, CMD_TEMP_ALTO };

// ================= CODIGOS REAIS CAPTURADOS PELO RECEPTOR (CodigosIR.txt) =
// Cada botao foi capturado varias vezes para checar consistencia; foi usada
// a 1a captura de cada grupo como referencia (as demais ficaram praticamente
// identicas, a menos de pequenas variacoes de ruido do TSOP).
static const uint16_t rawOnOff[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawMode[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawTempBaixo[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
static const uint16_t rawTempAlto[198] = {6147, 7378, 559, 1610, 559, 1610, 558, 1610, 558, 1610, 559, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 520, 559, 520, 558, 520, 559, 520, 559, 520, 558, 521, 557, 521, 558, 520, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 558, 1610, 558, 1610, 558, 1610, 558, 1611, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 520, 558, 521, 558, 521, 558, 520, 558, 520, 558, 1611, 557, 1611, 558, 1610, 558, 1611, 557, 1611, 557, 1611, 557, 1611, 558, 1611, 558, 521, 557, 521, 558, 521, 557, 522, 557, 521, 557, 521, 557, 1612, 557, 521, 557, 1612, 556, 521, 557, 1611, 557, 1611, 558, 1611, 557, 1612, 557, 520, 558, 1612, 557, 521, 557, 1612, 557, 522, 556, 522, 557, 522, 557, 520, 557, 1613, 557, 521, 557, 521, 557, 1611, 558, 1611, 557, 522, 557, 520, 557, 1613, 556, 521, 557, 1611, 558, 1612, 557, 521, 557, 521, 557, 1612, 556, 1612, 558, 522, 556, 520, 557, 1612, 557, 521, 557, 1613, 556, 521, 556, 1613, 557, 522, 556, 521, 557, 1613, 556, 521, 557, 1614, 555, 545, 533, 1613, 556, 544, 533, 1636, 532, 1615, 555, 7388, 556, 0};
// ===========================================================================

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

static void rgb_led_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = RGB_LED_GPIO,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .led_model = LED_MODEL_WS2812,
        .flags.invert_out = false,
    };
    // Backend SPI em vez de RMT: o ESP32C6 so tem 2 canais de TX no RMT e o
    // canal do IR ja consome os 2 (mem_block_symbols=64 usa 2 blocos de 48
    // simbolos). Usando SPI aqui, o LED nao disputa canal com o IR.
    led_strip_spi_config_t spi_config = {
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };
    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &s_rgb_led));
    led_strip_clear(s_rgb_led);
}

// Pisca o LED da placa uma vez. Usada como feedback visual de clique no botao.
// cor: r/g/b de 0-255. Bloqueia a tarefa chamadora por RGB_LED_BLINK_MS.
static void rgb_led_blink(uint8_t r, uint8_t g, uint8_t b)
{
    led_strip_set_pixel(s_rgb_led, 0, r, g, b);
    led_strip_refresh(s_rgb_led);
    vTaskDelay(pdMS_TO_TICKS(RGB_LED_BLINK_MS));
    led_strip_clear(s_rgb_led);
}

static void ir_btn_init(void)
{
    gpio_config_t btn_cfg = {
        .pin_bit_mask = (1ULL << IR_BTN_GPIO_NUM),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,     // botao liga o pino ao GND, precisa do pull-up interno
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,        // leitura por polling, sem interrupcao
    };
    ESP_ERROR_CHECK(gpio_config(&btn_cfg));
}

static void TarefaBotaoIR(void *pvParameters)
{
    int estadoAnterior = 1;   // solto = HIGH (pull-up)
    int contadorClique = 0;   // 0,1,2 = indice no ciclo | 3 = proximo clique so reseta

    while (1) {
        int estadoAtual = gpio_get_level(IR_BTN_GPIO_NUM);

        // Borda de descida: botao acabou de ser pressionado
        if (estadoAnterior == 1 && estadoAtual == 0) {
            vTaskDelay(pdMS_TO_TICKS(IR_BTN_DEBOUNCE_MS));

            // Reconfirma o nivel apos o debounce para filtrar ruido/bounce
            if (gpio_get_level(IR_BTN_GPIO_NUM) == 0) {
                if (contadorClique < IR_BTN_CICLO_LEN) {
                    int comando = s_cicloComandos[contadorClique];
                    ESP_LOGI(TAG, "[BOTAO] Clique #%d -> comando %s", contadorClique + 1, ir_comando_nome((ir_comando_t)comando));
                    rgb_led_blink(255, 255, 255);   // pulso branco = clique valido
                    xQueueSend(filaComandosAr, &comando, portMAX_DELAY);
                    contadorClique++;
                } else {
                    ESP_LOGI(TAG, "[BOTAO] Clique #%d -> reset da contagem", contadorClique + 1);
                    rgb_led_blink(255, 140, 0);     // pulso ambar = reset da contagem
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
                    vTaskDelay(pdMS_TO_TICKS(1000));
                }
            }
        }
    }
}


void emiter_init(void)
{
    ir_tx_init();
    ir_btn_init();
    rgb_led_init();

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