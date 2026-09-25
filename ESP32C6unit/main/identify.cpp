#include "identify.h"

#include <new>
#include <stdio.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "IRrecv.h"
#include "IRutils.h"
#include "IRac.h"

#define IR_GPIO 4
#define IR_BUF_SIZE 1024
#define IR_TIMEOUT_MS 50

static IRrecv *irrecv = nullptr;
static decode_results results;

static void identify_task(void *params)
{
    while (true) {
        if (irrecv->decode(&results)) {
            printf("%s\n", resultToHumanReadableBasic(&results).c_str());

            String ac = IRAcUtils::resultAcToString(&results);
            if (ac.length()) {
                printf("A/C: %s\n", ac.c_str());
            }

            printf("%s\n", resultToSourceCode(&results).c_str());
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void identify_init(void)
{
    irrecv = new (std::nothrow) IRrecv(IR_GPIO, IR_BUF_SIZE, IR_TIMEOUT_MS, true);
    if (irrecv == nullptr) {
        printf("Sem memoria para o IRrecv\n");
        return;
    }
    irrecv->enableIRIn();

    if (xTaskCreate(identify_task, "identify", 6144, nullptr, 1, nullptr) != pdPASS) {
        printf("Falha ao criar a tarefa\n");
    }
}