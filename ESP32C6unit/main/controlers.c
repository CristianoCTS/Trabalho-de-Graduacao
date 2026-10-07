#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_event.h"
#include "esp_timer.h"
#include "controlers.h"
#include "coms.h"

extern float parameters[20];
#define SET_MIN (parameters[0])
#define SET_MAX (parameters[1])
#define SET_PADRAO (parameters[2])
#define LD_BANDA (parameters[3])
#define LD_SET_FRIO (parameters[4])
#define LD_SET_OCIOSO (parameters[5])
#define PI_KP (parameters[6])
#define PI_TI (parameters[7])
#define PI_KI (PI_KP / PI_TI)
#define PI_JANELA ((int)parameters[8])
#define PI_IMAX (parameters[9])
#define AD_TREF_VAZIA (parameters[10])
#define AD_KFF0 (parameters[11])
#define AD_GAMMA (parameters[12])
#define AD_JAN_AD ((int)parameters[13])
#define AD_KFF_MAX (parameters[14])
#define CORR_ACIMA (parameters[15])
#define CORR_ABAIXO (parameters[16])
#define STEP_LIGADO_S ((int64_t)parameters[17])
#define STEP_CICLO_S ((int64_t)parameters[18])

float old_temps[2][HIST_LEN] = { [0 ... 1] = { [0 ... HIST_LEN - 1] = NAN } };
static float ultimo_setp = NAN;
static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

static int janela(int minutos) {
    int n = minutos * 60 / HIST_DT_S + 1;
    return n > HIST_LEN ? HIST_LEN : n;
}

static float finaliza(float v) {
    ultimo_setp = roundf(clampf(v, SET_MIN, SET_MAX));
    return ultimo_setp;
}

static float setp_anterior_valido(void) {
    if (isnan(ultimo_setp) || ultimo_setp < SET_MIN || ultimo_setp > SET_MAX) return SET_PADRAO;
    return ultimo_setp;
}

static bool corrige_compressor(float erro) {
    return !((erro < CORR_ACIMA && ultimo_setp != SET_MIN) || erro <= CORR_ABAIXO);
}

static bool integral_hist(int n, float ref, bool pesar_ocup, float *out) {
    float acc = 0.0f, f_prev = 0.0f, occ = 0.0f;
    int i_prev = 0, validas = 0;
    for (int i = n - 1; i >= 0; i--) {
        float T = old_temps[HIST_TEMP][i];
        if (isnan(T)) continue;
        float o = old_temps[HIST_OCUP][i];
        if (!isnan(o)) occ = o;
        float f = (T - ref) * (pesar_ocup ? fmaxf(occ, 0.0f) : 1.0f);
        if (validas > 0) acc += 0.5f * (f + f_prev) * (float)(i_prev - i) * HIST_DT_S / 60.0f;
        f_prev = f; i_prev = i; validas++;
    }
    *out = acc;
    return validas >= 2;
}

// ---------------- Controladores ----------------
float LigaDesliga(float Ocupacao, float temp_alvo) {
    (void)Ocupacao;
    float T = old_temps[HIST_TEMP][0];
    bool resfriando = isnan(ultimo_setp) || ultimo_setp != LD_SET_OCIOSO;
    if (isnan(T)) {
    } else if (T >= temp_alvo + LD_BANDA) {
        resfriando = true;
    } else if (T <= temp_alvo - LD_BANDA) {
        resfriando = false;
    }
    ultimo_setp = resfriando ? LD_SET_FRIO : LD_SET_OCIOSO;
    return ultimo_setp;
}

float ControlPI(float Ocupacao, float temp_alvo) {
    (void)Ocupacao;
    float T = old_temps[HIST_TEMP][0];
    if (isnan(T)) return (ultimo_setp = setp_anterior_valido());

    if (corrige_compressor(T - temp_alvo)) return (ultimo_setp = SET_MIN);

    float I = 0.0f;
    if (!integral_hist(janela(PI_JANELA), temp_alvo, false, &I)) I = 0.0f;
    I = clampf(I, -PI_IMAX / PI_KI, PI_IMAX / PI_KI);

    float delta = PI_KP * (T - temp_alvo) + PI_KI * I;
    return finaliza(temp_alvo - delta);
}

float ControlAdap(float Ocupacao, float temp_alvo) {
    float T = old_temps[HIST_TEMP][0];

    float n = Ocupacao;
    if (isnan(n)) n = old_temps[HIST_OCUP][0];
    bool nConhecida = !isnan(n);
    n = nConhecida ? fmaxf(n, 0.0f) : 0.0f;

    if (isnan(T)) return (ultimo_setp = setp_anterior_valido());

    float kff = AD_KFF0;
    float Ikff = 0.0f;
    if (integral_hist(janela(AD_JAN_AD), temp_alvo, true, &Ikff)) kff = AD_KFF0 + AD_GAMMA * Ikff;
    kff = clampf(kff, 0.0f, AD_KFF_MAX);

    bool vazia = nConhecida && n == 0.0f;
    float Tref = vazia ? AD_TREF_VAZIA : temp_alvo;
    if (!vazia && corrige_compressor(T - Tref)) return (ultimo_setp = SET_MIN);

    float I = 0.0f;
    if (!integral_hist(janela(PI_JANELA), Tref, false, &I)) I = 0.0f;
    I = clampf(I, -PI_IMAX / PI_KI, PI_IMAX / PI_KI);

    float delta = PI_KP * (T - Tref) + PI_KI * I + kff * n;
    return finaliza(Tref - delta);
}

float Step(float Ocupacao, float temp_alvo) {
    (void)Ocupacao; (void)temp_alvo;
    int64_t t = (esp_timer_get_time() / 1000000) % STEP_CICLO_S;
    ultimo_setp = (t < STEP_LIGADO_S) ? SET_MIN : SET_MAX;
    return ultimo_setp;
}