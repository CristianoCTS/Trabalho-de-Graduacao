#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "receiver.h"
#include "emiter.h"

void app_main(void)
{
    emiter_init();
    readIR();
}