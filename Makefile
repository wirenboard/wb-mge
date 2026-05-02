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
# OS detection and tool selection
#######################################

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS - use GNU tools if available, fallback to BSD versions
    SED := $(shell which gsed 2>/dev/null || which sed)
    GREP := $(shell which ggrep 2>/dev/null || which grep)
    # Try to find GNU find first, then use system find with compatible syntax
    FIND := $(shell which gfind 2>/dev/null || echo "find")
    # On macOS idf.py is not in PATH by default; resolve via IDF_PATH set by activate script
    IDF_PY ?= $(if $(IDF_PATH),$(IDF_PATH)/tools/idf.py,idf.py)
else
    # Linux and other Unix-like systems
    SED := sed
    GREP := grep
    FIND := find
    IDF_PY ?= idf.py
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
		cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
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
	@$(IDF_PY) $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf $(RELEASE_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

clean:
	@echo 'Cleaning project'
	@rm -rf $(BUILD_DIR)
	@$(IDF_PY) fullclean
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

.PHONY: all unittests build-frontend build-idf-project prepare_release clean ota-flash

# Include coverage definitions and targets
-include unittests/build_common_coverage.mk

# Include QEMU targets
include qemu.mk
