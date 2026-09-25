#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "coms.h"
#include "MQTT.h"
#include "esp_timer.h"
#include "curtain.h"
#include "emiter.h"
#include "humidity.h"
#include "temperature.h"

#define broker_interval 15
#define data_interval 8
#define HVAC_interval 17
float synchronize = 100.0f;

bool blacked_out = false;
bool HVACon = false;
int64_t Time = 0;
int64_t LastBrokerMsg = 0;
int64_t LastDataMngt = 0;
int command = 0;
char received_msg[20] = "";
char wellnescheck[20] = "";
char instrucao[20] = ""; //ABCDEFGHIJ
float data[] = {25.0f, 25.0f, 0.0f, 0.0f, 0.0f}; // {DS18B20, DHT22_t, DHT22_h, Ocupacao, Mensagens}
float old_data[] = {25.0f, 25.0f, 0.0f, 0.0f, 0.0f};
float wakeup_data[] = {25.0f, 25.0f, 0.0f, 0.0f,9.0f};
float last_temp = 25.0f;
float temp_alvo = 25.0f;
float broker_msg = 0.0f;
float DS18B20 = 0.0f;
dht22_reading_t DHT22 = {0.0f, 0.0f};
emiter_cmd_t cmd = {0};

void app_main(void) {
    //setup das conexões
    curtain_init();
    emiter_init();
    DHT22_init();
    DS18B20_init();
    coms_init();
    Time = esp_timer_get_time()/1000000;
    LastBrokerMsg = 0;
    LastDataMngt = 0;
    
    while (1) {
        Time = esp_timer_get_time()/1000000;
        memset(&cmd, 0, sizeof(cmd));
        
        //Comunicacao MQTT-------------------------------------------------
        if (data[3] == 1 && old_data[3] == 0) { // 0 segundos
            if (ESP_Iam == 0) {intracom_send(&synchronize, -1);}
            intercom_send(data);
            memcpy(old_data, data, sizeof(data));
            memset(&cmd, 0, sizeof(cmd));
            cmd.power = EMITER_ON;
            HVACon = true;
            if (!send_ir_command(&cmd)) {
                HVACon = false;
                printf("Falha ao enviar comando de desligar ao HVAC\n");
            }
            LastBrokerMsg = Time;
            printf("ESP%i woke up at %lld\n", ESP_Iam, (long long)LastBrokerMsg);
        }

        //Obtencao de dados------------------------------------------------
        if ((Time - LastDataMngt) >= data_interval) { // 8 segundos
            data[3] += carga_termica;
            carga_termica = 0;
            if (DS18B20_read(&DS18B20)) {
                data[0] = DS18B20;
            }
            if (DHT22_read(&DHT22)) {
                data[1] = DHT22.temperature;
                data[2] = DHT22.humidity;
            }
            printf("Ocupacao: %d\n", (int)data[3]);
            vTaskDelay(pdMS_TO_TICKS(100));

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
                memset(&cmd, 0, sizeof(cmd));
                cmd.temp_c = temp_alvo;
                if (!send_ir_command(&cmd)) {
                    printf("Falha ao enviar comando de temperatura ao HVAC\n");
                }
                printf("Set HVAC to: %.1f\n", temp_alvo);
                last_temp = temp_alvo;
            }
            if ((broker_msg == 9) && HVACon) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.power = EMITER_OFF;
                HVACon = false;
                broker_msg = 0;
                if (!send_ir_command(&cmd)) {
                    printf("Falha ao enviar comando de desligar ao HVAC\n");
                    HVACon = true;
                }
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