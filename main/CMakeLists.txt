idf_component_register(
    SRCS "hello_world_main.c" "serial_shell.c" "shell_input.c" "shell_commands.c"
         "flash_files.c" "flash_mount.c" "can_config.c" "can_config_shell.c" "can_bus.c" "wifi_console.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES ctshell driver esp_driver_twai esp_driver_uart esp_timer esp_system esp_app_format vfs fatfs wear_levelling esp_partition esp_wifi esp_netif esp_event nvs_flash lwip
    WHOLE_ARCHIVE
)
