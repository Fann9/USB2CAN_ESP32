#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { WIFI_CONSOLE_CONNECTED, WIFI_CONSOLE_DISCONNECTED, WIFI_CONSOLE_BYTE } wifi_console_event_kind_t;
typedef struct { wifi_console_event_kind_t kind; uint8_t byte; } wifi_console_event_t;

bool wifi_console_start(void);
bool wifi_console_pop_event(wifi_console_event_t *event);
bool wifi_console_write(const char *data, size_t size);
void wifi_console_disconnect(void);
bool wifi_console_connected(void);
bool wifi_console_ready(void);
void wifi_console_ip(char *out, size_t size);
