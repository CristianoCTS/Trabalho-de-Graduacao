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

#define broker_interval 15
#define data_interval 8
#define HVAC_interval 17
float synchronize = 100.0f;

bool blacked_out = false;
bool ESP0on = true;
int64_t Time = 0;
int64_t LastBrokerMsg = 0;
int64_t LastDataMngt = 0;
int command = 0;
char received_msg[20] = "";
char wellnescheck[20] = "";
char instrucao[20] = ""; //ABCDEFGHIJ
float data[] = {25.0f, 0.0f, 0.0f}; // {Temperatura, Ocupacao, Mensagens}
float old_data[] = {25.0f, 0.0f, 0.0f};
float wakeup_data[] = {25.0f, 0.0f, 9.0f};
float last_temp = 25.0f;
float temp_alvo = 25.0f;
float broker_msg = 0.0f;

void app_main(void) {
    //setup das conexões
    coms_init();
    Time = esp_timer_get_time()/1000000;
    LastBrokerMsg = 0;
    LastDataMngt = 0;
    
    while (1) {
        Time = esp_timer_get_time()/1000000;
        
        //Comunicacao MQTT-------------------------------------------------
        if (data[1] == 1 && old_data[1] == 0) { // 0 segundos
            if (ESP_Iam == 0) {intracom_send(&synchronize, -1);}
            intercom_send(data);
            memcpy(old_data, data, sizeof(data));
            LastBrokerMsg = Time;
            printf("ESP%i woke up at %lld\n", ESP_Iam, (long long)LastBrokerMsg);
        }

        //Obtencao de dados------------------------------------------------
        if ((Time - LastDataMngt) >= data_interval) { // 8 segundos
            data[0]++;
            if (data[0] > 35) {
                data[0] = 25;
            }
            data[1] = 1;
            LastDataMngt = Time;
            printf("ESP%i read data at %lld\n", ESP_Iam, (long long)LastDataMngt);
        }
        //Obtencao de dados------------------------------------------------

        if ((Time - LastBrokerMsg) >= broker_interval) { // 15 segundos
            if (command == 1) {
                intercom_send(data);
                command = 0;
                LastBrokerMsg = Time;
                printf("ESP%i sent broker a message at %lld\n", ESP_Iam, (long long)LastBrokerMsg);
            }
        }

        if ((Time - LastBrokerMsg) >= HVAC_interval) { // 17 segundos
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
                        printf("Intercom: Request in %lld\n", (long long)LastBrokerMsg);
                        printf("Temps: ESP0: %d, ESP1: %d, ESP2: %d\n", tA, tB, tC);
                        printf("Msgs:  ESP0: %d, ESP1: %d, ESP2: %d\n", msgA, msgB, msgC);
                        break;
                    default:
                        printf("Temps: ESP0: %d, ESP1: %d, ESP2: %d\n", tA, tB, tC);
                        printf("Msgs:  ESP0: %d, ESP1: %d, ESP2: %d\n", msgA, msgB, msgC);
                        break;
                }
                switch (ESP_Iam)
                {
                case 0:
                    temp_alvo = tA;
                    broker_msg = msgA;
                    break;
                case 1:
                    temp_alvo = tB;
                    broker_msg = msgB;
                    break;
                case 2:    
                    temp_alvo = tC;
                    broker_msg = msgC;
                    break;
                default:
                    break;
                }
            }

            if (!(temp_alvo == last_temp)) {
                printf("Set HVAC to: %.1f\n", temp_alvo);
                last_temp = temp_alvo;
            }
            if ((broker_msg == 9)) {
                printf("Turn HVAC off: %.1f\n", temp_alvo);
            }
        }
        //Comunicacao MQTT-------------------------------------------------

        //Comunicacao ESPNOW-----------------------------------------------
        intracom_read(received_msg);
        float msg = atof(received_msg);
        if (msg == 100.0f) {
            LastBrokerMsg = Time;
            printf("Intracom: synchronized in %lld\n", (long long)LastBrokerMsg);
        }
        memset(received_msg, 0, sizeof(received_msg));
        //Comunicacao ESPNOW-----------------------------------------------
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}