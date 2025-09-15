
TARGET := MGE

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
DEFS += FW_VERSION_STRING=$(VERSION)

#######################################
# git info
#######################################

GIT_COMMIT ?= $(shell git rev-parse HEAD)
GIT_HASH := $(shell echo $(GIT_COMMIT) | cut -c 1-7)
BRANCH_NAME ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
GIT_BRANCH := $(shell echo $(BRANCH_NAME) | $(SED) "s/\//_/g")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 56)
GIT_INFO := $(shell echo "\\\"$(GIT_INFO)\\\"")

TARGET_GIT_INFO := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH))

DEFS += TARGET_GIT_INFO=$(TARGET_GIT_INFO)

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

# QEMU targets
build-qemu: build-frontend build-idf-project-qemu

build-idf-project-qemu:
	@echo "Building for QEMU with OpenEth ethernet driver"
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal $(addprefix -D, $(DEFS)) build

qemu-flash-image: build-qemu
	@echo "Generating QEMU flash image..."
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal qemu || \
	(cd build && python -m esptool --chip=esp32 merge_bin --output=qemu_flash.bin \
	 --fill-flash-size=4MB --flash_mode dio --flash_freq 40m --flash_size 4MB \
	 0x1000 bootloader/bootloader.bin 0x10000 mge.bin 0x8000 partition_table/partition-table.bin)

qemu-monitor: build-qemu
	@echo "Starting QEMU monitor..."
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal monitor

qemu-run: qemu-flash-image
	@echo "Running in QEMU..."
	@echo "Note: Access web interface at http://localhost:8080"
	@echo "Use ./run_qemu_with_web.sh for web access with port forwarding"
	CONFIG_ETH_USE_OPENETH=1 idf.py -DSDKCONFIG=sdkconfig.qemu.minimal qemu monitor

qemu-web: qemu-flash-image
	@echo "Running QEMU with web server port forwarding..."
	@echo "Web interface will be available at: http://localhost:8080"
	./run_qemu_with_web.sh

# Help target for QEMU
qemu-help:
	@echo "QEMU Targets:"
	@echo "  build-qemu      - Build project for QEMU emulation"
	@echo "  qemu-flash-image- Generate QEMU flash image"
	@echo "  qemu-run        - Run in QEMU (basic mode)"
	@echo "  qemu-web        - Run in QEMU with web access (localhost:8080)"
	@echo "  qemu-monitor    - Run QEMU with monitor only"
	@echo ""
	@echo "Quick start: make qemu-web"

unittests: $(UNITTESTS_TARGETS)

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

build-frontend:
	set -e; \
	cd main/frontend/; \
	npm install;\
	npm run build; \
	find dist/ -type f -name "*.gz" -exec rm -f {} \; ; \
	find dist/ -type f -exec gzip -k {} \; ; \

build-idf-project:
	idf.py $(addprefix -D, $(DEFS)) build

clean:
	idf.py fullclean
	rm -rf build
	rm -rf $(COVERAGE_REPORT_DIR)
	rm -rf main/frontend/dist
	rm -rf sdkconfig
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done

clean-qemu:
	idf.py -DSDKCONFIG=sdkconfig.qemu.minimal fullclean
	rm -rf build

.PHONY: all build-qemu build-idf-project-qemu qemu-flash qemu-monitor qemu-run clean-qemu

# Include coverage definitions and targets
include unittests/build_common_coverage.mk
