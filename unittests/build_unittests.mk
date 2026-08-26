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

# Set build directory for each separate test.
# There is no matching list of report directories: they are created by the recipe
# that writes into them, see $(COVERAGE_TEST_LIST) below.
TEST_BUILD_DIRS = $(foreach test,$(TEST_LIST),$(BUILD_DIR)/$(test))

# These targets are not files
.PHONY: all coverage coverage_report clean remove_build_dir remove_report_dir

# Default target for make.
#
# clean and run must NOT be prerequisites of the same target: make imposes no
# order between the prerequisites of one target, so under `make -j` the
# `rm -rf $(BUILD_DIR)` of clean ran concurrently with the compilation into
# $(BUILD_DIR) that run drives. That either broke the build outright ("failed to
# open coverage notes file for writing") or, worse, silently dropped whole test
# suites from the run while still exiting 0.
#
# An order-only prerequisite (`run: | clean`) would not fix this: it only orders
# clean against the recipe of run, and run has no recipe — the compilation
# happens in its RUN_* prerequisites, which make is still free to start in
# parallel with clean. Two sequential sub-makes give a hard order instead; run
# keeps its internal parallelism, because -j reaches the sub-make through
# MAKEFLAGS. With -j<N> that also carries the jobserver, so the job limit stays
# shared across the whole tree; with a bare -j make creates no jobserver at all
# and every make in the tree runs unlimited, top-level one included.
all:
	@$(MAKE) clean
	@$(MAKE) run

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
		log_file=$$test_dir/$$test_name.log && \
		rm -f $$test_dir/*.gcda && \
		$$test_bin > $$log_file 2>&1; rc=$$?; \
		cat $$log_file; \
		if [ $$rc -ne 0 ]; then exit $$rc; fi; \
		echo "\n================ Test $(TEST_NAME): $$test_name finished =================\n"; \
	}

# Coverage metering target: run all unit tests and generate coverage data and report for each test
# After that generate summary coverage report for unit-test (for all tests in TEST_LIST)
#
# remove_report_dir must NOT stand next to $(COVERAGE_TEST_LIST) in one
# prerequisite list, for the same reason as clean/run above: make orders nothing
# between the prerequisites of a target, so under `make -j` the
# `rm -rf $(REPORT_DIR)` could land after a COVERAGE_* recipe had already written
# its report there. The wipe therefore runs as its own sequential sub-make.
#
# That also gives the mkdir in $(COVERAGE_TEST_LIST) a tree that is already
# wiped: make snapshots a target's mtime before it walks that target's
# prerequisites, so decisions taken by the second sub-make cannot be based on
# files the first one has deleted.
coverage:
	@$(MAKE) remove_report_dir
	@$(MAKE) coverage_report

# Everything the coverage target does after the report directory has been wiped:
# per-test coverage data and reports, then the summary report over all of them.
coverage_report: $(COVERAGE_TEST_LIST)
	@echo "\n\n================= Generating summary coverage report for $(TEST_NAME) test =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(TEST_NAME)_report)
#	Create the directory this recipe writes into, rather than inherit it from another recipe
	@mkdir -p $(REPORT_DIR)
#	Generate summary coverage report for unit-test
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR) $(OUT_FILES_BASE_NAME) 'separate' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nSummary coverage data for $(TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nSummary coverage report for $(TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).html\n"

# Coverage data and report generation for each test.
#
# RUN_% is a prerequisite here, not just of the coverage target above: gcovr reads
# the .gcda files the test binary writes into $(BUILD_DIR)/%, so that one test has
# to be finished before its COVERAGE_% starts. Being two prerequisites of coverage
# ordered them not at all. A static pattern rule pairs each COVERAGE_% with its own
# RUN_%, so `make COVERAGE_<test>` no longer rebuilds and reruns every other test.
#
# The report directory is created by the recipe below and is deliberately not a
# target of its own carrying `| remove_report_dir`: an order-only prerequisite
# cannot express "wipe first, then create". make snapshots a target's own mtime
# before it walks its prerequisites (remake.c: this_mtime = file_mtime (file)) and
# order-only prerequisites are excluded from the comparison that could set
# must_make again, so a directory left over from a previous run was declared up to
# date, its mkdir skipped, the rm -rf then removed it anyway, and gcovr wrote into
# a path that no longer existed. That edge also made `rm -rf $(REPORT_DIR)`
# reachable from a plain `make COVERAGE_<test>`, wiping every other test's report.
# An unconditional mkdir -p in the consumer cannot be skipped and deletes nothing.
$(COVERAGE_TEST_LIST): COVERAGE_%: RUN_%
	$(eval COV_TEST_NAME := $(subst COVERAGE_,,$@))
	@echo "\n\n================= Generating coverage data and report for test $(TEST_NAME): $(COV_TEST_NAME) =================\n"
#	Print filters string for debug information
	@echo "\nGCOVR_FILTERS_STR = $(GCOVR_FILTERS_STR)"

#	Set base name for generated files
	$(eval OUT_FILES_BASE_NAME := $(REPORT_DIR)/$(COV_TEST_NAME)/$(COV_TEST_NAME)_covr)
#	Create the directory this recipe writes into, see the comment above the rule
	@mkdir -p $(REPORT_DIR)/$(COV_TEST_NAME)
#	Generate JSON data file and .html report and also print report for unit test coverage
#	Usage: coverage_helper.sh --gen-ut-coverage PROJ_DIR SEARCH_DIR OUT_FILES_BASE_NAME FUNC_MERGE_MODE [FILTERS_STR]
	$(COVERAGE_HELPER) --gen-ut-coverage $(COVERAGE_ROOT_DIR) $(BUILD_DIR)/$(COV_TEST_NAME) $(OUT_FILES_BASE_NAME) 'separate' "$(GCOVR_FILTERS_STR)"
#	Print information about generated files
	@echo "\nCoverage data for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).json"
	@echo "\nCoverage report for $(TEST_NAME): $(COV_TEST_NAME) test saved: $(OUT_FILES_BASE_NAME).html\n"

# Create test build directories for targets
$(TEST_BUILD_DIRS):
	@mkdir -p $@

# Remove test build directory
remove_build_dir:
	@rm -rf $(BUILD_DIR)

# Remove test coverage report directory
remove_report_dir:
	@rm -rf $(REPORT_DIR)

# Clean test directory: remove build and report directories
clean: remove_build_dir remove_report_dir
