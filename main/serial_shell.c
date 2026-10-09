#include "serial_shell.h"
#include "shell_input.h"
#include "app_info.h"
#include "flash_files.h"
#include "can_bus.h"

#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#if !CONFIG_ESP_CONSOLE_UART
#error "USB2CAN shell requires the UART console"
#endif

enum { SHELL_CORE = 0, SHELL_STACK_BYTES = 8192, SHELL_PRIORITY = 4 };
static ctshell_ctx_t shell_ctx;
static TaskHandle_t shell_task_handle;
static const uart_port_t shell_uart = CONFIG_ESP_CONSOLE_UART_NUM;
static bool echo_active;

bool serial_shell_can_echo(uint16_t id)
{
    if (!can_bus_echo_start(id)) return false;
    echo_active=true;
    ctshell_printf("Streaming /0x%03X; press Ctrl+C to stop\r\n",id);
    return true;
}

static void print_echo_frames(void)
{
    twai_message_t frame;
    for (unsigned n=0;n<8 && can_bus_echo_receive(&frame);++n) {
        ctshell_printf("STD ID 0x%08lX DLC %u",(unsigned long)frame.identifier,
            frame.data_length_code);
        if (frame.rtr) ctshell_printf(" RTR");
        else for (unsigned i=0;i<frame.data_length_code && i<8;++i)
            ctshell_printf(" %02X",frame.data[i]);
        ctshell_printf("\r\n");
    }
}

static const char startup_banner[] =
    "\r\n"
    "    _     _      _   ____      _____  ____   ____  \r\n"
    "   / \\   | |    / | / ___|    | ____|/ ___| |  _ \\ \r\n"
    "  / _ \\  | |    | | \\___ \\    |  _|  \\___ \\ | |_) |\r\n"
    " / ___ \\ | |___ | |  ___) |   | |___  ___) ||  __/ \r\n"
    "/_/   \\_\\|_____||_| |____/    |_____||____/ |_|    \r\n"
    "Version: " USB2CAN_VERSION "\r\n"
    "ctshell " USB2CAN_CTSHELL_VERSION "\r\n";

static void shell_write(const char *data, uint16_t size, void *priv)
{
    (void)priv;
    /* TX buffering copies data before ctshell's temporary buffer is released. */
    int written = uart_write_bytes(shell_uart, data, size);
    ESP_ERROR_CHECK(written == size ? ESP_OK : ESP_FAIL);
}

static uint32_t shell_ticks(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

static void shell_task(void *arg)
{
    (void)arg;
    QueueHandle_t events;
    const uart_config_t config = {
        .baud_rate = CONFIG_ESP_CONSOLE_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    /* Install on CPU0 so both the UART interrupt and shell stay on CPU0.
     * Keep the existing console pins, including a possible custom assignment. */
    ESP_ERROR_CHECK(uart_param_config(shell_uart, &config));
    ESP_ERROR_CHECK(uart_driver_install(shell_uart, 2048, 2048, 20, &events, 0));
    uart_vfs_dev_use_driver(shell_uart);

    int banner_size = sizeof(startup_banner) - 1;
    ESP_ERROR_CHECK(uart_write_bytes(shell_uart, startup_banner, banner_size) == banner_size
                        ? ESP_OK
                        : ESP_FAIL);

    const ctshell_io_t io = { .write = shell_write, .get_tick = shell_ticks };
    ctshell_init(&shell_ctx, io, NULL);
    flash_files_attach(&shell_ctx);
    shell_input_t input = { .ctx = &shell_ctx };
    uart_event_t event;
    uint8_t data[64];

    while (true) {
        if (xQueueReceive(events, &event, echo_active ? pdMS_TO_TICKS(10) : portMAX_DELAY) != pdTRUE) {
            if (echo_active) print_echo_frames();
            continue;
        }
        if (event.type == UART_DATA) {
            size_t remaining = event.size;
            while (remaining > 0) {
                size_t count = remaining < sizeof(data) ? remaining : sizeof(data);
                int received = uart_read_bytes(shell_uart, data, count, 0);
                if (received <= 0) {
                    break;
                }
                remaining -= (size_t)received;
                for (int i = 0; i < received; ++i) {
                    if (echo_active) {
                        if (data[i]==CTSHELL_KEY_CTRL_C) {
                            echo_active=false;
                            can_bus_echo_stop();
                            ctshell_printf("\r\nEcho stopped; dropped %lu frame(s)\r\n",
                                (unsigned long)can_bus_echo_dropped());
                            shell_input_cancel(&input);
                            /* Ignore bytes following Ctrl+C in this UART chunk. */
                            remaining=0;
                            break;
                        }
                        continue;
                    }
                    shell_input_feed(&input, data[i]);
                    if (echo_active) ctshell_printf("\r\033[2K");
                }
            }
        } else if (event.type == UART_FIFO_OVF || event.type == UART_BUFFER_FULL ||
                   event.type == UART_PARITY_ERR || event.type == UART_FRAME_ERR) {
            ESP_ERROR_CHECK(uart_flush_input(shell_uart));
            xQueueReset(events);
            if (echo_active) { echo_active=false; can_bus_echo_stop(); }
            ctshell_printf("\r\nUART receive error; input discarded.\r\n");
            shell_input_cancel(&input);
        }
        if (echo_active) print_echo_frames();
    }
}

esp_err_t serial_shell_start(void)
{
    if (shell_task_handle != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return xTaskCreatePinnedToCore(shell_task, "ctshell", SHELL_STACK_BYTES,
                                   NULL, SHELL_PRIORITY, &shell_task_handle,
                                   SHELL_CORE) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM;
}
