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

# Dependencies list for the "coverage_report" target.
# remove_report_dir is not in here: it runs as a separate, sequential sub-make,
# see the "coverage" target below. Neither is $(COVERAGE_REPORT_DIR): the recipes
# that write into that directory create it themselves.
COVERAGE_DEPS = $(COVERAGE_TARGETS)

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
# EIM_ACTIVATE is sourced so that gcovr is on PATH (EIM/local installs do not export it globally).
#
# The search path is spelled out rather than left implicit. It is the same path
# gcovr would pick on its own — it defaults search_paths to --root (see
# gcovr/formats/gcov/read.py) — so this is documentation, not a behaviour change:
# it keeps the scan visibly pinned to $(COVERAGE_REPORT_DIR), which holds no
# coverage data, instead of relying on that default staying put. Only the
# "gcovr/format_version" field of the output is ever used.
$(GCOVR_FORMAT_VERSION_FILE):
	@mkdir -p $(COVERAGE_REPORT_DIR)
	@$(EIM_ACTIVATE) && gcovr -r $(COVERAGE_REPORT_DIR) --json-pretty -o $@ $(COVERAGE_REPORT_DIR)

endif #ifneq ($(COVERAGE_NO_ADD_UNCOVERED_FILES),1)

# These targets are not files
.PHONY: coverage coverage_report remove_report_dir

# Generate summary coverage report for project (main target)
#
# The wipe of the report directory is a separate, sequential sub-make rather than
# one more entry in COVERAGE_DEPS. make imposes no order between the prerequisites
# of a target, so under `make -j` the `rm -rf $(COVERAGE_REPORT_DIR)` raced with
# the recipes that fill that directory, and runs on a tree that already had a
# report directory failed on a missing $(COVERAGE_DATA_LIST_FILE). An order-only
# edge onto remove_report_dir does not help: make snapshots a target's mtime
# before it walks its prerequisites (remake.c: this_mtime = file_mtime (file)) and
# order-only prerequisites cannot set must_make again, so a file left over from
# the previous run was declared up to date against a state the rm -rf was about to
# destroy, and its recipe was skipped. Only a second make process re-stats
# everything, which is what makes the order here reliable.
coverage:
	@$(MAKE) remove_report_dir
	@$(MAKE) coverage_report

# Everything the coverage target does after the report directory has been wiped.
# List of dependencies COVERAGE_DEPS is assigned above
coverage_report: $(COVERAGE_DEPS)
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
#	EIM_ACTIVATE is sourced so that gcovr is on PATH (EIM/local installs do not export it globally).
	@$(EIM_ACTIVATE) && gcovr $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) --html-details $(COVERAGE_REPORT_FILE)
#	Print project coverage report with optional check minimum coverage level
	@$(EIM_ACTIVATE) && gcovr -s $(COVERAGE_ADD_TRACE_FILES) $(COVERAGE_FUNC_MERGE_MODE) $(COVERAGE_FILTERS_STR) $(COVERAGE_EXTRA_FLAGS)
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

# Create an empty file in which the list of coverage data files will be written.
# The recipe creates the directory it writes into; $(COVERAGE_REPORT_DIR) is not a
# target of its own, because a prerequisite edge — order-only or not — cannot
# express "wipe first, then create". See the comment on the coverage target.
$(COVERAGE_DATA_LIST_FILE):
	@mkdir -p $(COVERAGE_REPORT_DIR)
	@touch $@

# Remove coverage report directory
remove_report_dir:
	rm -rf $(COVERAGE_REPORT_DIR)
