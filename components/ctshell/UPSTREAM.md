# ctshell source snapshot

- Repository: https://github.com/MDLZCOOL/ctshell
- Commit: `2f48ab46c24b6a1e0ba094615278bd05c37e004c`
- Declared version: 1.1.0
- License: Apache-2.0, see LICENSE.

Vendored core, Kconfig, version and ESP32 linker files only. This project supplies
its own CMake integration and UART/task adapter in main/serial_shell.c.
No dependency on the sibling checkout is required to build.

Local changes:

1. ctshell_config.h includes sdkconfig.h under ESP_PLATFORM so all translation
   units agree on Kconfig-derived buffer sizes and the prompt.
2. ctshell.c clears sigint when cancelling an idle input line. Otherwise the
   next command calling ctshell_check_abort can inherit the old Ctrl+C signal.
3. The ESP-IDF component links command sections with KEEP and WHOLE_ARCHIVE.
4. CTSHELL_EXPORT_SUBCMD preserves CTSHELL_ALIGN on the parent's extern
   declaration. GCC on the host otherwise increases the parent's alignment,
   introducing holes in a table that the library iterates as a packed array.

Select the CUSTOM platform port. Filesystem backends and upstream platform ports
are not included in this snapshot. Application commands must return promptly;
long-running work belongs in worker tasks. ctshell_delay is a busy-wait and is
not used by the application.
