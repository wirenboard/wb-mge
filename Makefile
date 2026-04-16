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
RELEASE_INTERNAL_DIR = release_internal

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
# Copy protection
#######################################

# Scripts and data directory
COPY_PROT_DIR = copy_protection

# Scripts
COPY_PROT_HELPER_SCRIPT = ${COPY_PROT_DIR}/copy_prot_helper.py
COPY_PROT_RANDOM_SCRIPT = ${COPY_PROT_DIR}/gen_random_data.py

# Generated data files with random data
COPY_PROT_SWAP_TABLES_FILE = ${COPY_PROT_DIR}/swap_tables.txt
COPY_PROT_DUMMY_DATA_FILE = ${COPY_PROT_DIR}/dummy_data.txt

# Keys header file to build project with
COPY_PROT_KEYS_HEADER_FILE = main/copy_protection/keys.h

# Default keys headers file co cause compilation error
COPY_PROT_DEFAULT_KEYS_HEADER_FILE = ${COPY_PROT_DIR}/keys.h.default

# Use keys file from Jenkins secrets or local file for internal build
MGE_KEYS_FILE ?= ${COPY_PROT_DIR}/keys.txt

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

build-internal: build-frontend build-idf-project-internal

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
		npm install &&\
		npm run build && \
		$(FIND) dist/ -type f -name "*.gz" -exec rm -f {} \; && \
		$(FIND) dist/ -type f -exec gzip -k {} \; ; \
	}

build-idf-project: keys_header_file
	@echo 'Building ESP-IDF project'
	@$(IDF_PY) $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

build-idf-project-internal: DEFS += INTERNAL_BUILD=1
build-idf-project-internal:
	@echo 'Building ESP-IDF project (internal, no keys)'
	@$(IDF_PY) $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release_internal

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf $(RELEASE_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

prepare_release_internal:
	@mkdir -p $(RELEASE_INTERNAL_DIR)
	@rm -rf $(RELEASE_INTERNAL_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_INTERNAL_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware (internal): $(RELEASE_INTERNAL_DIR)/$(RELEASE_FILE_NAME)'

keys_header_file:
	@${COPY_PROT_RANDOM_SCRIPT} --swap_tables ${COPY_PROT_SWAP_TABLES_FILE} --dummy_data ${COPY_PROT_DUMMY_DATA_FILE}
	@${COPY_PROT_HELPER_SCRIPT} --keys $(MGE_KEYS_FILE) --swap_tables ${COPY_PROT_SWAP_TABLES_FILE} \
		--dummy_data ${COPY_PROT_DUMMY_DATA_FILE} --out_header ${COPY_PROT_KEYS_HEADER_FILE}

clean:
	@echo 'Cleaning project'
	@rm -rf $(BUILD_DIR)
	@$(IDF_PY) fullclean
	@rm -rf $(RELEASE_DIR)
	@rm -rf $(RELEASE_INTERNAL_DIR)
	@rm -rf $(COVERAGE_REPORT_DIR)
	@rm -rf main/frontend/dist
	@rm -rf sdkconfig
	@echo 'Cleaning protection keys and data'
	@cp -f ${COPY_PROT_DEFAULT_KEYS_HEADER_FILE} ${COPY_PROT_KEYS_HEADER_FILE}
	@rm -f ${COPY_PROT_SWAP_TABLES_FILE}
	@rm -f ${COPY_PROT_DUMMY_DATA_FILE}
	@echo 'Cleaning unittests'
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done

.PHONY: all unittests build-frontend build-idf-project build-idf-project-internal prepare_release prepare_release_internal keys_header_file clean

# Include coverage definitions and targets
-include unittests/build_common_coverage.mk

# Include QEMU targets
include qemu.mk
