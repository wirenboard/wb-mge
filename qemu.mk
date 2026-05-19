#######################################
# QEMU targets
#######################################

# QEMU targets
build-qemu: build-frontend build-idf-project-qemu

build-idf-project-qemu:
	@echo "Building for QEMU with OpenEth ethernet driver"
	@# Detect stale hardware build cache: if CMakeCache exists but was not a QEMU build,
	@# run fullclean to force CMake reconfiguration with correct source file selection
	@if [ -f "build/CMakeCache.txt" ]; then \
	    if ! grep -q "qemu_mge" "build/CMakeCache.txt"; then \
	        echo "Detected hardware build cache — running fullclean before QEMU build..."; \
	        $(IDF_PY) fullclean; \
	    fi; \
	fi
	CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" $(addprefix -D, $(DEFS)) build

qemu-flash-image: build-qemu
	@echo "Generating QEMU flash image..."
	cd build && python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin --fill-flash-size=4MB @flash_args
	@test -f build/qemu_flash.bin || { echo "QEMU flash image not found after generation"; exit 1; }
	@echo "QEMU flash image ready"

qemu-efuse-image:
	@# Create ESP32 rev3 eFuse image if it does not exist (QEMU requires this 124-byte file).
	@# Default eFuse values taken from ESP-IDF qemu_ext.py for chip revision 3.
	@mkdir -p build
	@if [ ! -f "build/qemu_efuse.bin" ]; then \
	    echo "Creating ESP32 eFuse image (build/qemu_efuse.bin)..."; \
	    python3 -c "import binascii,sys; sys.stdout.buffer.write(binascii.unhexlify('00000000000000000000000000800000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'))" > build/qemu_efuse.bin; \
	fi

qemu-monitor: build-qemu
	@echo "Starting QEMU monitor..."
	CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" monitor

qemu-run: qemu-flash-image
	@echo "Running in QEMU..."
	CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor

qemu-web: qemu-flash-image qemu-efuse-image
	@echo "Running QEMU with web server port forwarding..."
	@echo "Web interface will be available at: http://localhost:8080"
	@{ \
	    echo "Cleaning up any existing QEMU processes..."; \
	    pkill -f qemu-system-xtensa 2>/dev/null || true; \
	    pkill -f "idf.py qemu" 2>/dev/null || true; \
	    sleep 1; \
	    QEMU_BIN=""; \
	    for ESPRESSIF_TOOLS in "$(HOME)/.espressif/tools/tools" "$(HOME)/.espressif/tools"; do \
	        if [ -d "$$ESPRESSIF_TOOLS/qemu-xtensa" ]; then \
	            QEMU_VERSION_DIR=$$(find "$$ESPRESSIF_TOOLS/qemu-xtensa" -name "esp_develop_*" -type d | sort -V | tail -1); \
	            if [ -f "$$QEMU_VERSION_DIR/qemu/bin/qemu-system-xtensa" ]; then \
	                QEMU_BIN="$$QEMU_VERSION_DIR/qemu/bin/qemu-system-xtensa"; \
	                break; \
	            fi; \
	        fi; \
	    done; \
	    if [ -z "$$QEMU_BIN" ] && [ -f "$(IDF_PATH)/tools/qemu/esp-xtensa/bin/qemu-system-xtensa" ]; then \
	        QEMU_BIN="$(IDF_PATH)/tools/qemu/esp-xtensa/bin/qemu-system-xtensa"; \
	    elif [ -z "$$QEMU_BIN" ] && [ -f "$(IDF_PATH)/tools/qemu/esp-develop/bin/qemu-system-xtensa" ]; then \
	        QEMU_BIN="$(IDF_PATH)/tools/qemu/esp-develop/bin/qemu-system-xtensa"; \
	    fi; \
	    if [ -z "$$QEMU_BIN" ] && command -v qemu-system-xtensa >/dev/null 2>&1; then \
	        QEMU_BIN="qemu-system-xtensa"; \
	    fi; \
	    if [ -z "$$QEMU_BIN" ]; then \
	        echo "QEMU binary not found. Using idf.py method instead..."; \
	        echo "Note: This method will not have port forwarding built-in"; \
	        CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor; \
	    else \
	        echo "Found QEMU at: $$QEMU_BIN"; \
	        echo "Starting QEMU with port forwarding: localhost:8080 -> ESP32:80, localhost:50504 -> ESP32:50504"; \
	        echo "Press Ctrl-A x to exit QEMU"; \
	        $$QEMU_BIN \
	            -M esp32 \
	            -m 4M \
	            -drive file=build/qemu_flash.bin,if=mtd,format=raw \
	            -drive file=build/qemu_efuse.bin,if=none,format=raw,id=efuse \
	            -global driver=nvram.esp32.efuse,property=drive,value=efuse \
	            -global driver=timer.esp32.timg,property=wdt_disable,value=true \
	            -nic user,model=open_eth,hostfwd=tcp:127.0.0.1:8080-:80,hostfwd=tcp:127.0.0.1:50504-:50504 \
	            -nographic \
	            -serial mon:stdio || { \
	                echo "Direct QEMU launch failed. Trying idf.py qemu monitor..."; \
	                echo "Note: This method will not have port forwarding - you will need to find the ESP32 IP"; \
	                CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor; \
	            }; \
	    fi; \
	}

# Help target for QEMU
qemu-help:
	@echo "QEMU Targets:"
	@echo "  build-qemu        - Build project for QEMU emulation"
	@echo "  qemu-flash-image  - Generate QEMU flash image"
	@echo "  qemu-efuse-image  - Create ESP32 eFuse image (build/qemu_efuse.bin)"
	@echo "  qemu-run          - Run in QEMU (basic mode, no port forwarding)"
	@echo "  qemu-web          - Run in QEMU with web access (localhost:8080)"
	@echo "  qemu-monitor      - Run QEMU with monitor only"
	@echo ""
	@echo "Quick start: make qemu-web"

clean-qemu:
	$(IDF_PY) -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" fullclean
	rm -rf build sdkconfig

.PHONY: build-qemu build-idf-project-qemu qemu-flash-image qemu-efuse-image qemu-monitor qemu-run qemu-web qemu-help clean-qemu
