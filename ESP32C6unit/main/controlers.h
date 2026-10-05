#pragma once
#include <stdint.h>

#define HIST_DT_S 8
#define HIST_LEN (120 * (60 / HIST_DT_S +  1))
#define HIST_OCUP 0
#define HIST_TEMP 1
extern float old_temps[2][HIST_LEN];

float LigaDesliga(float Ocupacao, float temp_alvo);
float ControlPI(float Ocupacao, float temp_alvo);
float ControlAdap(float Ocupacao, float temp_alvo);
float Step(float Ocupacao, float temp_alvo);