#pragma once
#include <stdbool.h>
#include "ctshell.h"
#ifndef FLASH_FILES_ROOT
#define FLASH_FILES_ROOT "/flash"
#endif
bool flash_files_mount(void);
bool flash_files_ready(void);
void flash_files_attach(ctshell_ctx_t *ctx);
