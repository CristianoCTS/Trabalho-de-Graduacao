#pragma once
#include <stdbool.h>
#include <stdint.h>

extern float carga_termica;

void PIRcurtain_init(void);
void led_init(void);
void led_blink(int r, int g, int b);
void led_set(bool ligado, int r, int g, int b);