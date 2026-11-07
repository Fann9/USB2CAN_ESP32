/* SPDX-License-Identifier: CC0-1.0 */

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "serial_shell.h"
#include "flash_files.h"
#include "can_config_shell.h"
#include "can_bus.h"
#include "wifi_console.h"

void app_main(void)
{
    can_config_boot(flash_files_mount());
    can_config_t config=can_config_active();
    (void)can_bus_start(&config);
    (void)wifi_console_start();
    ESP_ERROR_CHECK(serial_shell_start());
    /* Shell owns the console now; avoid a main-task exit log over the prompt. */
    vTaskDelete(NULL);
}
