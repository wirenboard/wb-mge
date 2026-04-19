#!/bin/bash
# Flash and monitor ESP32 with persistent log capture
# Usage: ./flash_monitor.sh [/dev/ttyACM0]
set -e

PORT=${1:-/dev/ttyACM0}
LOGDIR="$(dirname "$0")/../logs"
mkdir -p "$LOGDIR"
LOGFILE="$LOGDIR/esp32_$(date +%Y%m%d_%H%M%S).log"

cd "$(dirname "$0")/.."
export IDF_PATH="${IDF_PATH:-$(pwd)/_tools/esp-idf}"
source "$IDF_PATH/export.sh" > /dev/null 2>&1

echo "=== Flashing to $PORT ==="
idf.py -p "$PORT" \
    -DDEVICE_SIGNATURE=mge_v3 \
    -DMODEL_DEFINE=MODEL_mge_v3 \
    -DFIRMWARE_VERSION=1.2.0-knx \
    -DTARGET_PROJECT_NAME=wb_mge \
    -DFIRMWARE_GIT_INFO=knx-ipsecure \
    -DINTERNAL_BUILD=1 \
    flash

echo "=== Monitoring (log: $LOGFILE) ==="
echo "Press Ctrl+] to exit"
idf.py -p "$PORT" monitor 2>&1 | tee "$LOGFILE"
