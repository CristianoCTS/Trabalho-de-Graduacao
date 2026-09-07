#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "MAC.h"
#include "mqtt_client.h"
#include "MQTT.h"
#include "nvs_flash.h"

bool wifi_conected = false;
bool mqtt_conected = false;
static esp_mqtt_client_handle_t mqtt_client;
static char mqtt_received[20] = "";
static uint8_t *slaves[10];
static char msn[20] = "";
static char received_msg[20] = "";

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        printf("coms: Ligando a antena\n");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        printf("coms: WiFi conectado. IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_conected = true;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
        printf("coms: Desconectado, reconectando... motivo: %d\n", event->reason);
        wifi_conected = false;
        esp_wifi_connect();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_conected = true;
            printf("intercom: MQTT conectado\n");
            esp_mqtt_client_subscribe(mqtt_client, MQTTsub, 0);
            break;
        case MQTT_EVENT_DATA:
            snprintf(mqtt_received, sizeof(mqtt_received), "%.*s", event->data_len, event->data);
            printf("intercom: Recebido: %s\n", mqtt_received);
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_conected = false;
            printf("intercom: MQTT desconectado\n");
            break;
        case MQTT_EVENT_ERROR:
            printf("MQTT erro - tipo: %d, motivo: %d\n", event->error_handle->error_type, event->error_handle->connect_return_code);
            break;
        default:
            break;
    }
}

static void receive_msg(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    memset(received_msg, 0, sizeof(received_msg));
    if (len < sizeof(received_msg)) {
        memcpy(received_msg, data, len);
        received_msg[len] = '\0';
    }
}

void coms_init(void) {

    nvs_flash_init();
    mac_init();
    credentials_init();

    // Configura Wi-Fi----------------------------------------------------------------
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    // Configura Wi-Fi----------------------------------------------------------------

    //Configura o MQTT----------------------------------------------------------------
    esp_netif_set_hostname(netif, HOSTNAME);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = SSID,
            .password = SSIDp,
            .threshold.authmode = WIFI_AUTH_OPEN,
        },
    };
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    while (!wifi_conected) {
        printf("Esperando conexão wifi\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri  = MQTT_BROKER,
        .broker.address.port = MQTT_port,
        .credentials.client_id = MQTTc,
        .credentials.username = MQTTu,
        .credentials.authentication.password = MQTTp,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
    while (!mqtt_conected) {
        printf("Esperando conexão MQTT\n");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
    //Configura o MQTT----------------------------------------------------------------

    //Configura o ESPNOW--------------------------------------------------------------
    int j = 0;
    for (int i = 0; i < NUM_ESPS; i++) { // Monta lista de slaves
        if (!ESP[i].Iam) {
            slaves[j++] = ESP[i].mac;
        }
    }
    esp_now_init(); // Inicia ESP-NOW
    esp_now_register_recv_cb(receive_msg);
    
    for (int i = 0; i < (NUM_ESPS - 1); i++) { // Registra peers
        esp_now_peer_info_t slaveInfo = {};
        memcpy(slaveInfo.peer_addr, slaves[i], 6);
        slaveInfo.channel = 0;
        slaveInfo.encrypt = false;
        esp_now_add_peer(&slaveInfo);
    }
    //Configura o ESPNOW--------------------------------------------------------------
}

void intercom_send(const float *data) {
    if (!mqtt_conected) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "field1=%.1f&field2=%.1f&field3=%.1f", data[0], data[1], data[2]);
    esp_mqtt_client_publish(mqtt_client, MQTTpub, buf, 0, 1, 0);
}

void wake_up(const float *data) {
    if (!mqtt_conected) return;
    char buf[48];
    snprintf(buf, sizeof(buf), "field1=%.1f&field2=%.1f&field3=%.1f", data[0], data[1], data[2]);
    esp_mqtt_client_publish(mqtt_client, MQTT.pub[0], buf, 0, 1, 0);
}

void intercom_read(char *out) {
    strncpy(out, mqtt_received, sizeof(mqtt_received) - 1);
    out[sizeof(mqtt_received) - 1] = '\0';
    memset(mqtt_received, 0, sizeof(mqtt_received));
}


void intracom_send(const float *data, int slave_index) {
    char message[16];
    sprintf(message, "%f", *data);
    if (slave_index == -1) {
        printf("Sent to all\n");
        for (int i = 0; i < (NUM_ESPS - 1); i++) {
            printf("Sent to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                slaves[i][0], slaves[i][1], slaves[i][2],
                slaves[i][3], slaves[i][4], slaves[i][5]);
            strcpy(msn, message);
            esp_now_send(slaves[i], (uint8_t *)msn, strlen(msn));
        }
    } else if (slave_index < (NUM_ESPS - 1)) {
        printf("Sent to one\n");
        printf("Sent to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
            slaves[slave_index][0], slaves[slave_index][1], slaves[slave_index][2],
            slaves[slave_index][3], slaves[slave_index][4], slaves[slave_index][5]);
        strcpy(msn, message);
        esp_now_send(slaves[slave_index], (uint8_t *)msn, strlen(msn));
    } else {
        printf("slave inválido: %d\n", slave_index);
    }
    vTaskDelay(pdMS_TO_TICKS(10));
}

void intracom_read(char *out_msg) {
    strncpy(out_msg, received_msg, sizeof(received_msg) - 1);
    out_msg[sizeof(received_msg) - 1] = '\0';
    memset(received_msg, 0, sizeof(received_msg));
}