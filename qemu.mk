#######################################
# QEMU targets
#######################################

# Python interpreter for running api_tests pytest suite.
# Prefer the local .venv (developer workflow); fall back to the Docker image venv (CI/Jenkins).
PYTEST_PYTHON ?= $(shell [ -f api_tests/.venv/bin/python ] && echo api_tests/.venv/bin/python || echo /opt/api_tests_venv/bin/python)

# Helper: find QEMU binary path. Sets $$QEMU_BIN variable in shell context.
# Used by qemu-web, qemu-bin-path, and Python test harness (via make -s qemu-bin-path).
define find_qemu_bin
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
	if [ -z "$$QEMU_BIN" ] && command -v qemu-system-xtensa >/dev/null 2>&1; then \
	    QEMU_BIN="qemu-system-xtensa"; \
	fi
endef

# QEMU targets
# qemu-build: full build including frontend (use for initial/explicit QEMU builds).
# qemu-create-flash-image depends on build-idf-project-qemu so that qemu-test always
# compiles QEMU firmware (incremental, fast when nothing changed) before packaging.
# If the last build was hardware, build-idf-project-qemu detects the stale cache and
# runs fullclean automatically, then rebuilds with the correct QEMU config.
qemu-build: build-frontend build-idf-project-qemu

# Apply patches to ESP-IDF sources required for QEMU builds.
# Patches live in patches/ and are idempotent: re-running is safe if already applied.
# This target must run before any QEMU firmware compile so that the patched IDF
# sources are compiled in, regardless of whether the build runs locally or in Docker.
qemu-apply-idf-patches:
	@echo "Applying IDF patches for QEMU build..."
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug04-openeth-isr-dram-log.patch
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug05-lact-timer-null-isr-guard.patch

build-idf-project-qemu: qemu-apply-idf-patches
	@echo "Building for QEMU with OpenEth ethernet driver"
	@# Detect stale hardware build cache: if CMakeCache exists but was not a QEMU build,
	@# run fullclean to force CMake reconfiguration with correct source file selection.
	@# No need to rm sdkconfig — each build type uses its own sdkconfig file.
	@if [ -f "build/CMakeCache.txt" ]; then \
	    if ! grep -q "qemu_mge" "build/CMakeCache.txt"; then \
	        echo "Detected hardware build cache — running fullclean before QEMU build..."; \
	        $(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build fullclean; \
	    fi; \
	fi
	@$(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" $(addprefix -D, $(DEFS)) build

qemu-create-flash-image: build-idf-project-qemu
	@echo "Generating QEMU flash image..."
	@$(EIM_ACTIVATE) && cd build && python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin --fill-flash-size=4MB @flash_args
	@test -f build/qemu_flash.bin || { echo "QEMU flash image not found after generation"; exit 1; }
	@echo "QEMU flash image ready"

qemu-create-efuse-image:
	@# Create ESP32 rev3 eFuse image if it does not exist (QEMU requires this 124-byte file).
	@# Default eFuse values taken from ESP-IDF qemu_ext.py for chip revision 3.
	@mkdir -p build
	@if [ ! -f "build/qemu_efuse.bin" ]; then \
	    echo "Creating ESP32 eFuse image (build/qemu_efuse.bin)..."; \
	    python3 -c "import binascii,sys; sys.stdout.buffer.write(binascii.unhexlify('00000000000000000000000000800000000000000000100000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000'))" > build/qemu_efuse.bin; \
	fi

qemu-monitor:
	@echo "Starting QEMU monitor..."
	@$(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" monitor

qemu-run: qemu-create-flash-image qemu-create-efuse-image
	@echo "Running in QEMU..."
	@$(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor

qemu-web: qemu-create-flash-image qemu-create-efuse-image
	@echo "Running QEMU with web server port forwarding..."
	@echo "Web interface will be available at: http://localhost:8080"
	@{ \
	    echo "Cleaning up any existing QEMU processes..."; \
	    pkill -f qemu-system-xtensa 2>/dev/null || true; \
	    pkill -f "idf.py qemu" 2>/dev/null || true; \
	    sleep 1; \
	    $(find_qemu_bin); \
	    if [ -z "$$QEMU_BIN" ]; then \
	        echo "QEMU binary not found. Using idf.py method instead..."; \
	        echo "Note: This method will not have port forwarding built-in"; \
	        $(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor; \
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
	                $(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor; \
	            }; \
	    fi; \
	}

qemu-bin-path:
	@{ \
	    $(find_qemu_bin); \
	    if [ -z "$$QEMU_BIN" ]; then \
	        echo "ERROR: QEMU binary not found" >&2; \
	        exit 1; \
	    fi; \
	    echo "$$QEMU_BIN"; \
	}

# Run API tests against QEMU.
# qemu-create-flash-image depends on build-idf-project-qemu, so the correct QEMU
# firmware is always compiled (incremental) before packaging and running tests.
qemu-test: qemu-create-flash-image qemu-create-efuse-image
	cd api_tests && $(PYTEST_PYTHON) -m pytest --qemu --qemu-skip-build $(PYTEST_ARGS)

# Help target for QEMU
qemu-help:
	@echo "QEMU Targets:"
	@echo "  qemu-apply-idf-patches  - Apply IDF source patches for QEMU builds (called automatically; idempotent)"
	@echo "  qemu-build              - Build frontend + QEMU firmware (run once or after code changes)"
	@echo "  qemu-create-flash-image - Compile QEMU firmware (incremental) and merge into qemu_flash.bin"
	@echo "  qemu-create-efuse-image - Create eFuse image build/qemu_efuse.bin if missing (idempotent)"
	@echo "  qemu-run                - Run QEMU in basic mode (no port forwarding)"
	@echo "  qemu-web                - Run QEMU with web UI at localhost:8080"
	@echo "  qemu-monitor            - Attach monitor to already-running QEMU (no build)"
	@echo "  qemu-bin-path           - Print path to qemu-system-xtensa binary"
	@echo "  qemu-test               - Build QEMU firmware, flash image, and run API pytest suite"
	@echo "  qemu-clean              - Remove build/ and sdkconfig.qemu_build"
	@echo ""
	@echo "Build:       make qemu-build"
	@echo "Quick start: make qemu-web"
	@echo "Run tests:   make qemu-test"
	@echo "One test:    make qemu-test PYTEST_ARGS=\"-k test_auth\""

qemu-clean:
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" fullclean
	@rm -rf build
	@rm -f sdkconfig.qemu_build sdkconfig.qemu_build.old

.PHONY: qemu-build qemu-apply-idf-patches build-idf-project-qemu qemu-create-flash-image qemu-create-efuse-image qemu-monitor qemu-run qemu-web qemu-bin-path qemu-test qemu-help qemu-clean
