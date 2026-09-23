#pragma once
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EMITER_KEEP = 0,
    EMITER_ON,
    EMITER_OFF,
} emiter_tri_t;

typedef enum {
    EMITER_MODE_KEEP = 0,
    EMITER_MODE_AUTO,
    EMITER_MODE_COOL,
    EMITER_MODE_HEAT,
    EMITER_MODE_DRY,
    EMITER_MODE_FAN,
} emiter_mode_t;

typedef enum {
    EMITER_FAN_KEEP = 0,
    EMITER_FAN_AUTO,
    EMITER_FAN_LOW,
    EMITER_FAN_MEDIUM,
    EMITER_FAN_HIGH,
} emiter_fan_t;

typedef enum {
    EMITER_FLAP_KEEP = 0,
    EMITER_FLAP_OFF,
    EMITER_FLAP_SLOW,
    EMITER_FLAP_FAST,
    EMITER_FLAP_AUTO = EMITER_FLAP_SLOW,
} emiter_flap_t;

typedef struct {
    emiter_tri_t power;
    emiter_mode_t mode;
    float temp_c;
    emiter_fan_t fan;
    emiter_flap_t flap;
    bool turbo_toggle;
    bool light_toggle;
    bool sleep_toggle;
} emiter_cmd_t;

bool emiter_init(void);
bool send_ir_command(const emiter_cmd_t *cmd); // send_ir_command(&(emiter_cmd_t){ .power = EMITER_ON });

#ifdef __cplusplus
}
#endif