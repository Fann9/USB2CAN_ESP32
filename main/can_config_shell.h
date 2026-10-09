#pragma once
#include <stdbool.h>
#include "can_config.h"
void can_config_boot(bool storage_ready);
can_config_t can_config_active(void);
