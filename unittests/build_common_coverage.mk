#######################################
# Coverage
#######################################

# Sources list for coverage measurement
COVERAGE_C_SOURCES = $(C_SOURCES)

# Output coverage report directory and report file
COVERAGE_REPORT_DIR = covr_report
COVERAGE_REPORT_FILE = $(COVERAGE_REPORT_DIR)/coverage_report.html

# Auxilary files used for coverage report generation
COVERAGE_DATA_LIST_FILE = $(COVERAGE_REPORT_DIR)/covr_data_list.txt
UNCOVERED_SRC_LIST_FILE = $(COVERAGE_REPORT_DIR)/uncovr_src_list.txt
UNCOVERED_SRC_JSON = $(COVERAGE_REPORT_DIR)/uncovr_src.json
GCOVR_FORMAT_VERSION_FILE = $(COVERAGE_REPORT_DIR)/gcovr_format_version.json

# Filters string for gcovr used for coverage report generation
COVERAGE_FILTERS_STR = $(foreach file,$(COVERAGE_C_SOURCES),-f '$(file)')

# If COVERAGE_FAIL_UNDER value is assigned, add extra flag to gcovr for failure when (project_coverage < COVERAGE_FAIL_UNDER)
ifneq ($(COVERAGE_FAIL_UNDER),)
	COVERAGE_EXTRA_FLAGS += --fail-under-line $(COVERAGE_FAIL_UNDER)
endif

# Functions merge mode for gcovr
# We use "separate" because one function could be implemented for different targets by different ways
COVERAGE_FUNC_MERGE_MODE = --merge-mode-functions=separate

# Coverage targets, $(UNITTESTS_DIRS) is provided by build_common.mk
COVERAGE_TARGETS = $(addprefix COVERAGE_, $(UNITTESTS_DIRS))

# Dependencies list for "coverage" target
COVERAGE_DEPS = remove_report_dir $(COVERAGE_REPORT_DIR) $(COVERAGE_TARGETS)

# Add trace files string for gcovr used in "coverage" target
# JSON_LIST is assigned inside the "coverage" target
COVERAGE_ADD_TRACE_FILES = $(addprefix -a , $(JSON_LIST))

#######################################
# Coverage targets
#######################################

ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1) # Addition of uncovered files to the report is enabled

# Add $(UNCOVERED_SRC_JSON) to dependancies for "coverage" target and to add trace files string for gcovr
COVERAGE_DEPS += $(UNCOVERED_SRC_JSON)
COVERAGE_ADD_TRACE_FILES += -a $(UNCOVERED_SRC_JSON)

# Generate JSON for uncovered source files (all lines of each file marked as uncovered)
$(UNCOVERED_SRC_JSON): $(UNCOVERED_SRC_LIST_FILE) $(GCOVR_FORMAT_VERSION_FILE)
	@echo "\nGenerating data file for uncovered source files..."
#	Usage: coverage_helper.sh --gen-uncovered-json UNCOVR_SRC_FILE GCOVR_VER_JSON OUT_UNCOVR_SRC_JSON
	./unittests/coverage_helper.sh --gen-uncovered-json $< $(GCOVR_FORMAT_VERSION_FILE) $@
	@echo "Uncovered sources data file saved: $@"

# Generate auxilary file with uncovered sources list
$(UNCOVERED_SRC_LIST_FILE): $(COVERAGE_TARGETS)
	@echo "\nGenerating uncovered files list..."
#	Usage: coverage_helper.sh --find-uncovered-src COVR_DATA_FILE OUT_UNCOVR_SRC_FILE SRC_LIST
	./unittests/coverage_helper.sh --find-uncovered-src $(COVERAGE_DATA_LIST_FILE) $@ "$(COVERAGE_C_SOURCES)"
	@echo "\nUncovered files found:"; cat $@
	@echo "\nUncovered files list saved: $@"

# Generate an empty JSON trace data file to get "gcovr/format_version" value from it
$(GCOVR_FORMAT_VERSION_FILE):
	@gcovr -r $(COVERAGE_REPORT_DIR) --json-pretty -o $@

endif #ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1)

# Generate summary coverage report for project (main target)
# List of dependencies COVERAGE_DEPS is assigned above
coverage: $(COVERAGE_DEPS)
	@echo "\n\n================= Generating summary coverage report for project =================\n"

#	Print extra debug information about COVERAGE_EXTRA_FLAGS, COVERAGE_NO_ADD_UNCOVERED_FILES and COVERAGE_FAIL_UNDER variables
	@echo "COVERAGE_EXTRA_FLAGS = $(COVERAGE_EXTRA_FLAGS)"
	@echo "COVERAGE_NO_ADD_UNCOVERED_FILES = $(COVERAGE_NO_ADD_UNCOVERED_FILES)"
	@echo "COVERAGE_FAIL_UNDER = $(COVERAGE_FAIL_UNDER)"
	@if [ -z "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" ] || [ "$(COVERAGE_NO_ADD_UNCOVERED_FILES)" != "1" ]; then \
		echo "Uncovered source files will be added in the report"; \
	else \
		echo "Uncovered source files will NOT be added in the report"; \
	fi

#	Read list of JSON data files from a file, JSON_LIST is used to resolve COVERAGE_ADD_TRACE_FILES value
	$(eval JSON_LIST := $(shell cat $(COVERAGE_DATA_LIST_FILE)))
	@echo "\nCoverage data files found: $(JSON_LIST)\n"

#	Generate .html coverage report
	@gcovr $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) --html-details $(COVERAGE_REPORT_FILE)
#	Print project coverage report with optional check minimum coverage level
	@gcovr -s $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) $(COVERAGE_EXTRA_FLAGS)
#	Print information about generated .html report
	@echo "\nSummary project coverage report saved: $(COVERAGE_REPORT_FILE)\n"

# Generate coverage data files for each unittest which have "coverage" target
$(COVERAGE_TARGETS): $(COVERAGE_DATA_LIST_FILE)
	$(eval COV_DIR := $(subst COVERAGE_,,$@))
#	Usage: coverage_helper.sh --make-coverage TEST_DIR OUT_COVR_DATA_FILE
#	EIM_ACTIVATE is sourced so that IDF_PATH is available for sub-make (unity.c path).
	@if [ -f "$(COV_DIR)/Makefile" ]; then \
		$(EIM_ACTIVATE) && ./unittests/coverage_helper.sh --make-coverage $(COV_DIR) $(COVERAGE_DATA_LIST_FILE); \
	fi

# Create an empty file in which the list of coverage data files will be written
$(COVERAGE_DATA_LIST_FILE):
	@touch $@

# Create directory for coverage report
$(COVERAGE_REPORT_DIR):
	mkdir -p $@

# Remove coverage report directory
remove_report_dir:
	rm -rf $(COVERAGE_REPORT_DIR)
