#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/rmt_rx.h"
#include "esp_log.h"
#include "receiver.h"

static const char *TAG = "IR_RX";

#define IR_RX_GPIO_NUM      GPIO_NUM_4
#define IR_RESOLUTION_HZ    1000000
#define IR_RX_MAX_SYMBOLS   256
#define IR_RX_IDLE_TIMEOUT_NS   (30 * 1000 * 1000)
#define IR_RX_MIN_GLITCH_NS     1250

static QueueHandle_t s_receive_queue;
static rmt_symbol_word_t s_raw_symbols[IR_RX_MAX_SYMBOLS];
static rmt_channel_handle_t s_rx_channel = NULL;
static rmt_receive_config_t s_receive_config;

static bool IRAM_ATTR rmt_rx_done_callback(rmt_channel_handle_t channel,
                                            const rmt_rx_done_event_data_t *edata,
                                            void *user_data)
{
    BaseType_t high_task_wakeup = pdFALSE;
    QueueHandle_t queue = (QueueHandle_t)user_data;
    xQueueSendFromISR(queue, edata, &high_task_wakeup);
    return high_task_wakeup == pdTRUE;
}

static void print_as_source_code(const rmt_symbol_word_t *symbols, size_t num_symbols)
{
    printf("\n--- SINAL RECEBIDO (%d simbolos) ---\n", (int)num_symbols);
    printf("uint16_t rawData[%d] = {", (int)(num_symbols * 2));
    for (size_t i = 0; i < num_symbols; i++) {
        printf("%d, %d", symbols[i].duration0, symbols[i].duration1);
        if (i != num_symbols - 1) {
            printf(", ");
        }
    }
    printf("};\n");
    printf("----------------------\n\n");
}

// Tarefa que fica bloqueada na fila esperando um pacote IR completo.
// O bloqueio aqui nao trava o resto do sistema: as outras tarefas seguem rodando.
static void TarefaReceptorIR(void *pvParameters)
{
    rmt_rx_done_event_data_t rx_data;

    while (1) {
        if (xQueueReceive(s_receive_queue, &rx_data, portMAX_DELAY) == pdTRUE) {
            print_as_source_code(rx_data.received_symbols, rx_data.num_symbols);
            ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_raw_symbols,
                                         sizeof(s_raw_symbols), &s_receive_config));
        }
    }
}

void receiver_init(void)
{
    printf("=========================================\n");
    printf("Receptor IR (ESP-IDF/RMT) iniciado no GPIO %d!\n", IR_RX_GPIO_NUM);
    printf("Aponte o controle do Ar para o TSOP e aperte UM botao.\n");
    printf("=========================================\n");

    s_receive_queue = xQueueCreate(1, sizeof(rmt_rx_done_event_data_t));
    if (s_receive_queue == NULL) {
        ESP_LOGE(TAG, "Falha ao criar fila de recepcao");
        return;
    }

    rmt_rx_channel_config_t rx_channel_cfg = {
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = IR_RESOLUTION_HZ,
        .mem_block_symbols = 64,
        .gpio_num = IR_RX_GPIO_NUM,
        .flags.invert_in = false,
        .flags.with_dma = false,
    };
    ESP_ERROR_CHECK(rmt_new_rx_channel(&rx_channel_cfg, &s_rx_channel));

    rmt_rx_event_callbacks_t cbs = {
        .on_recv_done = rmt_rx_done_callback,
    };
    ESP_ERROR_CHECK(rmt_rx_register_event_callbacks(s_rx_channel, &cbs, s_receive_queue));
    ESP_ERROR_CHECK(rmt_enable(s_rx_channel));

    s_receive_config.signal_range_min_ns = IR_RX_MIN_GLITCH_NS;
    s_receive_config.signal_range_max_ns = IR_RX_IDLE_TIMEOUT_NS;

    ESP_ERROR_CHECK(rmt_receive(s_rx_channel, s_raw_symbols,
                                 sizeof(s_raw_symbols), &s_receive_config));

    BaseType_t r = xTaskCreate(TarefaReceptorIR, "TarefaRxIR", 4096, NULL, 4, NULL);
    if (r != pdPASS) {
        ESP_LOGE(TAG, "[ERRO CRITICO] Falha ao criar a Tarefa do Receptor IR!");
    } else {
        ESP_LOGI(TAG, "[RTOS] Tarefa do Receptor IR iniciada com sucesso.");
    }
}