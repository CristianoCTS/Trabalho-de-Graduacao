#include "MAC.h"

ESP_t ESP[] = {
    {9, {0xE8, 0xF6, 0x0A, 0xFC, 0xBF, 0x50}, false},
    {8, {0xE8, 0xF6, 0x0A, 0xFC, 0x11, 0xD8}, false},
    {6, {0xE8, 0xF6, 0x0A, 0xFC, 0xD2, 0xC8}, false}
};

int ESP_Iam = -1; //ESP não na lista
const int NUM_ESPS = sizeof(ESP) / sizeof(ESP[0]);

void mac_init(void) {
    uint8_t this_mac[6];
    esp_read_mac(this_mac, ESP_MAC_WIFI_STA);

    for (int i = 0; i < NUM_ESPS; i++) {
        if (memcmp(this_mac, ESP[i].mac, 6) == 0) {
            ESP[i].Iam = true;
            ESP_Iam = i;
            break;
        }
    }
}