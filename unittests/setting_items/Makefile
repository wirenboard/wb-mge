# Name of test / tests group
TEST_NAME = setting_items_test

# Project root directory
PROJ_DIR = ../..

# GCC compiler binary name
GCC_BIN = gcc

# Build directory
BUILD_DIR = build

# Add mandatory GCC flags
GCC_FLAGS += -g -O0 -Wall

# Add GCC definitions for unit test compilation
DEFS += __unittest_env__
DEFS += UNITY_OUTPUT_COLOR

# List of tests
TEST_LIST = setting_items_test

# Set source files list for compiler
SRC = $(TESTED_SRC) $(AUX_SRC)

# Add Unity source and include
UNITY_DIR = $(IDF_PATH)/components/unity/unity
SRC += $(UNITY_DIR)/src/unity.c
INC += $(UNITY_DIR)/src

# Set build directory for each separate test
TEST_BUILD_DIRS = $(foreach test,$(TEST_LIST),$(BUILD_DIR)/$(test))

# These targets are not files
.PHONY: all clean remove_build_dir

# Default target for make
all: clean run

run: $(addprefix RUN_, $(TEST_LIST))

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/setting_items.c

# Auxiliary source files used in this test
AUX_SRC += ../mocks/*.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += ..
INC += ../mocks
INC += $(PROJ_DIR)/main/
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include/
INC += $(IDF_PATH)/components/esp_hw_support/include/
INC += $(IDF_PATH)/components/nvs_flash/include/

$(TEST_LIST): $(TEST_BUILD_DIRS)
	@echo "\nBuilding $(TEST_NAME) test..."
	{ \
		test_dir=$(BUILD_DIR)/$@ && \
		test_bin=$$test_dir/$@ && \
		$(GCC_BIN) $(addprefix -D, $(DEFS)) $(addprefix -I, $(INC)) $@.c $(SRC) $(GCC_FLAGS) -o $$test_bin; \
	}

RUN_%: %
	@{ \
		test_name=$(subst RUN_,,$@) && \
		echo "\n\n================= Running test $(TEST_NAME): $$test_name =================\n" && \
		test_dir=$(BUILD_DIR)/$$test_name && \
		test_bin=$$test_dir/$$test_name && \
		$$test_bin && \
		echo "\n================ Test $(TEST_NAME): $$test_name finished =================\n"; \
	}

# Create test build directories for targets
$(TEST_BUILD_DIRS):
	@mkdir -p $@

# Remove test build directory
remove_build_dir:
	rm -rf $(BUILD_DIR)

# Clean build directory
clean: remove_build_dir
