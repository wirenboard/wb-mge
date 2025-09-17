# WB-MGE QEMU Emulation Guide

## 🚀 Quick Start

Install QEMU:
https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/tools/qemu.html

Run the WB-MGE firmware in QEMU with web access:

```bash
./run_qemu_with_web.sh
```

**Web Interface:** http://localhost:8080
**Login / Password:** admin / admin

## 📋 What It Does

The script automatically:
1. Loads ESP-IDF environment
2. Builds the project with QEMU configuration
3. Generates QEMU flash image
4. Kills any existing QEMU processes
5. Starts QEMU with port forwarding (localhost:8080 → ESP32:80)

## 🔧 Key Implementation Details

### QEMU-Specific Files
- `main/ethernet_qemu.c` - OpenEth driver for QEMU networking
- `main/wifi_qemu_mock.c` - WiFi functionality mock
- `main/hardware_mocks_qemu.c` - Hardware component mocks
- `sdkconfig.qemu.minimal` - QEMU-compatible configuration

### Critical Configuration Changes
```
CONFIG_ESPTOOLPY_FLASHMODE_DIO=y          # QEMU requires DIO, not QIO
CONFIG_SPI_FLASH_BROWNOUT_RESET=n         # Disable hardware-specific feature
CONFIG_SPI_FLASH_DANGEROUS_WRITE_ALLOWED=y # Allow QEMU flash operations
CONFIG_ETH_USE_OPENETH=y                  # Enable OpenEth for QEMU
```

### Networking in QEMU
- **Ethernet:** OpenEth driver provides network connectivity
- **WiFi:** Mocked (no actual WiFi in QEMU)
- **IP Address:** Assigned via DHCP (typically 10.0.2.15)
- **Port Forwarding:** localhost:8080 forwards to ESP32 port 80

## 🛠️ Manual QEMU Commands

If you need to run QEMU manually:

```bash
# Generate flash image
source /Users/radmir/esp/v5.4.1/esp-idf/export.sh
CONFIG_ETH_USE_OPENETH=1 idf.py build
cd build
python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin \
  --fill-flash-size=4MB --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x1000 bootloader/bootloader.bin 0x10000 qemu_mge.bin 0x8000 partition_table/partition-table.bin

# Run QEMU with port forwarding
~/.espressif/tools/qemu-xtensa/esp_develop_9.0.0_20240606/qemu/bin/qemu-system-xtensa \
  -M esp32 -m 4M \
  -drive file=qemu_flash.bin,if=mtd,format=raw \
  -drive file=qemu_efuse.bin,if=none,format=raw,id=efuse \
  -global driver=nvram.esp32.efuse,property=drive,value=efuse \
  -global driver=timer.esp32.timg,property=wdt_disable,value=true \
  -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8080-:80 \
  -nographic -serial mon:stdio
```

## 🌐 Web Interface Features

Once running, access these endpoints:
- **Main Interface:** http://localhost:8080
- **System Info:** http://localhost:8080/info
- **Settings:** http://localhost:8080/settings
- **WiFi Scan Start:** http://localhost:8080/wifi_scan/start
- **WiFi Scan Results:** http://localhost:8080/wifi_scan/results
- **Firmware Update:** http://localhost:8080/update

## 🔍 Troubleshooting

### No Web Access
1. Check QEMU is running with port forwarding: `ps aux | grep qemu`
2. Verify port forwarding in command: should include `hostfwd=tcp:127.0.0.1:8080-:80`
3. Check ESP32 got IP address in QEMU logs: `eth ip: 10.0.2.15`

### QEMU Won't Start
1. Ensure ESP-IDF environment is loaded
2. Verify `qemu_flash.bin` exists in build directory
3. Check QEMU binary path in script

### Build Errors
1. Use QEMU configuration: `cp sdkconfig.qemu.minimal sdkconfig`
2. Clean and rebuild: `idf.py clean && CONFIG_ETH_USE_OPENETH=1 idf.py build`

## ⚡ Performance Notes

- QEMU emulation is slower than real hardware
- Network operations work but with higher latency
- Hardware-specific features (GPIO expander, voltage monitoring) are mocked
- RS485 ports are present but not functional in QEMU
