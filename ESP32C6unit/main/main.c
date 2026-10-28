#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "coms.h"
#include "MQTT.h"
#include "esp_timer.h"
#include "curtain.h"
#include "PIRcurtain.h"
#include "emiter.h"
#include "humidity.h"
#include "temperature.h"
#include "controlers.h"


void app_main(void) {
    PIRcurtain_init();
}