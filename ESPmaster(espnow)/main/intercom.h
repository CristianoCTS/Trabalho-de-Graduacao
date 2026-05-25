#pragma once

void intercom_init(void);
void intercom_publish(const char *topic, const char *data);
void intercom_read(char *out);