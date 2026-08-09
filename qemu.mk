#######################################
# QEMU targets
#######################################

# Python interpreter for running api_tests pytest suite.
# Use absolute path so it remains valid after "cd api_tests" in qemu-test recipe.
# Prefer the local .venv (developer workflow); fall back to the Docker image venv (CI/Jenkins).
PYTEST_PYTHON ?= $(shell [ -f "$(CURDIR)/api_tests/.venv/bin/python" ] && echo "$(CURDIR)/api_tests/.venv/bin/python" || echo /opt/api_tests_venv/bin/python)

# Host ports for the QEMU launchers below, taken from api_tests/qemu_ports.py — the SAME
# module conftest.py builds its hostfwd/-serial arguments and its --ip default from.
#
# Why the Makefile shells out instead of keeping its own copy: it used to hardcode
# hostfwd 8080/50502-50504, -serial 5561/5562 and udp 5570, so once the test suite moved
# to slot-derived ports the documented "start QEMU with `make qemu-web`, then point pytest
# at it" flow pointed at ports nothing listened on. Deriving both sides from one module is
# what makes that flow correct for every slot instead of only for the legacy one.
#
# Plain `python3` (not PYTEST_PYTHON): qemu_ports imports nothing outside the stdlib, so it
# does not need the test venv — which may not exist yet on a fresh checkout.
#
# Recursively expanded (`=`, not `:=`) on purpose: this way the python is run only by the
# recipes that actually expand these, not on every `make <anything>` parse. They inherit
# make's environment, so `WB_MGE_PORT_SLOT=3 make qemu-web` picks the slot up.
QEMU_PORTS_PY = cd $(CURDIR)/api_tests && python3 -c
QEMU_NIC_ARG     = $(shell $(QEMU_PORTS_PY) "import qemu_ports; print(qemu_ports.qemu_nic_arg())")
QEMU_SERIAL_ARGS = $(shell $(QEMU_PORTS_PY) "import qemu_ports; print(qemu_ports.qemu_serial_args_str())")
QEMU_PORT_SUMMARY = $(shell $(QEMU_PORTS_PY) "import qemu_ports; print(qemu_ports.port_summary())")
QEMU_HTTP_PORT   = $(shell $(QEMU_PORTS_PY) "import qemu_ports; print(qemu_ports.HTTP_HOST_PORT)")

# Absolute path to this tree's flash image. Used as the -drive argument AND as the pattern
# that identifies "a QEMU belonging to THIS working tree" — api_tests/conftest.py matches
# the same string in the process list, and the pkill below is scoped with it.
QEMU_FLASH_IMAGE = $(CURDIR)/build/qemu_flash.bin
QEMU_EFUSE_IMAGE = $(CURDIR)/build/qemu_efuse.bin

# One e2e run per working tree, enforced from the MAKE side as well as from pytest.
#
# Why the make side is needed at all: conftest could only ever take the lock once pytest
# was running, and `qemu-test: qemu-create-flash-image qemu-create-efuse-image` does its
# damage BEFORE that — a second `make qemu-test` in the same tree rebuilds and rewrites
# build/qemu_flash.bin (esptool merge_bin --output=... truncates the same inode) underneath
# the first run's LIVE QEMU, and only then reaches the pytest that refuses it. The
# corruption the lock exists to prevent had already happened. So the public targets below
# hold the lock across the build AND the run, by delegating to a *-locked inner target.
#
# tree_lock.py rather than flock(1): flock(1) is util-linux and is NOT installed on macOS,
# where developers run these targets. A tiny Python wrapper (python3 is already required by
# QEMU_PORTS_PY above) gives every platform the same lock file, the same refusal message and
# the same semantics as conftest — it IS the module conftest uses. It exports
# WB_MGE_E2E_TREE_LOCK to the command it runs, so the nested `pytest --qemu` recognises the
# lock its own parent holds instead of refusing itself.
#
# If python3 is missing we WARN and run unlocked rather than refusing to work: an absent
# interpreter is not a reason to make `make qemu-test` unusable, and conftest still refuses
# a genuinely concurrent pytest (it is only the pre-pytest build window that goes unguarded).
TREE_LOCK_PY = $(CURDIR)/api_tests/tree_lock.py

# `make -n` must NOT take the lock. A recipe line that mentions $(MAKE) is executed even in
# dry-run mode (make treats it as recursion and lets it run so the sub-make can report), and
# the wrapper below is exactly such a line — so `make -n qemu-test` really created
# .e2e-tree.lock, and really exited 1 when another run held it, from a command whose entire
# contract is "print what you WOULD do".
#
# Both spellings are tested because the flag reaches a SUB-make (qemu-coverage recurses into
# qemu-test) as a separate word rather than inside the grouped short-option word: MAKEFLAGS
# is `n` for the outer make and ` --no-print-directory -n` for the inner one.
MAKE_DRY_RUN := $(findstring n,$(firstword -$(MAKEFLAGS)))$(filter -n --dry-run --just-print --recon,$(MAKEFLAGS))

ifeq (,$(MAKE_DRY_RUN))
define run_locked
	@if command -v python3 >/dev/null 2>&1; then \
	    python3 "$(TREE_LOCK_PY)" -- $(1); \
	else \
	    echo "WARNING: python3 not found — running WITHOUT the one-run-per-working-tree lock."; \
	    echo "         A second run in this tree would corrupt build/qemu_flash.bin under this one."; \
	    $(1); \
	fi
endef
else
# Dry run: recurse straight into the -locked target with no wrapper. make passes -n down
# through MAKEFLAGS, so the inner make still only prints its recipes.
define run_locked
	$(1)
endef
endif

# Helper: find QEMU binary path. Sets $$QEMU_BIN variable in shell context.
# Used by qemu-web, qemu-bin-path, and Python test harness (via make -s qemu-bin-path).
# Search order:
#   1. $(HOME)/.espressif/tools — standard local EIM install
#   2. /opt/esp/tools           — espressif/idf Docker image (IDF_TOOLS_PATH)
#   3. PATH                     — fallback for any other install method
define find_qemu_bin
	QEMU_BIN=""; \
	for ESPRESSIF_TOOLS in "$(HOME)/.espressif/tools/tools" "$(HOME)/.espressif/tools" "/opt/esp/tools"; do \
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
# check-idf-pins comes first (as for the hardware apply-idf-patches): under
# `make -j` an unordered prerequisite would let the patches land while the pin
# check is still running.
qemu-apply-idf-patches: check-idf-pins
	@echo "Applying IDF patches for QEMU build..."
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug04-openeth-isr-dram-log.patch
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug05-lact-timer-null-isr-guard.patch
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug06-uart-install-rxfifo-storm.patch

# check-idf-pins is listed explicitly, not left to qemu-apply-idf-patches: this is
# the entry point of the QEMU build (qemu-build, qemu-test, the Jenkins e2e stage),
# and it must verify the IDF pins just like the hardware build-idf-project does.
build-idf-project-qemu: check-idf-pins qemu-apply-idf-patches
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

qemu-run:
	$(call run_locked,$(MAKE) --no-print-directory qemu-run-locked)

qemu-run-locked: qemu-create-flash-image qemu-create-efuse-image
	@echo "Running in QEMU..."
	@$(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor

# Print the host ports this tree/slot uses, without starting anything. The one command to
# run before `pytest --ip ...` against an already-running QEMU, and what README_QEMU.md
# points at instead of quoting numbers that only hold for slot 0.
qemu-ports:
	@# Same guard as qemu-web, for the same reason: $$(shell ...) swallows a missing python3
	@# and a qemu_ports that raised on a bad WB_MGE_PORT_SLOT, so without this the target
	@# prints an empty line and exits 0 — while README_QEMU.md, api_tests/README_API_Tests.md
	@# and qemu-help all send the reader here as the source of truth for "which ports does my
	@# slot use". Captured into a shell variable so the summary is expanded — and python run —
	@# exactly once for both the guard and the echo. (Do NOT name that make variable with its
	@# expansion syntax in a recipe comment: make expands recipe comments too, so it would run
	@# python a second time just to build a line the shell then throws away.)
	@SUMMARY="$(QEMU_PORT_SUMMARY)"; \
	test -n "$$SUMMARY" || { \
	    echo "ERROR: could not derive the QEMU ports from api_tests/qemu_ports.py."; \
	    echo "       Run this to see the real error:"; \
	    echo "         cd $(CURDIR)/api_tests && python3 -c 'import qemu_ports; print(qemu_ports.port_summary())'"; \
	    exit 1; \
	}; \
	echo "$$SUMMARY"

# Holds the tree lock for as long as QEMU runs: this target rebuilds and rewrites
# build/qemu_flash.bin, and its pkill below would kill a concurrent e2e run's QEMU.
qemu-web:
	$(call run_locked,$(MAKE) --no-print-directory qemu-web-locked)

qemu-web-locked: qemu-create-flash-image qemu-create-efuse-image
	@# Fail loudly if the port derivation produced nothing (no python3, or qemu_ports raised
	@# on a bad WB_MGE_PORT_SLOT). $$(shell ...) swallows both, and an empty -nic argument
	@# would otherwise reach QEMU as a confusing syntax error with no hint of the cause.
	@test -n "$(QEMU_NIC_ARG)" || { \
	    echo "ERROR: could not derive the QEMU ports from api_tests/qemu_ports.py."; \
	    echo "       Run this to see the real error:"; \
	    echo "         cd $(CURDIR)/api_tests && python3 -c 'import qemu_ports; print(qemu_ports.port_summary())'"; \
	    exit 1; \
	}
	@echo "Running QEMU with web server port forwarding..."
	@echo "Web interface will be available at: http://localhost:$(QEMU_HTTP_PORT)"
	@{ \
	    echo "Cleaning up any existing QEMU processes for THIS tree..."; \
	    : "ONE pattern, and it names this tree's flash image. A bare 'pkill -f qemu-system-xtensa'" ; \
	    : "kills every parallel run on the machine, which is precisely the thing the port" ; \
	    : "slots exist to make possible; another tree's QEMU is none of our business." ; \
	    : "Anchored as QEMU spells the argument (file=<image>,if=mtd,...), not as a bare" ; \
	    : "path substring: an unanchored pattern also matches a foreign tree whose path" ; \
	    : "merely ENDS with ours (/w/mge inside /home/ci/w/mge)." ; \
	    : "A 'pkill -f \"idf.py qemu\"' used to sit here and was the cross-tree kill this" ; \
	    : "whole change claims to have removed: unscoped, and both qemu-run-locked and the" ; \
	    : "fallback below launch exactly that command line, so 'make qemu-web' here killed" ; \
	    : "'make qemu-run' in a sibling checkout while the echo above said 'THIS tree'." ; \
	    : "Nothing is lost by dropping it: an idf.py-launched QEMU in THIS tree cannot be" ; \
	    : "running, because qemu-run/qemu-web/qemu-test all hold the tree lock we hold now." ; \
	    pkill -f "file=$(QEMU_FLASH_IMAGE)," 2>/dev/null || true; \
	    sleep 1; \
	    $(find_qemu_bin); \
	    if [ -z "$$QEMU_BIN" ]; then \
	        echo "QEMU binary not found. Using idf.py method instead..."; \
	        echo "Note: This method will not have port forwarding built-in"; \
	        $(EIM_ACTIVATE) && CONFIG_ETH_USE_OPENETH=1 $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" qemu monitor; \
	    else \
	        echo "Found QEMU at: $$QEMU_BIN"; \
	        echo "Host ports (api_tests/qemu_ports.py): $(QEMU_PORT_SUMMARY)"; \
	        echo "Test against it with: cd api_tests && python -m pytest --ip localhost:$(QEMU_HTTP_PORT)   (NO --qemu)"; \
	        echo "Press Ctrl-A x to exit QEMU"; \
	        $$QEMU_BIN \
	            -M esp32 \
	            -m 4M \
	            -drive file=$(QEMU_FLASH_IMAGE),if=mtd,format=raw \
	            -drive file=$(QEMU_EFUSE_IMAGE),if=none,format=raw,id=efuse \
	            -global driver=nvram.esp32.efuse,property=drive,value=efuse \
	            -global driver=timer.esp32.timg,property=wdt_disable,value=true \
	            -nic '$(QEMU_NIC_ARG)' \
	            -nographic \
	            -serial mon:stdio \
	            $(QEMU_SERIAL_ARGS) || { \
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
# The image build is NOT a prerequisite of this target: prerequisites run before the recipe,
# i.e. before the lock could be taken, which is exactly the hole described at run_locked
# above. It is a prerequisite of the -locked target instead, so the whole build-then-run
# sequence happens inside one lock. PYTEST_ARGS/COVERAGE and any other command-line
# override reach the sub-make through MAKEFLAGS, so `make qemu-test PYTEST_ARGS="-k x"`
# and qemu-coverage's `$(MAKE) qemu-test COVERAGE=1 PYTEST_ARGS=...` keep working.
qemu-test:
	$(call run_locked,$(MAKE) --no-print-directory qemu-test-locked)

# qemu-create-flash-image depends on build-idf-project-qemu, so the correct QEMU
# firmware is always compiled (incremental) before packaging and running tests.
qemu-test-locked: qemu-create-flash-image qemu-create-efuse-image
	cd api_tests && $(PYTEST_PYTHON) -m pytest --qemu --qemu-skip-build \
	    --junitxml=$(CURDIR)/build/qemu_test_report.xml $(PYTEST_ARGS)

# End-to-end firmware coverage in QEMU: build the instrumented firmware, run the
# API test suite (reboot tests excluded via pytest's --without-reboot — a reboot
# zeroes the in-RAM gcov counters), pull .gcda over HTTP (GET /gcov), and build an
# HTML + text report.

# Coverage report locations.
COVERAGE_STREAM    := $(CURDIR)/build/coverage.stream
COVERAGE_OUT_DIR   := build/qemu_coverage
# Firmware .gcda/.gcno live together in this single object dir. Scoping gcovr to it
# avoids stale host unit-test artifacts under unittests/*/build (built with a
# different gcc/gcov version that the xtensa gcov cannot parse).
COVERAGE_FW_OBJDIR := build/esp-idf/main/CMakeFiles/__idf_main.dir

qemu-coverage:
	$(MAKE) qemu-test COVERAGE=1 \
	    PYTEST_ARGS="--coverage-dump=$(COVERAGE_STREAM) --without-reboot $(PYTEST_ARGS)"
	@$(MAKE) qemu-coverage-report

# Reconstruct .gcda from the on-target coverage stream and build the report.
# Can be re-run standalone against an existing build/coverage.stream (no QEMU run).
# EIM_ACTIVATE puts the matching xtensa gcov / gcov-tool and gcovr on PATH.
qemu-coverage-report:
	@test -s $(COVERAGE_STREAM) || { echo "ERROR: $(COVERAGE_STREAM) is missing or empty"; exit 1; }
	@echo 'Reconstructing .gcda files from coverage stream...'
	@# Drop any previous .gcda first: merge-stream ADDS to existing counters, so
	@# re-running without this would double-count. The stream holds the full counts.
	@$(FIND) $(COVERAGE_FW_OBJDIR) -name '*.gcda' -delete 2>/dev/null || true
	@$(EIM_ACTIVATE) && xtensa-esp-elf-gcov-tool merge-stream $(COVERAGE_STREAM)
	@mkdir -p $(COVERAGE_OUT_DIR)
	@echo 'Generating coverage report with gcovr...'
	@# --gcov-ignore-parse-errors negative_hits.warn_once_per_file: xtensa gcov can emit
	@# a negative hit count (GCC bug 68080); clamp to 0 instead of aborting the report.
	@$(EIM_ACTIVATE) && gcovr --root $(CURDIR) \
	    --gcov-executable xtensa-esp-elf-gcov \
	    --gcov-ignore-parse-errors negative_hits.warn_once_per_file \
	    --filter 'main/.*\.c' \
	    --exclude 'main/frontend/.*' \
	    --exclude 'main/coverage_dump\.c' \
	    --txt $(COVERAGE_OUT_DIR)/summary.txt \
	    --html-details $(COVERAGE_OUT_DIR)/index.html \
	    --json $(COVERAGE_OUT_DIR)/qemu_covr.json \
	    --print-summary \
	    $(COVERAGE_FW_OBJDIR)
	@echo ''
	@echo '==================== COVERAGE SUMMARY ===================='
	@cat $(COVERAGE_OUT_DIR)/summary.txt
	@echo '=========================================================='
	@echo 'HTML report: $(COVERAGE_OUT_DIR)/index.html'

# Dry-run: collect and list QEMU API tests without running them or building firmware.
# Usage:
#   make qemu-collect-only                          — list all qemu-marked tests
#   make qemu-collect-only PYTEST_ARGS="38_test_sniffer_slow_response.py"
qemu-collect-only:
	cd api_tests && $(PYTEST_PYTHON) -m pytest --collect-only -q $(PYTEST_ARGS)

# Help target for QEMU
qemu-help:
	@echo "QEMU Targets:"
	@echo "  qemu-apply-idf-patches  - Apply IDF source patches for QEMU builds (called automatically; idempotent)"
	@echo "  qemu-build              - Build frontend + QEMU firmware (run once or after code changes)"
	@echo "  qemu-create-flash-image - Compile QEMU firmware (incremental) and merge into qemu_flash.bin"
	@echo "  qemu-create-efuse-image - Create eFuse image build/qemu_efuse.bin if missing (idempotent)"
	@echo "  qemu-run                - Run QEMU in basic mode (no port forwarding)"
	@echo "  qemu-web                - Run QEMU with web UI at localhost:\$$(web port for this slot)"
	@echo "  qemu-ports              - Print the host ports this tree/slot uses (no build, no run)"
	@echo "  qemu-monitor            - Attach monitor to already-running QEMU (no build)"
	@echo "  qemu-bin-path           - Print path to qemu-system-xtensa binary"
	@echo "  qemu-test               - Build QEMU firmware, flash image, and run API pytest suite"
	@echo "  qemu-coverage           - Build instrumented firmware, run tests (no reboot), pull /gcov, build coverage report"
	@echo "  qemu-coverage-report    - Rebuild the coverage report from an existing build/coverage.stream (no QEMU run)"
	@echo "  qemu-collect-only       - List collected API tests without building or running"
	@echo "  qemu-clean              - Remove build/ and sdkconfig.qemu_build"
	@echo ""
	@echo "qemu-run, qemu-web and qemu-test hold an exclusive lock on this working tree"
	@echo "(.e2e-tree.lock) for their whole run, build included: they all rewrite"
	@echo "build/qemu_flash.bin, which a concurrent run in this tree would be using."
	@echo "A different WB_MGE_PORT_SLOT does NOT lift that — use a second checkout."
	@echo ""
	@echo "Build:       make qemu-build"
	@echo "Quick start: make qemu-web"
	@echo "Ports:       make qemu-ports        (set WB_MGE_PORT_SLOT=N to move the block)"
	@echo "Run tests:   make qemu-test"
	@echo "One test:    make qemu-test PYTEST_ARGS=\"-k test_auth\""
	@echo "List tests:  make qemu-collect-only"

qemu-clean:
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.qemu_build -DSDKCONFIG_DEFAULTS="sdkconfig.qemu.minimal;sdkconfig.qemu.extra" fullclean
	@rm -rf build
	@rm -f sdkconfig.qemu_build sdkconfig.qemu_build.old

.PHONY: qemu-build qemu-apply-idf-patches build-idf-project-qemu qemu-create-flash-image qemu-create-efuse-image qemu-monitor qemu-run qemu-run-locked qemu-web qemu-web-locked qemu-ports qemu-bin-path qemu-test qemu-test-locked qemu-coverage qemu-coverage-report qemu-collect-only qemu-help qemu-clean
