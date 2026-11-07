#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "can_config.h"
#ifdef CAN_BUS_HOST_TEST
#define portTICK_PERIOD_MS 10
typedef struct { uint32_t identifier; uint8_t data_length_code; uint8_t data[8]; bool extd, rtr; } twai_message_t;
#else
#include "driver/twai.h"
#endif

bool can_bus_start(const can_config_t *config);
bool can_bus_reload(const can_config_t *config);
bool can_bus_send(uint32_t id, const uint8_t *data, uint8_t length, uint32_t period_ms);
bool can_bus_stop_periodic(void);
uint32_t can_bus_period_ms(void);
uint32_t can_bus_period_id(void);
bool can_bus_receive(twai_message_t *frame);
bool can_bus_running(void);
uint32_t can_bus_dropped(void);
uint32_t can_bus_tx_failed(void);
bool can_bus_measure_hz(uint16_t id, uint32_t sample_ms, uint32_t *frames);
uint32_t can_bus_seen_count(uint16_t id);
/* UINT32_MAX selects every received frame, including extended frames. */
bool can_bus_echo_start(uint32_t id);
void can_bus_echo_stop(void);
bool can_bus_echo_receive(twai_message_t *frame);
uint32_t can_bus_echo_dropped(void);
