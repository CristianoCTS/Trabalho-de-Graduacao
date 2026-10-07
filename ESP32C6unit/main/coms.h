#pragma once
#include <stdint.h>

extern float parameters[20];

void coms_init(void);
void wake_up(const float *data);
void intercom_send(const float *data);
void intercom_read(char *out);
void intercom_read_parameters(void);
void intracom_send(const float *data, int slave_index);
void intracom_read(char *out_msg);