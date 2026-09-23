#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    float temperature;
    float humidity;
} dht22_reading_t;

void DHT22_init(void);
bool DHT22_read(dht22_reading_t *out);