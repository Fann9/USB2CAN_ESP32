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
