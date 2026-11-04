#!/usr/bin/env bash
# Load the ESP-IDF environment used by this project.
# Usage: source ./export_idf.sh

if [[ "${BASH_SOURCE[0]}" == "$0" ]]; then
    echo "Please source this script: source ./export_idf.sh" >&2
    exit 1
fi

source /home/al1s/git/esp/esp-idf/export.sh
