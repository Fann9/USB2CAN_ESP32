#include <inttypes.h>
#include "app_info.h"
#include "can_bus.h"
#include "ctshell.h"
#include "esp_chip_info.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static int command_ping(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) {
        ctshell_printf("Usage: ping\r\n");
        return 1;
    }
    ctshell_printf("PONG!\r\n");
    return 0;
}
CTSHELL_EXPORT_CMD(ping, command_ping, "Reply PONG!", CTSHELL_ATTR_NONE);

static int command_version(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) {
        ctshell_printf("Usage: version\r\n");
        return 1;
    }
    ctshell_printf("USB2CAN %s\r\n", USB2CAN_VERSION);
    ctshell_printf("ESP-IDF %s\r\n", esp_get_idf_version());
    ctshell_printf("ctshell %s\r\n", USB2CAN_CTSHELL_VERSION);
    return 0;
}
CTSHELL_EXPORT_CMD(version, command_version, "Show firmware and library versions", CTSHELL_ATTR_NONE);

CTSHELL_EXPORT_CMD(sys, NULL, "System information", CTSHELL_ATTR_MENU);

static int command_info(int argc, char *argv[])
{
    (void)argv;
    if (argc != 1) {
        ctshell_printf("Usage: sys info\r\n");
        return 1;
    }
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    ctshell_printf("CPU cores: %u; shell core: %d\r\n", chip.cores, xPortGetCoreID());
    ctshell_printf("Uptime: %" PRIi64 " ms\r\n", esp_timer_get_time() / 1000);
    ctshell_printf("Free heap: %" PRIu32 " bytes\r\n", esp_get_free_heap_size());
    ctshell_printf("Minimum free heap: %" PRIu32 " bytes\r\n", esp_get_minimum_free_heap_size());
    ctshell_printf("CAN: TWAI %s, worker core 1\r\n",can_bus_running()?"running":"stopped");
    return 0;
}
CTSHELL_EXPORT_SUBCMD(sys, info, command_info, "Show CPU, uptime and heap usage");
