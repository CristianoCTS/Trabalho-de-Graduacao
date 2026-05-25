#include "intracom.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_now.h"
#include "MAC.h"

static uint8_t *slaves[10];
static char msn[20] = "";
static char received_msg[20] = "";

static void receive_msg(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
    memset(received_msg, 0, sizeof(received_msg));
    if (len < sizeof(received_msg)) {
        memcpy(received_msg, data, len);
        received_msg[len] = '\0';
    }
}

void intracom_init(void) {
    mac_init();
    // Monta lista de slaves
    int j = 0;
    for (int i = 0; i < NUM_ESPS; i++) {
        if (!ESP[i].Iam) {
            slaves[j++] = ESP[i].mac;
        }
    }
    
    // Inicia ESP-NOW
    esp_now_init();
    esp_now_register_recv_cb(receive_msg);

    // Registra peers
    for (int i = 0; i < (NUM_ESPS - 1); i++) {
        esp_now_peer_info_t slaveInfo = {};
        memcpy(slaveInfo.peer_addr, slaves[i], 6);
        slaveInfo.channel = 0;
        slaveInfo.encrypt = false;
        esp_now_add_peer(&slaveInfo);
    }
}

void intracom_send(const char *message, int slave_index) {
    if (slave_index == -1) {
        printf("Sent to all\n");
        for (int i = 0; i < (NUM_ESPS - 1); i++) {
            printf("Sent to MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                slaves[i][0], slaves[i][1], slaves[i][2],
                slaves[i][3], slaves[i][4], slaves[i][5]);
            strcpy(msn, message);
            esp_now_send(slaves[i], (uint8_t *)msn, strlen(msn));
        }
    } else if (slave_index < NUM_ESPS) {
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