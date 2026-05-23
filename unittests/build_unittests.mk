# Default values for variables if they are not defined in the test's Makefile
TEST_NAME ?= Unknown_test
PROJ_DIR ?= .
COVERAGE_ROOT_DIR ?= $(PROJ_DIR)
GCC_BIN ?= gcc

# Build and coverage report directories
BUILD_DIR = build
REPORT_DIR = covr_report

# coverage_helper.sh script path
COVERAGE_HELPER = $(PROJ_DIR)/unittests/coverage_helper.sh

# Add mandatory GCC flags for coverage and debug
GCC_FLAGS += --coverage -g -O0 -Wall

# Add GCC definitions for unit test compilation
DEFS += __unittest_env__
DEFS += UNITY_OUTPUT_COLOR

# Set filters string for gcovr, use regex pattern for file filtering
GCOVR_FILTERS_STR = $(foreach file,$(TESTED_SRC),-f '$(shell python3 -c "import os; print(os.path.relpath('$(file)', '$(COVERAGE_ROOT_DIR)'))")')

# Set source files list for compiler
SRC = $(TESTED_SRC) $(AUX_SRC)

# Add Unity source and include
UNITY_DIR = $(IDF_PATH)/components/unity/unity
SRC += $(UNITY_DIR)/src/unity.c
INC += $(UNITY_DIR)/src

# Set coverage targets list
COVERAGE_TEST_LIST = $(addprefix COVERAGE_, $(TEST_LIST))

# Set build and report directories for each separate test
TEST_BUILD_DIRS = $(foreach test,$(TEST_LIST),$(BUILD_DIR)/$(test))
TEST_REPORT_DIRS = $(foreach test,$(TEST_LIST),$(REPORT_DIR)/$(test))

# These targets are not files
.PHONY: all coverage clean remove_build_dir remove_report_dir

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
		rm -f $$test_dir/*.gcda && \
		$$test_bin && \
		echo "\n================ Test $(TEST_NAME): $$test_name finished =================\n"; \
	}

# Coverage metering target: run all unit tests and generate coverage data and report for each test
# After that generate summary coverage report for unit-test (for all tests in TEST_LIST)
coverage: run remove_report_dir $(COVERAGE_TEST_LIST)
	@echo "\n\n================= Generating summary coverage report for $(TEST_NAME) test =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(TEST_NAME)_report)
#	Generate summary coverage report for unit-test
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR) $(OUT_FILES_BASE_NAME) 'separate' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nSummary coverage data for $(TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nSummary coverage report for $(TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).html\n"

# Coverage data and report generation for each test
$(COVERAGE_TEST_LIST): $(TEST_REPORT_DIRS)
	$(eval COV_TEST_NAME := $(subst COVERAGE_,,$@))
	@echo "\n\n================= Generating coverage data and report for test $(TEST_NAME): $(COV_TEST_NAME) =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(COV_TEST_NAME)/$(COV_TEST_NAME)_covr)
#	Generate JSON data file and .html report and also print report for unit test coverage
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR)/$(COV_TEST_NAME) $(OUT_FILES_BASE_NAME) 'separate' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nCoverage data for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nCoverage report for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).html\n"

# Create test build directories for targets
$(TEST_BUILD_DIRS):
	@mkdir -p $@

# Create test coverage report directories
$(TEST_REPORT_DIRS):
	@mkdir -p $@

# Remove test build directory
remove_build_dir:
	@rm -rf $(BUILD_DIR)

# Remove test coverage report directory
remove_report_dir:
	@rm -rf $(REPORT_DIR)

# Clean test directory: remove build and report directories
clean: remove_build_dir remove_report_dir
