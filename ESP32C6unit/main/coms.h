#pragma once
#include <stdint.h>
#include <stdbool.h>

extern float parameters[20];

void coms_init(bool all);
void wake_up(const float *data);
bool intercom_send(const float *data);
bool intercom_read(char *out);
bool intercom_timestamp(char *out);
bool intercom_ocupation(char *out);
void intercom_read_parameters(void);
bool intracom_send(const float *data, int slave_index);
bool intracom_read(char *out_msg);