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

#define SET_MIN 20.0f
#define SET_MAX 26.0f
#define SET_PADRAO 23.0f
#define LD_BANDA 0.3f
#define LD_SET_FRIO 20.0f
#define LD_SET_OCIOSO 26.0f
#define PI_KP 1.0f
#define PI_TI 15.0f
#define PI_KI (PI_KP / PI_TI)
#define PI_JANELA 30
#define PI_IMAX 3.0f
#define AD_TREF_VAZIA 27.0f
#define AD_KFF0 0.25f
#define AD_GAMMA 0.002f
#define AD_JAN_AD 120
#define AD_KFF_MAX 1.0f
#define CORR_ACIMA 0.3f
#define CORR_ABAIXO -0.2f
#define STEP_LIGADO_S (3 * 3600)
#define STEP_CICLO_S (4 * 3600)

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