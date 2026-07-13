#include "MAC.h"

ESP_t ESP[] = {
    {9, {0xE8, 0xF6, 0x0A, 0xFC, 0xBF, 0x50}, false},
    {8, {0xE8, 0xF6, 0x0A, 0xFC, 0x11, 0xD8}, false},
    {6, {0xE8, 0xF6, 0x0A, 0xFC, 0xD2, 0xC8}, false},
    {12, {0xA0, 0xF2, 0x62, 0x45, 0xE5, 0x2C}, false}
};

int ESP_Iam = -1; //ESP não na lista
const int NUM_ESPS = sizeof(ESP) / sizeof(ESP[0]);

void mac_init(void) {
    uint8_t this_mac[6];
    esp_read_mac(this_mac, ESP_MAC_WIFI_STA);

    if (NUM_ESPS > 10) {
        printf("Número de ESPs excede o limite de 10\n");
    }

    for (int i = 0; i < NUM_ESPS; i++) {
        if (memcmp(this_mac, ESP[i].mac, 6) == 0) {
            ESP[i].Iam = true;
            ESP_Iam = i;
            break;
        }
    }
}