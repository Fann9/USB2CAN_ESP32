#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "ctshell.h"

typedef struct {
    ctshell_ctx_t *ctx;
    bool previous_cr;
    bool discard_line;
} shell_input_t;

/* Called only by the shell task: no cross-core ctshell context access. */
void shell_input_feed(shell_input_t *input, uint8_t byte);
void shell_input_cancel(shell_input_t *input);
