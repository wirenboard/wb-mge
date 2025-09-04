#######################################
# device signature
#######################################

TARGET := MGEv3

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
else
    # Linux and other Unix-like systems
    SED := sed
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
GIT_HASH := $(shell echo $(GIT_COMMIT) | cut -c -7 )
BRANCH_NAME ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
GIT_BRANCH := $(shell echo $(BRANCH_NAME) | sed "s/\//_/")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 56)

RELEASE_FILE_NAME := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH).bin)

DEFS += TARGET_PROJECT_NAME=$(TARGET)
DEFS += FIRMWARE_GIT_INFO=$(GIT_INFO)

#######################################
# unittests
#######################################

UNITTESTS_DIRS += $(shell $(FIND) . -type d -name "*unittests*" 2>/dev/null)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))


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
		set -e; \
		cd main/frontend/; \
		npm install;\
		npm run build; \
		find dist/ -type f -name "*.gz" -exec rm -f {} \; ; \
		find dist/ -type f -exec gzip -k {} \; ; \
	}

build-idf-project:
	@echo 'Building ESP-IDF project'
	@idf.py $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf release/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

clean:
	@echo 'Cleaning project'
	@idf.py fullclean
	@rm -rf $(BUILD_DIR)
	@rm -rf $(RELEASE_DIR)
	@rm -rf main/frontend/dist
	@rm -rf sdkconfig
	@echo 'Cleaning unittests'
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done
