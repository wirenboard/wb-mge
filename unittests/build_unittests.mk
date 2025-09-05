# Name of test / tests group
TEST_NAME ?= Unknown_test

# Project root directory
PROJ_DIR ?= .

# GCC compiler binary name
GCC_BIN = gcc

# Add mandatory GCC flags
GCC_FLAGS += -g -O0 -Wall

# Build directory
BUILD_DIR = build

# Add GCC definitions for unit test compilation
DEFS += __unittest_env__
DEFS += UNITY_OUTPUT_COLOR

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

$(TEST_LIST): $(TEST_BUILD_DIRS)
	@echo "\nBuilding $(TEST_NAME) test..."
	@{ \
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
