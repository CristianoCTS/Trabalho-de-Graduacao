#pragma once
#include <stdint.h>

void coms_init(void);
void intercom_send(const char *data);
void intercom_read(char *out);
void intracom_send(const char *message, int slave_index);
void intracom_read(char *out_msg);