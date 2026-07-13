#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "coms.h"
#include "MQTT.h"
#include "esp_timer.h"
#include <stdlib.h>

#define MinInterval 15

int64_t Time = 0;
int64_t LastTime = 0;
int command = 0;
char received_msg[20] = "";
char instrucao[20] = ""; //ABCDEFGHIJ
float data[] = {25.0f, 0.0f, 0.0f}; // {Temperatura, Ocupacao, Mensagens}
float old_data[] = {25.0f, 0.0f, 0.0f};

void app_main(void) {
    //setup das conexões
    coms_init();
    
    while (1) {
        Time = esp_timer_get_time()/1000000;
        if (data[1] == 1 && old_data[1] == 0) {
            intercom_send(data);
        }
        memcpy(old_data, data, sizeof(data));

        //Obtencao de dados------------------------------------------------
        data[0]++; //obtido pelo sensor de temperatura
        if (data[0] > 35) {
            data[0] = 25;
        }
        data[1] = 1; //obtido pela cortina
        data[2]++; //obtido pela lógica de tomada de decisões
        if (data[2] == 9) {
            data[2] = 0;
        }
        //Obtencao de dados------------------------------------------------

        //Comunicacao MQTT-------------------------------------------------
        intercom_read(instrucao);
        if (instrucao[0] != '\0') {
            int tA = atoi((char[]){instrucao[0], instrucao[1], '\0'}); // AB
            int tB = atoi((char[]){instrucao[3], instrucao[4], '\0'}); // DE
            int tC = atoi((char[]){instrucao[6], instrucao[7], '\0'}); // GH
            int msgA = instrucao[2] - '0'; // C
            int msgB = instrucao[5] - '0'; // F
            int msgC = instrucao[8] - '0'; // I
            command = instrucao[9] - '0';  // J
            switch (command) {
                case 1:
                    LastTime = Time;
                    printf("Intercom: Request in %lld\n", (long long)LastTime);
                    break;
                default:
                    break;
            }
        }
        if ((Time - LastTime >= MinInterval) && (command == 1)) {
            intercom_send(data);
            command = 0;
            LastTime = Time;
            //logica que altera a temperatura do ar
        }
        //Comunicacao MQTT-------------------------------------------------
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}