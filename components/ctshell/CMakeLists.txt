# Core-only ESP-IDF adapter; UART/task ownership belongs to the application.
idf_component_register(
    SRCS "ctshell.c"
    INCLUDE_DIRS "."
    LDFRAGMENTS "port/esp32/linker.lf"
    WHOLE_ARCHIVE
)
target_link_libraries(${COMPONENT_LIB} INTERFACE
    "-T ${CMAKE_CURRENT_SOURCE_DIR}/port/esp32/ctshell_cmd.ld")
