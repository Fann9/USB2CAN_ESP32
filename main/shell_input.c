#include "shell_input.h"

void shell_input_cancel(shell_input_t *input)
{
    input->previous_cr = false;
    input->discard_line = false;
    input->ctx->dfa_state = CTSHELL_DFA_ROOT;
    ctshell_input(input->ctx, CTSHELL_KEY_CTRL_C);
    ctshell_poll(input->ctx);
}

void shell_input_feed(shell_input_t *input, uint8_t byte)
{
    /* IDF Monitor commonly sends CRLF; execute each line only once. */
    if (byte == '\n' && input->previous_cr) {
        input->previous_cr = false;
        return;
    }
    input->previous_cr = byte == '\r';

    if (input->discard_line) {
        if (byte == '\r' || byte == '\n' || byte == CTSHELL_KEY_CTRL_C) {
            shell_input_cancel(input);
            input->previous_cr = byte == '\r';
        }
        return;
    }

    /* Never execute the truncated prefix of an overlong command. */
    if (byte >= 32 && byte <= 126 &&
        input->ctx->dfa_state == CTSHELL_DFA_ROOT &&
        input->ctx->line_len >= CONFIG_CTSHELL_LINE_BUF_SIZE - 1) {
        ctshell_printf("\r\nInput too long; line discarded. Press Enter.\r\n");
        input->discard_line = true;
        return;
    }

    ctshell_input(input->ctx, (char)byte);
    ctshell_poll(input->ctx);
}
