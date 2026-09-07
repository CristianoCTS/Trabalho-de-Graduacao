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
float synchronize = 100.0f;
float ESP0isON = 200.0f;

bool blacked_out = false;
bool ESP0on = true;
int64_t Time = 0;
int64_t LastTime = 0;
int command = 0;
char received_msg[20] = "";
char wellnescheck[20] = "";
char instrucao[20] = ""; //ABCDEFGHIJ
float data[] = {25.0f, 0.0f, 0.0f}; // {Temperatura, Ocupacao, Mensagens}
float old_data[] = {25.0f, 0.0f, 0.0f};
float wakeup_data[] = {25.0f, 0.0f, 9.0f};

void app_main(void) {
    //setup das conexões
    coms_init();
    Time = esp_timer_get_time()/1000000;
    LastTime = 0;
    
    while (1) {
        Time = esp_timer_get_time()/1000000;
        if (data[1] == 1 && old_data[1] == 0) {
            if (ESP_Iam == 0) {intracom_send(&synchronize, -1);}
            intercom_send(data);
            LastTime = Time;
        }
        memcpy(old_data, data, sizeof(data));

        //Obtencao de dados------------------------------------------------
        data[0]++; //obtido pelo sensor de temperatura
        if (data[0] > 35) {
            data[0] = 25;
        }
        data[1] = 1; //obtido pela cortina
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
                    printf("Intercom: Request in %lld\n", (long long)LastTime);
                    break;
                default:
                    break;
            }
        }
        if ((Time - LastTime >= MinInterval) && (command == 1)) {
            intercom_send(data);
            if (ESP_Iam == 0) {intracom_send(&ESP0isON, -1);} //wellnes_check
            command = 0;
            LastTime = Time;
            //logica que altera a temperatura do ar
        }
        if (((MinInterval + 9) >= (Time - LastTime)) && ((Time - LastTime) >= (MinInterval + 5)) && 
            (ESP_Iam != 0) && !ESP0on) {
            printf("ESP0 blacked out\n");
            wake_up(wakeup_data);
        }
        if ((Time - LastTime >= (MinInterval + 10)) && (ESP_Iam != 0)) {
            ESP0on = false;
        }
        //Comunicacao MQTT-------------------------------------------------

        //Comunicacao ESPNOW-----------------------------------------------
        intracom_read(received_msg);
        float msg = atof(received_msg);
        if (msg == 100.0f) {
            LastTime = Time;
            ESP0on = true;
            printf("Intracom: synchronized in %lld\n", (long long)LastTime);
        } else if (msg == 200.0f) {
            ESP0on = true;
            printf("Intracom: ESP0 still on at %lld\n", (long long)LastTime);
        }
        memset(received_msg, 0, sizeof(received_msg));
        //Comunicacao ESPNOW-----------------------------------------------
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}