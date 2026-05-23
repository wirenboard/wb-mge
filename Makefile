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

# EIM activate script sources IDF_PATH, venv, and tool paths into the current shell.
# idf.py is a shell alias set by EIM — not a binary — so it cannot be called directly
# in Makefile recipes. Use EIM_ACTIVATE before every idf.py invocation instead.
EIM_ACTIVATE := . $(HOME)/.espressif/tools/activate_idf_$(EIM_IDF_VERSION).sh

# idf.py path derived from IDF_PATH set by the activate script ($$IDF_PATH → $IDF_PATH in shell).
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

UNITTESTS_DIRS += $(shell $(FIND) . -type d | $(GREP) unittests)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

# C source files for coverage measurement (exclude frontend files)
C_SOURCES = $(shell $(FIND) main -name "*.c" -not -path "*/frontend/*")

#######################################
# targets
#######################################

all: unittests build-frontend build-idf-project

unittests: $(UNITTESTS_TARGETS)

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		$(EIM_ACTIVATE) && cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

build-frontend:
	@echo 'Building frontend'
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run test && \
		npm run build && \
		$(FIND) dist/ -type f -name "*.gz" -exec rm -f {} \; && \
		$(FIND) dist/ -type f -exec gzip -k {} \; ; \
	}

build-idf-project:
	@echo 'Building ESP-IDF project'
	@$(EIM_ACTIVATE) && $(IDF_PY) $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf $(RELEASE_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

clean:
	@echo 'Cleaning project'
	@rm -rf $(BUILD_DIR)
	@$(EIM_ACTIVATE) && $(IDF_PY) fullclean
	@rm -rf $(RELEASE_DIR)
	@rm -rf $(COVERAGE_REPORT_DIR)
	@rm -rf main/frontend/dist
	@rm -rf sdkconfig
	@echo 'Cleaning unittests'
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done

#######################################
# Flash and monitor
#######################################

flash:
	@$(EIM_ACTIVATE) && $(IDF_PY) flash

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
	@$(EIM_ACTIVATE) && $(IDF_PY) monitor

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

.PHONY: all unittests build-frontend build-idf-project prepare_release clean flash flash-all monitor ota-flash

# Include coverage definitions and targets
-include unittests/build_common_coverage.mk

# Include QEMU targets
include qemu.mk
