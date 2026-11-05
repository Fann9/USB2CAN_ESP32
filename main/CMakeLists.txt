idf_component_register(
    SRCS "hello_world_main.c" "serial_shell.c" "shell_input.c" "shell_commands.c"
    INCLUDE_DIRS "."
    PRIV_REQUIRES ctshell esp_driver_uart esp_timer esp_system esp_app_format vfs
    WHOLE_ARCHIVE
)
