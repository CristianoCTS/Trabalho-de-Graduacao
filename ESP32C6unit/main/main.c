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

#define broker_interval 15
#define data_interval 8
#define reading_interval 4

bool blacked_out = false;
bool HVACon = false;
int64_t Time = 0;
int64_t LastBrokerMsg = 0;
int64_t LastDataMngt = 0;
int command = 0;
char received_msg[20] = "";
char wellnescheck[20] = "";
char instrucao[20] = ""; //ABCDEFGHIJ
char horario[20] = ""; //Segundos
char ocupacao[20] = ""; //N
float data[] = {24.0f, 24.0f, 55.0f, 0.0f, 0.0f, 24.0f}; // {DS18B20, DHT22_t, DHT22_h, Ocupacao, Mensagens, TemperaturaEnviada}
float wakeup_data[] = {24.0f, 24.0f, 55.0f, 0.0f, 0.0f, 24.0f};
float last_temp = 0.0f;
float temp_alvo = 25.0f;
float new_temp = 23.0f;
float broker_msg = 0.0f;
float DS18B20 = 0.0f;
dht22_reading_t DHT22 = {0.0f, 0.0f};
emiter_cmd_t cmd = {0};
int ControlType = 1;
float TimeStamp = 0.0f;
float ocupation = NAN;
int64_t IRerror = 0;
int64_t COMSerror = 0;

void app_main(void) {
    //setup das conexões
    curtain_init();
    emiter_init();
    DHT22_init();
    DS18B20_init();
    led_set(true, 120, 20, 0);
    coms_init(true);
    Time = esp_timer_get_time()/1000000;
    led_set(false, 0, 0, 0);
    
    while (1) {
        printf("carga termica: %.1f\n", carga_termica);
        Time = esp_timer_get_time()/1000000;
        memset(&cmd, 0, sizeof(cmd));
        if (IRerror >= 6) {data[4] = 6.0f; IRerror = 0;}
        if (COMSerror >= 7) {data[4] = 7.0f; COMSerror = 0;}

        //Comunicacao MQTT-------------------------------------------------
        if (data[3] > 0 && (ocupation == 0 || isnan(ocupation))) { // 0 segundos
            TimeStamp += (float)((Time - LastBrokerMsg) % 3600); // valor em que o timestamp ser maior ou menos não muda nada 
            wakeup_data[3] += (data[3] * 10000.0f) + TimeStamp;
            wakeup_data[4] = 2.0f;
            if (intercom_send(wakeup_data)) {
                ocupation = data[3];
                data[3] = 0.0f;
                memset(&cmd, 0, sizeof(cmd));
                cmd.power = EMITER_ON;
                HVACon = true;
                if (send_ir_command(&cmd)) {
                    printf("Turn HVAC on: %.1f\n", temp_alvo);
                    IRerror = 0;
                } else {
                    HVACon = false;
                    IRerror += 1;
                    printf("Falha ao enviar comando de ligar ao HVAC\n");
                }
                TimeStamp -= (float)((Time - LastBrokerMsg) % 3600);
                LastBrokerMsg = Time;
                printf("ESP%i woke up at %lld\n", ESP_Iam, (long long)LastBrokerMsg);
                led_blink(0, 150, 20);
                COMSerror = 0;
            } else {
                printf("Falha ao enviar mensagem de wakeup\n");
                COMSerror += 1;
                TimeStamp -= (float)((Time - LastBrokerMsg) % 3600);
            }    
            wakeup_data[3] = 0.0f; 
            wakeup_data[4] = 0.0f;
        }

        if ((Time - LastBrokerMsg) >= reading_interval) { // 4 segundos
            //Obtencao de Parametros--------------------------------------------------------
            intercom_read_parameters();
            //Obtencao de Parametros--------------------------------------------------------
            //Obtencao da Time Stamp--------------------------------------------------------
            if (intercom_timestamp(horario)) {
                TimeStamp = atof(horario);
            }
            //Obtencao da Time Stamp--------------------------------------------------------
            //Obtencao da Ocupação----------------------------------------------------------
            if (intercom_ocupation(ocupacao)) {
                ocupation = atof(ocupacao); // N
            }
            //Obtencao da Ocupação----------------------------------------------------------
            //Obtencao da Instrução---------------------------------------------------------
            if (intercom_read(instrucao)) {
                int temperature = atoi((char[]){instrucao[0], instrucao[1], '\0'}); // AB
                int Controler = instrucao[2] - '0'; // C
                int msgA = instrucao[3] - '0'; // D
                int msgB = instrucao[4] - '0'; // E
                command = instrucao[5] - '0';  // F
                switch (command) {
                    case 1:
                        printf("Intercom: Request in %lld\n", (long long)LastBrokerMsg);
                        printf("Temperatura Alvo: %d\n", temperature);
                        printf("Msgs:  ESP0: %d, ESP1: %d\n", msgA, msgB);
                        break;
                    default:
                        printf("Temperatura Alvo: %d\n", temperature);
                        printf("Msgs:  ESP0: %d, ESP1: %d\n", msgA, msgB);
                        break;
                }
                switch (ESP_Iam)
                {
                case 0:
                    temp_alvo = temperature;
                    broker_msg = msgA;
                    ControlType = Controler;
                    break;
                case 1:
                    temp_alvo = temperature;
                    broker_msg = msgB;
                    ControlType = Controler;
                    break;
                default:
                    break;
                }
                led_blink(255, 0, 255);
            }
            //Obtencao da Instrução---------------------------------------------------------

            if ((broker_msg == 9) && HVACon) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.power = EMITER_OFF;
                if (send_ir_command(&cmd)) {
                    printf("Turn HVAC off: %.1f\n", temp_alvo);
                    HVACon = false;
                    broker_msg = 0;
                    led_blink(255, 255, 255);
                    IRerror = 0;
                } else {
                    printf("Falha ao enviar comando de desligar ao HVAC\n");
                    HVACon = true; 
                    IRerror += 1;
                }
            }

            if (!(new_temp == last_temp)) {
                memset(&cmd, 0, sizeof(cmd));
                cmd.temp_c = new_temp;
                if (send_ir_command(&cmd)) {
                    printf("Set HVAC to: %.1f\n", new_temp);
                    last_temp = new_temp;
                    led_blink(255, 255, 255);
                    IRerror = 0;
                } else {
                    printf("Falha ao enviar comando de temperatura ao HVAC\n");
                    IRerror += 1;
                }
            }
        }

        //Obtencao de dados------------------------------------------------
        if ((Time - LastDataMngt) >= data_interval) { // 8 segundos
            data[3] += carga_termica;
            printf("carga termica: %.1f\n", carga_termica);
            if (data[3] > 50) {data[3] = 0;}
            if (data[3] < -50) {data[3] = 0; data[4] = 8.0f;}
            carga_termica = 0;
            if (DS18B20_read(&DS18B20)) {
                data[0] = DS18B20;
            } else {
                printf("Falha ao ler DS18B20\n");
                data[0] = 24.0f;
            }
            if (DHT22_read(&DHT22)) {
                data[1] = DHT22.temperature;
                data[2] = DHT22.humidity;
            } else {
                printf("Falha ao ler DHT22\n");
                data[1] = 24.0f;
                data[2] = 55.0f;
            }
            printf("Ocupacao': %d\n", (int)data[3]);
            vTaskDelay(pdMS_TO_TICKS(100));

            LastDataMngt = Time;
            printf("ESP%i read data at %lld\n", ESP_Iam, (long long)LastDataMngt);

            for (int k = 0; k < 2; k++) memmove(&old_temps[k][1], &old_temps[k][0], (HIST_LEN-1)*sizeof(float));
            old_temps[HIST_OCUP][0] = ocupation; old_temps[HIST_TEMP][0] = data[0];
            switch (ControlType) {
                case 1: //LigaDesliga
                    new_temp = LigaDesliga(ocupation, temp_alvo);
                    break;
                case 2: //PI
                    new_temp = ControlPI(ocupation, temp_alvo);
                    break;
                case 3: //Adaptativo
                    new_temp = ControlAdap(ocupation, temp_alvo);
                    break;
                case 4: //Degrau
                    new_temp = Step(ocupation, temp_alvo);
                    break;
                default:
                    printf("Tipo de controle inválido\n");
                    new_temp = LigaDesliga(ocupation, temp_alvo);
                    break;
            }
            data[5] = new_temp;
        }
        //Obtencao de dados------------------------------------------------

        if ((Time - LastBrokerMsg) >= broker_interval) { // 15 segundos
            if (command == 1) {
                TimeStamp += (float)((Time - LastBrokerMsg) % 3600);
                data[3] = (data[3] * 10000.0f) + TimeStamp;
                if (intercom_send(data)) {
                    data[3] = 0.0f;
                    data[4] = 0.0f;
                    command = 0;
                    TimeStamp -= (float)((Time - LastBrokerMsg) % 3600);
                    LastBrokerMsg = Time;
                    printf("ESP%i sent broker a message at %lld\n", ESP_Iam, (long long)LastBrokerMsg);
                    led_blink(0, 150, 20);
                    COMSerror = 0;
                } else {
                    printf("Falha ao enviar mensagem ao broker\n");
                    data[3] = floorf(data[3] / 10000.0f);
                    COMSerror += 1;
                    TimeStamp -= (float)((Time - LastBrokerMsg) % 3600);
                }
            }
        }

        //Comunicacao MQTT-------------------------------------------------
        
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}