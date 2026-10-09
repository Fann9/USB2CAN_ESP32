#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifndef CAN_CONFIG_PATH
#define CAN_CONFIG_PATH "/flash/can.conf"
#endif
#define CAN_CONFIG_TEXT_SIZE 512
typedef struct {
    uint32_t version, bitrate, tx_gpio, rx_gpio, tx_period_ms, rx_timeout_ms;
} can_config_t;

can_config_t can_config_defaults(void);
int can_config_set(can_config_t *cfg, const char *key, const char *value, char *error, size_t size);
int can_config_validate(const can_config_t *cfg, char *error, size_t size);
int can_config_parse(const char *text, can_config_t *cfg, char *error, size_t size);
int can_config_format(const can_config_t *cfg, char *text, size_t size);
bool can_config_equal(const can_config_t *a, const can_config_t *b);
int can_config_load(const char *path, can_config_t *cfg, char *error, size_t size);
int can_config_save(const char *path, const can_config_t *cfg, char *error, size_t size);
