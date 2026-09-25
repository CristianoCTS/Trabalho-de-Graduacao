#include "emiter.h"
#include <new>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "IRremoteESP8266.h"
#include "IRsend.h"
#include "ir_Goodweather.h"

#define emiter_GPIO 5
#define interframe 150
#define temp_step 0
#define in_LARA  1
#define e_bit_mark 536
#define e_one_space 540
#define e_zero_space 1630
#define e_HDR_mark 6086
#define e_HDR_space 7374
#define e_foot_space 7404
#define e_initial_raw 0xD52806000000ULL

static IRGoodweatherAc *ac = nullptr;
static IRsend *irsend = nullptr;
static SemaphoreHandle_t mutex = nullptr;
static bool first_frame = true;
static char error_code[64];

static bool native_mode(emiter_mode_t modo, uint8_t *out)
{
    switch (modo) {
        case EMITER_MODE_AUTO: *out = kGoodweatherAuto; return true;
        case EMITER_MODE_COOL: *out = kGoodweatherCool; return true;
        case EMITER_MODE_HEAT: *out = kGoodweatherHeat; return true;
        case EMITER_MODE_DRY: *out = kGoodweatherDry; return true;
        case EMITER_MODE_FAN: *out = kGoodweatherFan; return true;
        default: return false;
    }
}

static bool native_fan(emiter_fan_t fan, uint8_t *out)
{
    switch (fan) {
        case EMITER_FAN_AUTO: *out = kGoodweatherFanAuto; return true;
        case EMITER_FAN_LOW: *out = kGoodweatherFanLow; return true;
        case EMITER_FAN_MEDIUM: *out = kGoodweatherFanMed; return true;
        case EMITER_FAN_HIGH: *out = kGoodweatherFanHigh; return true;
        default: return false;
    }
}

static bool native_swing(emiter_flap_t flap, uint8_t *out)
{
    switch (flap) {
        case EMITER_FLAP_OFF: *out = kGoodweatherSwingOff; return true;
        case EMITER_FLAP_SLOW: *out = kGoodweatherSwingSlow; return true;
        case EMITER_FLAP_FAST: *out = kGoodweatherSwingFast; return true;
        default: return false;
    }
}

static bool validate(const emiter_cmd_t *c)
{
    uint8_t n;
    if ((unsigned)c->power > (unsigned)EMITER_OFF) {
        snprintf(error_code, sizeof(error_code), "Argumento invalido: power");
        return false;
    }
    if (c->mode != EMITER_MODE_KEEP && !native_mode(c->mode, &n)) {
        snprintf(error_code, sizeof(error_code), "Argumento invalido: mode");
        return false;
    }
    if (c->fan  != EMITER_FAN_KEEP  && !native_fan(c->fan, &n)) {
        snprintf(error_code, sizeof(error_code), "Argumento invalido: fan");
        return false;
    }
    if (c->flap != EMITER_FLAP_KEEP && !native_swing(c->flap, &n)) {
        snprintf(error_code, sizeof(error_code), "Argumento invalido: flap");
        return false;
    }

    if (c->temp_c > 0.0f) {
        int t = (int)(c->temp_c + 0.5f);
        if (t < (int)kGoodweatherTempMin || t > (int)kGoodweatherTempMax) {
            snprintf(error_code, sizeof(error_code), "Argumento invalido: temp");
            return false;
        }
    }
    return true;
}

static void convert_to_LARA(uint64_t data)
{
    irsend->enableIROut(38);
    irsend->mark(e_HDR_mark);
    irsend->space(e_HDR_space);

    for (int i = 0; i < (int)kGoodweatherBits; i += 8) {
        uint16_t chunk = (uint16_t)((data >> i) & 0xFF);
        chunk = (uint16_t)((((~chunk) & 0xFF) << 8) | chunk);
        irsend->sendData(e_bit_mark, e_one_space, e_bit_mark, e_zero_space, chunk, 16, false);
    }

    irsend->mark(e_bit_mark);
    irsend->space(e_foot_space);
    irsend->mark(e_bit_mark);
}

static void send_frame(const char *what)
{
    if (!first_frame) {
        vTaskDelay(pdMS_TO_TICKS(interframe));
    }
    first_frame = false;

    if (in_LARA) {
        convert_to_LARA(ac->getRaw());
    } else {
        ac->send();
    }
}

static void send_temp(uint8_t alvo)
{
    uint8_t atual = ac->getTemp();
    if (alvo == atual) {
        return;
    }
    if (temp_step) {
        while (atual != alvo) {
            atual = (uint8_t)(atual + ((alvo > atual) ? 1 : -1));
            ac->setTemp(atual);
            send_frame("temp");
        }
    } else {
        ac->setTemp(alvo);
        send_frame("temp");
    }
}

bool emiter_init(void)
{
    if (ac != nullptr) {
        return true;
    }

    mutex = xSemaphoreCreateMutex();
    IRGoodweatherAc *ac_local = new (std::nothrow) IRGoodweatherAc(emiter_GPIO);
    IRsend *irsend_local = new (std::nothrow) IRsend(emiter_GPIO);
    if (mutex == nullptr || ac_local == nullptr || irsend_local == nullptr) {
        snprintf(error_code, sizeof(error_code), "Sem memoria para mutex/IRGoodweatherAc/IRsend");
        if (mutex != nullptr) {
            vSemaphoreDelete(mutex);
            mutex = nullptr;
        }
        delete ac_local;
        delete irsend_local;
        return false;
    }

    ac_local->begin();
    irsend_local->begin();
    ac_local->setRaw(e_initial_raw);

    irsend = irsend_local;
    ac = ac_local;

    printf("Emissor  com protocolo GOODWEATHER no GPIO %d\n", emiter_GPIO);
    return true;
}

bool send_ir_command(const emiter_cmd_t *c)
{
    if (ac == nullptr) {
        printf("Estado nao inicializado\n");
        return false;
    }
    if (c == nullptr) {
        printf("Comando nulo\n");
        return false;
    }
    if (!validate(c)) {
        printf("Comando invalido: %s\n", error_code);
        return false;
    }

    xSemaphoreTake(mutex, portMAX_DELAY);
    first_frame = true;
    uint8_t n = 0;

    if (c->power == EMITER_ON) {
        ac->setPower(true);
        send_frame("power");
    }
    if (c->mode != EMITER_MODE_KEEP) {
        native_mode(c->mode, &n);
        ac->setMode(n);
        send_frame("mode");
    }
    if (c->temp_c > 0.0f) {
        send_temp((uint8_t)((int)(c->temp_c + 0.5f)));
    }
    if (c->fan != EMITER_FAN_KEEP) {
        native_fan(c->fan, &n);
        ac->setFan(n);
        send_frame("fan");
    }
    if (c->flap != EMITER_FLAP_KEEP) {
        native_swing(c->flap, &n);
        ac->setSwing(n);
        send_frame("swing");
    }
    if (c->turbo_toggle) {
        ac->setTurbo(true);  send_frame("turbo");  ac->setTurbo(false);
    }
    if (c->light_toggle) {
        ac->setLight(true);  send_frame("light");  ac->setLight(false);
    }
    if (c->sleep_toggle) {
        ac->setSleep(true);  send_frame("sleep");  ac->setSleep(false);
    }

    if (c->power == EMITER_OFF) {
        ac->setPower(false);
        send_frame("power");
    }
    xSemaphoreGive(mutex);
    return true;
}