#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "esp_mac.h"

typedef struct {
    int com;
    uint8_t mac[6];
    bool Iam;
} ESP_t;

extern const int NUM_ESPS;
extern ESP_t ESP[];
extern int ESP_Iam;

void mac_init(void);