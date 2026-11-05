#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "ctshell.h"
#include "shell_input.h"

static ctshell_ctx_t ctx;
static shell_input_t input;
static char output[32768];
static size_t used;
static int calls;
static char last_arg[128];

static void capture(const char *data, uint16_t size, void *priv)
{
    (void)priv;
    assert(used + size < sizeof(output));
    memcpy(output + used, data, size);
    used += size;
    output[used] = 0;
}

static int probe(int argc, char **argv)
{
    ctshell_check_abort(&ctx);
    ++calls;
    snprintf(last_arg, sizeof(last_arg), "%s", argc > 1 ? argv[1] : "");
    return 0;
}
CTSHELL_EXPORT_CMD(probe, probe, "Test command", CTSHELL_ATTR_NONE);
CTSHELL_EXPORT_CMD(group, NULL, "Test group", CTSHELL_ATTR_MENU);
CTSHELL_EXPORT_SUBCMD(group, child, probe, "Test child");

static void reset(void)
{
    used = 0;
    output[0] = 0;
    calls = 0;
    last_arg[0] = 0;
    input = (shell_input_t){ .ctx = &ctx };
    ctshell_init(&ctx, (ctshell_io_t){ .write = capture }, NULL);
    assert(strstr(output, "al1s> "));
}

static void feed(const char *text)
{
    for (; *text; ++text) shell_input_feed(&input, (uint8_t)*text);
}

int main(void)
{
    reset();
    feed("probe one\r\n");
    assert(calls == 1 && strcmp(last_arg, "one") == 0);
    /* Exactly one prompt after CRLF. */
    const char *first = strstr(output, "al1s> ");
    const char *second = strstr(first + 1, "al1s> ");
    assert(second && !strstr(second + 1, "al1s> "));
    feed("probe two\n");
    assert(calls == 2 && strcmp(last_arg, "two") == 0);

    reset();
    feed("help\r");
    assert(strstr(output, "Available commands:") && strstr(output, "Test command"));
    feed("group child nested\r");
    assert(calls == 1 && strcmp(last_arg, "nested") == 0);

    reset();
    feed("pro\t\r");
    assert(calls == 1);
    feed("\033[A\r");
    assert(calls == 2);

    reset();
    feed("probX\177e abc\033[D\033[DZ\r");
    assert(calls == 1 && strcmp(last_arg, "aZbc") == 0);

    reset();
    feed("bad input\003probe ok\r");
    assert(calls == 1 && strcmp(last_arg, "ok") == 0 && ctx.sigint == 0);

    reset();
    feed("probe ");
    for (int i = 0; i < CONFIG_CTSHELL_LINE_BUF_SIZE; ++i) shell_input_feed(&input, 'x');
    feed("\r\n");
    assert(calls == 0 && strstr(output, "line discarded"));
    feed("probe recovered\r");
    assert(calls == 1 && strcmp(last_arg, "recovered") == 0);

    reset();
    feed("probe partial");
    shell_input_cancel(&input);
    feed("\rprobe fresh\r");
    assert(calls == 1 && strcmp(last_arg, "fresh") == 0);
    puts("PASS: prompt, registration, CR/LF/CRLF, help, subcommands, tab, history, editing, Ctrl+C, overflow and cancellation");
}
