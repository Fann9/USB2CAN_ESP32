#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
test_dir="$(mktemp -d /tmp/usb2can-shell-test.XXXXXX)"
trap 'rm -rf "$test_dir"' EXIT
test -f "$project_dir/build/config/sdkconfig.h" || {
    echo 'Run idf.py build first (tests use the actual generated Kconfig settings).' >&2
    exit 1
}
cc -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-sign-compare \
    -DESP_PLATFORM -I"$project_dir/build/config" \
    -I"$project_dir/components/ctshell" -I"$project_dir/main" \
    "$project_dir/components/ctshell/ctshell.c" \
    "$project_dir/main/shell_input.c" "$project_dir/tests/shell_input_test.c" \
    -o "$test_dir/shell_input_test"
"$test_dir/shell_input_test"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Werror \
    -I"$project_dir/main" "$project_dir/main/can_config.c" \
    "$project_dir/tests/can_config_test.c" -Wl,--wrap=fsync -Wl,--wrap=rename \
    -o "$test_dir/can_config_test"
"$test_dir/can_config_test" "$test_dir"
cc -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra \
    -Wno-unused-parameter -Wno-sign-compare -DESP_PLATFORM -DCAN_BUS_HOST_TEST \
    -DCAN_CONFIG_PATH=\""$test_dir/shell.conf"\" \
    -DFLASH_FILES_ROOT=\""$test_dir"\" \
    -I"$project_dir/build/config" -I"$project_dir/components/ctshell" -I"$project_dir/main" \
    "$project_dir/components/ctshell/ctshell.c" "$project_dir/main/shell_input.c" \
    "$project_dir/main/can_config.c" "$project_dir/main/can_config_shell.c" \
    "$project_dir/main/flash_files.c" \
    "$project_dir/tests/can_config_shell_test.c" -o "$test_dir/can_config_shell_test"
"$test_dir/can_config_shell_test"
