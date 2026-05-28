# Force bash for all recipes: the EIM activate script defines functions with dots
# in their names (e.g. idf.py()), which POSIX sh (dash on Debian/Ubuntu) rejects.
SHELL := /bin/bash

#######################################
# EIM (Espressif IDF Manager) settings
#######################################

# IDF version installed via EIM; used to locate the activate script.
EIM_IDF_VERSION := v5.4

#######################################
# device signature
#######################################

TARGET := mge_v3

MODEL_DEFINE := $(shell echo MODEL_$(TARGET))

DEFS += DEVICE_SIGNATURE=$(TARGET)
DEFS += MODEL_DEFINE=$(MODEL_DEFINE)

#######################################
# Directories
#######################################

BUILD_DIR = build
RELEASE_DIR = release

#######################################
# Tool selection (platform-specific)
#######################################

# EIM_ACTIVATE sources the EIM activate script when it exists (local EIM install).
# If the script is absent (Docker espressif/idf image, Jenkins with export.sh),
# it expands to "true" — a no-op — assuming IDF_PATH is already set in the environment.
# stdout is redirected to /dev/null to suppress the verbose activation messages;
# stderr is kept so that errors (missing script, bad env) are still visible.
EIM_ACTIVATE_SCRIPT := $(HOME)/.espressif/tools/activate_idf_$(EIM_IDF_VERSION).sh
EIM_ACTIVATE := $(if $(wildcard $(EIM_ACTIVATE_SCRIPT)),. $(EIM_ACTIVATE_SCRIPT) >/dev/null,true)

# idf.py path: IDF_PATH is set either by EIM_ACTIVATE or by the caller's export.sh.
# $$IDF_PATH in a recipe expands to $IDF_PATH in the shell at recipe execution time.
IDF_PY = $$IDF_PATH/tools/idf.py

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS: prefer GNU tools from Homebrew (gsed/ggrep/gfind) over BSD variants
    SED  := $(shell which gsed  2>/dev/null || which sed)
    GREP := $(shell which ggrep 2>/dev/null || which grep)
    FIND := $(shell which gfind 2>/dev/null || echo "find")
else
    SED  := sed
    GREP := grep
    FIND := find
endif

#######################################
# version parsing
#######################################

# get version string from ChangeLog if VERSION_STRING is not defined
VERSION_STRING ?= $(shell cat ChangeLog | $(GREP) version: | head -n 1 | $(SED) 's/.*version:[ ]*//')
# check version string format using regexp
VERSION := $(shell echo $(VERSION_STRING) | awk '/[0-9]+\.[0-9]+\.[0-9]+(\+wb[1-9][0-9]*|-rc[1-9][0-9]*)?$$/{print $$0}')
# global defines with version in different formats
DEFS += FIRMWARE_VERSION=$(VERSION)

#######################################
# git info
#######################################

GIT_COMMIT ?= $(shell git rev-parse HEAD)
GIT_HASH := $(shell echo $(GIT_COMMIT) | cut -c 1-7)
BRANCH_NAME ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
GIT_BRANCH := $(shell echo $(BRANCH_NAME) | $(SED) "s/\//_/g")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 50)

DEFS += TARGET_PROJECT_NAME=$(TARGET)
DEFS += FIRMWARE_GIT_INFO=$(GIT_INFO)

#######################################
# Release file name
#######################################

RELEASE_FILE_NAME := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH).bin)

#######################################
# unittests
#######################################

# Find only top-level unittest module directories (those that have their own Makefile).
# Avoids the old pattern of grep-ing all dirs by name, which matched nested subdirs
# (mocks/, freertos/ etc.) and caused dozens of spurious "rm -rf build" lines in clean.
UNITTESTS_DIRS += $(shell $(FIND) ./unittests -maxdepth 1 -mindepth 1 -type d -exec test -f {}/Makefile \; -print)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

# C source files for coverage measurement (exclude frontend files)
C_SOURCES = $(shell $(FIND) main -name "*.c" -not -path "*/frontend/*")

#######################################
# targets
#######################################

all: build-frontend build-idf-project

test: unittests test-frontend

unittests: $(UNITTESTS_TARGETS)

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		$(EIM_ACTIVATE) && cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

lint-frontend:
	@echo 'Running frontend linter'
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run lint; \
	}

# C linter. Requires build/compile_commands.json from a prior idf.py build
# (any build flavour — Lint reads what was actually compiled). The wrapper
# script patches a couple of pyclang defaults that don't fit our toolchain.
# Paths to esp-clang / pyclang are derived at runtime from IDF_PYTHON_ENV_PATH
# and IDF_TOOLS_PATH so this works in both EIM (local) and the Docker image.
# See docs/linters.md.
CLANG_TIDY_OUT := /tmp/clang-tidy-out
CLANG_TIDY_LOG := /tmp/clang-tidy-log

lint-c:
	@echo 'Running clang-tidy on main/'
	@test -f build/compile_commands.json || { echo "ERROR: build/compile_commands.json missing. Run 'make build-idf-project' or equivalent first."; exit 1; }
	@mkdir -p $(CLANG_TIDY_OUT) $(CLANG_TIDY_LOG)
	@$(EIM_ACTIVATE) && \
	    PATH="$$IDF_PATH/tools:$$PATH" && \
	    test -n "$$IDF_PYTHON_ENV_PATH" || { echo "ERROR: IDF_PYTHON_ENV_PATH not set. Source the IDF env first."; exit 1; } && \
	    TOOLS_PATH="$${IDF_TOOLS_PATH:-$$HOME/.espressif}" && \
	    NEWLIB_INCLUDE=$$(ls -d $$TOOLS_PATH/xtensa-esp-elf/*/xtensa-esp-elf/xtensa-esp-elf/include $$TOOLS_PATH/tools/xtensa-esp-elf/*/xtensa-esp-elf/xtensa-esp-elf/include 2>/dev/null | head -n1) && \
	    test -n "$$NEWLIB_INCLUDE" || { echo "ERROR: xtensa-esp-elf newlib include dir not found under $$TOOLS_PATH (tried with and without /tools prefix)"; exit 1; } && \
	    "$$IDF_PYTHON_ENV_PATH/bin/python3" scripts/clang-tidy/wb_clang_tidy.py \
	        --build-dir build \
	        --check-files-regex '$(CURDIR)/main/.*\.c' \
	        --output-path $(CLANG_TIDY_OUT) \
	        --log-path $(CLANG_TIDY_LOG) \
	        --exit-code \
	        --clang-extra-args="-header-filter=.*/main/.* -config-file=$(CURDIR)/.clang-tidy -extra-arg-before=-isystem -extra-arg-before=$$NEWLIB_INCLUDE" \
	        $(CURDIR)

lint-comments:
	@echo "Checking for Cyrillic characters in C/H comments..."
	@if $(GREP) -rnP --include='*.c' --include='*.h' '//[^\n]*[\x{0400}-\x{04FF}]' main/ unittests/; then \
	    echo "ERROR: Cyrillic characters found in C/H comments. All comments must be in English."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in C/H"
	@echo "Checking for Cyrillic characters in Python comments..."
	@if $(GREP) -rnP --include='*.py' '#[^\n]*[\x{0400}-\x{04FF}]' api_tests/; then \
	    echo "ERROR: Cyrillic characters found in Python comments. All comments must be in English."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in Python"
	@echo "Checking for Cyrillic characters in frontend comments..."
	@if $(GREP) -rnP --include='*.ts' --include='*.vue' --include='*.js' '//[^\n]*[\x{0400}-\x{04FF}]' main/frontend/src/; then \
	    echo "ERROR: Cyrillic characters found in frontend comments. All comments must be in English."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in frontend"

test-frontend:
	@echo 'Running frontend tests'
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run test; \
	}

build-frontend:
	@echo 'Building frontend'
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run build && \
		$(FIND) dist/ -type f -name "*.gz" -exec rm -f {} \; && \
		$(FIND) dist/ -type f -exec gzip -k {} \; ; \
	}

apply-idf-patches:
	@echo "Applying IDF patches..."
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch

build-idf-project: apply-idf-patches
	@echo 'Building ESP-IDF project'
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.native $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf $(RELEASE_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

clean:
	@echo 'Cleaning project'
	@rm -rf $(BUILD_DIR)
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.native fullclean
	@rm -rf $(RELEASE_DIR)
	@rm -rf $(COVERAGE_REPORT_DIR)
	@rm -rf main/frontend/dist
	@rm -f sdkconfig sdkconfig.old sdkconfig.native sdkconfig.native.old sdkconfig.qemu_build sdkconfig.qemu_build.old
	@echo 'Cleaning unittests'
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			echo "  Cleaning $$dir"; \
			$(MAKE) -C $$dir clean --no-print-directory --silent; \
		fi; \
	done

#######################################
# Flash and monitor
#######################################

flash:
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.native flash

# Flash all partitions (bootloader + partition table + OTA data + app) via esptool directly.
# Useful when idf.py flash cannot detect the port automatically.
flash-all:
	@$(EIM_ACTIVATE) && python -m esptool --chip esp32 -b 460800 \
		--before default_reset --after hard_reset write_flash \
		--flash_mode dio --flash_size 4MB --flash_freq 40m \
		0x1000  build/bootloader/bootloader.bin \
		0x8000  build/partition_table/partition-table.bin \
		0xd000  build/ota_data_initial.bin \
		0x90000 build/$(TARGET).bin

monitor:
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=sdkconfig.native monitor

#######################################
# OTA flash
#######################################

OTA_HOST ?= 192.168.1.1
OTA_USER ?= admin
OTA_PASS ?= admin
OTA_COOKIE_FILE := /tmp/mge_ota_cookie.txt

ota-flash:
	@echo "Flashing $(RELEASE_DIR)/$(RELEASE_FILE_NAME) to http://$(OTA_HOST)/ ..."
	@AUTH_RESULT=$$(curl -s -c $(OTA_COOKIE_FILE) -X POST http://$(OTA_HOST)/auth \
		-H "Content-Type: application/json" \
		-d '{"login":"$(OTA_USER)","pass":"$(OTA_PASS)"}'); \
	echo "$$AUTH_RESULT" | python3 -c "import sys,json; d=json.load(sys.stdin); exit(0 if d.get('auth') else 1)" \
		|| (echo "ERROR: Authentication failed: $$AUTH_RESULT"; exit 1)
	@echo "Authenticated, uploading firmware..."
	@curl --progress-bar -b $(OTA_COOKIE_FILE) \
		-X POST http://$(OTA_HOST)/update \
		-H "Content-Type: application/octet-stream" \
		--data-binary @$(RELEASE_DIR)/$(RELEASE_FILE_NAME) \
		--max-time 60 \
		| python3 -c "import sys,json; d=json.load(sys.stdin); exit(0) if d.get('success') else (print('ERROR:', d), exit(1))" \
		|| (echo "ERROR: Firmware upload failed"; exit 1)
	@rm -f $(OTA_COOKIE_FILE)
	@echo "OTA flash complete, device is rebooting"

.PHONY: all test unittests lint-frontend lint-c lint-comments test-frontend build-frontend apply-idf-patches build-idf-project prepare_release clean flash flash-all monitor ota-flash

# Include coverage definitions and targets
-include unittests/build_common_coverage.mk

# Include QEMU targets
include qemu.mk
