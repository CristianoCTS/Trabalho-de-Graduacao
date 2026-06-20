#include "intracom.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "MAC.h"
#include "esp_log.h"
#include "mqtt_client.h"

#define MQTT_BROKER "mqtt3.thingspeak.com"
#define MQTT_port 1883
#define MQTT_TOPIC "nu uh"
#define MQTT_USER   "seu_usuario"
#define MQTT_PWD    "sua_senha"
#define HOSTNAME  "imjustdeperatepleasework"

#define SSID      "not today"
#define PASSWORD  "nope"

static const char *TAG = "intercom";
static esp_mqtt_client_handle_t mqtt_client;
static char mqtt_received[20] = "";
static bool mqtt_connected = false;

static void event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "WiFi conectado. IP: " IPSTR, IP2STR(&event->ip_info.ip));
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Desconectado, reconectando...");
        esp_wifi_connect();
    }
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            mqtt_connected = true;
            ESP_LOGI(TAG, "MQTT conectado");
            esp_mqtt_client_subscribe(mqtt_client, MQTT_TOPIC, 0);
            break;
        case MQTT_EVENT_DATA:
            snprintf(mqtt_received, sizeof(mqtt_received), "%.*s", event->data_len, event->data);
            ESP_LOGI(TAG, "Recebido: %s", mqtt_received);
            break;
        case MQTT_EVENT_DISCONNECTED:
            mqtt_connected = false;
            ESP_LOGI(TAG, "MQTT desconectado");
            break;
        default:
            break;
    }
}

void intercom_init(void) {
    // Configura Wi-Fi
    esp_netif_init();
    esp_event_loop_create_default();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();

    //Configura o MQTT
    esp_netif_set_hostname(netif, HOSTNAME);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &event_handler, NULL);
    wifi_config_t wifi_config = {
        .sta = {
            .ssid     = SSID,
            .password = PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    esp_mqtt_client_config_t mqtt_config = {
        .broker.address.uri  = MQTT_BROKER,
        .broker.address.port = MQTT_port,
        .credentials.client_id = HOSTNAME,
    };
    mqtt_client = esp_mqtt_client_init(&mqtt_config);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void intercom_publish(const char *topic, const char *data) {
    if (!mqtt_connected) return;
    esp_mqtt_client_publish(mqtt_client, topic, data, 0, 1, 0);
}

void intercom_read(char *out) {
    strncpy(out, mqtt_received, sizeof(mqtt_received) - 1);
    out[sizeof(mqtt_received) - 1] = '\0';
    memset(mqtt_received, 0, sizeof(mqtt_received));
}