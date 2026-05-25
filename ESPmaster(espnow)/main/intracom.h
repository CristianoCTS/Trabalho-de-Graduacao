#pragma once
#include <stdint.h>

void intracom_init(void);
void intracom_send(const char *message, int slave_index);
void intracom_read(char *out_msg);