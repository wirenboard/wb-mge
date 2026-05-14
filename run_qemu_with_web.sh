#!/bin/bash

# WB-MGE QEMU with Web Server Port Forwarding
# This script starts QEMU with port forwarding: localhost:8080 -> ESP32:80, localhost:50504 -> ESP32:50504

set -e

echo "🚀 Starting WB-MGE in QEMU with web server port forwarding..."
echo "📦 Loading ESP-IDF environment..."

# Load ESP-IDF environment
if [ -n "$IDF_PATH" ]; then
    echo "📦 Using existing ESP-IDF environment: $IDF_PATH"
else
    echo "📦 Loading ESP-IDF environment..."
    # Try to find ESP-IDF installation automatically
    if [ -f "$HOME/esp/v5.4/esp-idf/export.sh" ]; then
        source "$HOME/esp/v5.4/esp-idf/export.sh"
    elif [ -f "$HOME/esp-idf/export.sh" ]; then
        source "$HOME/esp-idf/export.sh"
    elif [ -f "/opt/esp-idf/export.sh" ]; then
        source "/opt/esp-idf/export.sh"
    else
        echo "❌ ESP-IDF not found. Please install ESP-IDF or set IDF_PATH"
        echo "Expected locations:"
        echo "  - $HOME/esp/v5.4/esp-idf/export.sh"
        echo "  - $HOME/esp-idf/export.sh" 
        echo "  - /opt/esp-idf/export.sh"
        exit 1
    fi
fi

echo "🔧 Building QEMU configuration..."

echo "🏗️  Building project..."

# Build the project with QEMU-specific config defaults applied on top of base config
CONFIG_ETH_USE_OPENETH=1 "$IDF_PATH/tools/idf.py" -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" build

# Generate QEMU flash image using esptool with flash_args (same method as idf.py qemu)
echo "💾 Generating QEMU flash image (qemu_flash.bin)..."
(cd build && python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin --fill-flash-size=4MB @flash_args)

# Verify the flash image was created
if [ ! -f "build/qemu_flash.bin" ]; then
    echo "❌ QEMU flash image not found after generation"
    exit 1
fi
echo "✅ QEMU flash image ready"

# Create ESP32 rev3 eFuse image if it does not exist (QEMU requires this 124-byte file)
# Default eFuse values taken from ESP-IDF qemu_ext.py for chip revision 3
if [ ! -f "build/qemu_efuse.bin" ]; then
    echo "💾 Creating ESP32 eFuse image (build/qemu_efuse.bin)..."
    python3 -c "import binascii,sys; sys.stdout.buffer.write(binascii.unhexlify('00000000000000000000000000800000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'))" > build/qemu_efuse.bin
fi

# Kill any existing QEMU processes to avoid conflicts
echo "🧹 Cleaning up any existing QEMU processes..."
pkill -f qemu-system-xtensa 2>/dev/null || true
pkill -f "idf.py qemu" 2>/dev/null || true
sleep 1

echo "🌐 Starting QEMU with port forwarding (localhost:8080 -> ESP32:80, localhost:50504 -> ESP32:50504)..."

# Find the QEMU binary in ESP-IDF
QEMU_BIN=""
# Check in the espressif tools directory (auto-detect user home).
# The IDF tools manager places qemu-xtensa under tools/tools/ on some installs.
for ESPRESSIF_TOOLS in "$HOME/.espressif/tools/tools" "$HOME/.espressif/tools"; do
    if [ -d "$ESPRESSIF_TOOLS/qemu-xtensa" ]; then
        # Find the most recent QEMU installation
        QEMU_VERSION_DIR=$(find "$ESPRESSIF_TOOLS/qemu-xtensa" -name "esp_develop_*" -type d | sort -V | tail -1)
        if [ -f "$QEMU_VERSION_DIR/qemu/bin/qemu-system-xtensa" ]; then
            QEMU_BIN="$QEMU_VERSION_DIR/qemu/bin/qemu-system-xtensa"
            break
        fi
    fi
done

# Fallback to ESP-IDF tools directory
if [ -z "$QEMU_BIN" ] && [ -f "$IDF_PATH/tools/qemu/esp-xtensa/bin/qemu-system-xtensa" ]; then
    QEMU_BIN="$IDF_PATH/tools/qemu/esp-xtensa/bin/qemu-system-xtensa"
elif [ -z "$QEMU_BIN" ] && [ -f "$IDF_PATH/tools/qemu/esp-develop/bin/qemu-system-xtensa" ]; then
    QEMU_BIN="$IDF_PATH/tools/qemu/esp-develop/bin/qemu-system-xtensa"
fi

# Final fallback to PATH
if [ -z "$QEMU_BIN" ] && command -v qemu-system-xtensa >/dev/null 2>&1; then
    QEMU_BIN="qemu-system-xtensa"
fi

if [ -z "$QEMU_BIN" ]; then
    echo "❌ QEMU binary not found. Using idf.py method instead..."
    echo "🔧 Note: This method won't have port forwarding built-in"
    echo "⚠️  Web access will be via the ESP32's IP address, not localhost:8080"
    CONFIG_ETH_USE_OPENETH=1 "$IDF_PATH/tools/idf.py" -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor
    exit 0
fi

echo "✅ Found QEMU at: $QEMU_BIN"

# Start QEMU with port forwarding
echo "🔧 Starting QEMU with port forwarding: localhost:8080 -> ESP32:80"
echo "🔧 QEMU command: $QEMU_BIN"
echo "🌐 Web server will be accessible at: http://localhost:8080 once ESP32 boots"
echo "🔧 Press Ctrl-A x to exit QEMU"
echo ""

$QEMU_BIN \
    -M esp32 \
    -m 4M \
    -drive file=build/qemu_flash.bin,if=mtd,format=raw \
    -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse \
    -global driver=nvram.esp32.efuse,property=drive,value=efuse \
    -global driver=timer.esp32.timg,property=wdt_disable,value=true \
    -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8080-:80,hostfwd=tcp:127.0.0.1:50504-:50504 \
    -nographic \
    -serial mon:stdio || {
    echo "❌ Direct QEMU launch failed. Trying idf.py qemu monitor..."
    echo "🔧 Note: This method won't have port forwarding - you'll need to find the ESP32 IP"
    CONFIG_ETH_USE_OPENETH=1 "$IDF_PATH/tools/idf.py" -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor
}
