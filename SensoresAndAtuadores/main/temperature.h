#pragma once
#include <stdbool.h>
#include <stdint.h>

void DS18B20_init(void);
bool DS18B20_read(float *out_celsius);