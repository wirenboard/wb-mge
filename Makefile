# Force bash for all recipes: the EIM activate script defines functions with dots
# in their names (e.g. idf.py()), which POSIX sh (dash on Debian/Ubuntu) rejects.
SHELL := /bin/bash

#######################################
# EIM (Espressif IDF Manager) settings
#######################################

# ESP-IDF version the project is built against. This is the exact tag EIM uses
# when installing: it names the activate script activate_idf_<tag>.sh, so the
# value must match an installed EIM version verbatim (v5.4.4, not v5.4).
# Keep it in sync with the base image tag in Dockerfile — local builds and CI
# must run the same IDF. `?=` so it can be overridden both from the environment
# and from the command line (make EIM_IDF_VERSION=v5.5.2 ...).
EIM_IDF_VERSION ?= v5.4.4

# Dockerfile carries the second half of that pin (FROM espressif/idf:<tag>), and
# dependencies.lock records the IDF version the component manager resolved
# against. check-idf-pins compares both with EIM_IDF_VERSION; see the target below.
# Absolute paths on purpose, for the same reason as EIM_ACTIVATE below: recipes
# are free to `cd` first, and a relative path would resolve against whatever
# directory the shell happens to be in.
DOCKERFILE := $(CURDIR)/Dockerfile
DEPENDENCIES_LOCK := $(CURDIR)/dependencies.lock

#######################################
# device signature
#######################################

# Buildable device signatures. Each must match the eFuse signature and its
# fw-releases.wirenboard.com/fw/by-signature/<sig> line.
# This list is the only place a signature is spelled out in Makefile/.gitignore:
# the default TARGET, the check below and the per-signature sdkconfig files
# `clean` removes derive from it, and .gitignore matches the generated
# sdkconfig.<sig> by pattern. A new signature also needs its own
# sdkconfig.defaults.<sig> here, and on the firmware side main/boards/<sig>.h
# wired into main/board_pins.h and a DEVICE_MODEL branch in main/config.h; both
# chains end in #error, so a missing branch fails the build loudly.
# Order matters: the first entry is the default TARGET, which is what the CI
# release and QEMU e2e builds use, so append new signatures, never prepend.
MODEL_LIST := mge_v3 mgu_v1
# Default build target; override e.g. `make TARGET=<signature> build-idf-project`.
TARGET ?= $(firstword $(MODEL_LIST))
override TARGET := $(strip $(TARGET))

# A mistyped TARGET used to reach CMake before failing, as a missing
# sdkconfig.defaults.<typo>. MODEL_LIST has to be the filter-out patterns and
# TARGET the checked word, not the reverse: $(filter $(TARGET),$(MODEL_LIST))
# reads TARGET as a pattern list, so '%' and a multi-word value matched
# everything. The word count catches the empty and multi-word values filter-out
# cannot see. Both checks run at parse time, for every target, so a stray TARGET
# in the environment fails even targets that never read it — hence the reminder
# in the messages.
# The strip above is `override` because a command-line TARGET= wins over a plain
# assignment, and unstripped whitespace reaches the file names built from TARGET.
ifneq ($(words $(TARGET)),1)
    $(error TARGET must name exactly one signature, got '$(TARGET)'; expected one of: $(MODEL_LIST). Check the environment too)
endif
ifneq ($(filter-out $(MODEL_LIST),$(TARGET)),)
    $(error unknown TARGET '$(TARGET)'; expected one of: $(MODEL_LIST). Check the environment too)
endif

MODEL_DEFINE := $(shell echo MODEL_$(TARGET))

DEFS += DEVICE_SIGNATURE=$(TARGET)
DEFS += MODEL_DEFINE=$(MODEL_DEFINE)

# Per-target generated sdkconfig so switching TARGET never reuses another
# signature's PSRAM/pin config (mirrors the dedicated sdkconfig.qemu_build).
SDKCONFIG_FILE := sdkconfig.$(TARGET)

# Legacy names that no longer come from MODEL_LIST: the removed native build
# flavour today, plus any signature retired from MODEL_LIST later. `clean` keeps
# removing them so a tree that still carries one does not keep it forever,
# invisible under .gitignore.
SDKCONFIG_LEGACY := sdkconfig.native

# Every sdkconfig `clean` removes. A bare sdkconfig is written by no make target —
# they all pass -DSDKCONFIG= — only by a manual idf.py run; idf.py keeps a .old
# backup next to every sdkconfig it rewrites, hence the second line.
SDKCONFIG_BASES := sdkconfig $(SDKCONFIG_LEGACY) sdkconfig.qemu_build $(addprefix sdkconfig.,$(MODEL_LIST))
SDKCONFIG_GENERATED := $(SDKCONFIG_BASES) $(addsuffix .old,$(SDKCONFIG_BASES))

#######################################
# Directories
#######################################

BUILD_DIR = build
RELEASE_DIR = release

#######################################
# Tool selection (platform-specific)
#######################################

# EIM_ACTIVATE sources scripts/idf_env.sh, which activates the EIM toolchain for
# $(EIM_IDF_VERSION) when it is installed, falls back to the IDF_PATH already set
# in the environment (Docker espressif/idf image, Jenkins with export.sh), and in
# both cases verifies that the IDF actually in use is the expected version — the
# activation used to silently override the caller's IDF_PATH and build against a
# different IDF than intended. See scripts/idf_env.sh for the details.
# It is sourced, not executed, so recipes keep the `$(EIM_ACTIVATE) && <command>`
# form. EIM_IDF_VERSION is exported so the script can read it.
# The path is absolute on purpose: recipes are free to `cd` first, and a relative
# path would resolve against whatever directory the shell happens to be in.
export EIM_IDF_VERSION
EIM_ACTIVATE := . "$(CURDIR)/scripts/idf_env.sh"

# idf.py path: IDF_PATH is set either by EIM_ACTIVATE or by the caller's export.sh.
# $$IDF_PATH in a recipe expands to $IDF_PATH in the shell at recipe execution time.
IDF_PY = $$IDF_PATH/tools/idf.py

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
    # macOS: prefer GNU tools from Homebrew (gsed/ggrep/gfind) over BSD variants
    SED  := $(shell which gsed  2>/dev/null || which sed)
    GREP := $(shell which ggrep 2>/dev/null || which grep)
    FIND := $(shell which gfind 2>/dev/null || echo "find")
else
    SED  := sed
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
GIT_HASH := $(shell echo $(GIT_COMMIT) | cut -c 1-7)
BRANCH_NAME ?= $(shell git rev-parse --abbrev-ref HEAD 2>/dev/null)
GIT_BRANCH := $(shell echo $(BRANCH_NAME) | $(SED) "s/\//_/g")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 50)

DEFS += TARGET_PROJECT_NAME=$(TARGET)
DEFS += FIRMWARE_GIT_INFO=$(GIT_INFO)

#######################################
# Coverage instrumentation (opt-in)
#######################################

# COVERAGE=1 instruments the main component with gcov (--coverage): emits .gcno at
# compile time and .gcda at runtime. Off by default; increases flash/RAM footprint,
# so it must not ship in release firmware.
#
# The value is passed to EVERY firmware build as an explicit CMake cache value
# (-DCOVERAGE=0/1) on purpose: idf.py -D writes a CACHE variable that persists in
# build/CMakeCache.txt, so always re-asserting the current value resets it. Without
# this, a prior `COVERAGE=1` build would silently leak instrumentation (and the
# test-only /gcov endpoint) into the next normal build until a fullclean.
COVERAGE ?= 0
DEFS += COVERAGE=$(COVERAGE)

#######################################
# Release file name
#######################################

RELEASE_FILE_NAME := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH).bin)

#######################################
# unittests
#######################################

# Find only top-level unittest module directories (those that have their own Makefile).
# Avoids the old pattern of grep-ing all dirs by name, which matched nested subdirs
# (mocks/, freertos/ etc.) and caused dozens of spurious "rm -rf build" lines in clean.
UNITTESTS_DIRS += $(shell $(FIND) ./unittests -maxdepth 1 -mindepth 1 -type d -exec test -f {}/Makefile \; -print)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

# C source files for coverage measurement (exclude frontend files)
C_SOURCES = $(shell $(FIND) main -name "*.c" -not -path "*/frontend/*")

#######################################
# targets
#######################################

all: build-frontend build-idf-project

test: unittests test-frontend

unittests: $(UNITTESTS_TARGETS)

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		export IDF_ENV_QUIET=1; \
		$(EIM_ACTIVATE) && cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

lint-frontend:
	@echo 'Running frontend linter'
	@mkdir -p $(CURDIR)/build
	@{ \
		cd main/frontend/ && \
		npm install; \
	}
	@{ \
		cd main/frontend/ && \
		rc=0; \
		npm run lint || rc=$$?; \
		npx eslint src --ext .vue,.ts,.js --format junit --output-file $(CURDIR)/build/eslint_report.xml > /dev/null 2>&1 || true; \
		exit $$rc; \
	}

# C linter. Requires build/compile_commands.json from a prior idf.py build
# (any build flavour — Lint reads what was actually compiled). The wrapper
# script patches a couple of pyclang defaults that don't fit our toolchain.
# Paths to esp-clang / pyclang are derived at runtime from IDF_PYTHON_ENV_PATH
# and IDF_TOOLS_PATH so this works in both EIM (local) and the Docker image.
# See docs/linters.md.
CLANG_TIDY_OUT := /tmp/clang-tidy-out
CLANG_TIDY_LOG := /tmp/clang-tidy-log

lint-c:
	@echo 'Running clang-tidy on main/'
	@test -f build/compile_commands.json || { echo "ERROR: build/compile_commands.json missing. Run 'make build-idf-project' or equivalent first."; exit 1; }
	@mkdir -p $(CLANG_TIDY_OUT) $(CLANG_TIDY_LOG)
	@$(EIM_ACTIVATE) && \
	    PATH="$$IDF_PATH/tools:$$PATH" && \
	    test -n "$$IDF_PYTHON_ENV_PATH" || { echo "ERROR: IDF_PYTHON_ENV_PATH not set. Source the IDF env first."; exit 1; } && \
	    TOOLS_PATH="$${IDF_TOOLS_PATH:-$$HOME/.espressif}" && \
	    NEWLIB_INCLUDE=$$(ls -d $$TOOLS_PATH/xtensa-esp-elf/*/xtensa-esp-elf/xtensa-esp-elf/include $$TOOLS_PATH/tools/xtensa-esp-elf/*/xtensa-esp-elf/xtensa-esp-elf/include 2>/dev/null | head -n1) && \
	    test -n "$$NEWLIB_INCLUDE" || { echo "ERROR: xtensa-esp-elf newlib include dir not found under $$TOOLS_PATH (tried with and without /tools prefix)"; exit 1; } && \
	    "$$IDF_PYTHON_ENV_PATH/bin/python3" scripts/clang-tidy/wb_clang_tidy.py \
	        --build-dir build \
	        --check-files-regex '$(CURDIR)/main/.*\.c' \
	        --output-path $(CLANG_TIDY_OUT) \
	        --log-path $(CLANG_TIDY_LOG) \
	        --exit-code \
	        --clang-extra-args="-header-filter=.*/main/.* -config-file=$(CURDIR)/.clang-tidy -extra-arg-before=-isystem -extra-arg-before=$$NEWLIB_INCLUDE" \
	        $(CURDIR)

# Third-party and generated trees are never linted: a Cyrillic comment in some
# site-packages module (api_tests/.venv alone holds ~2200 .py files) or in a
# generated build tree would fail the target for a reason unrelated to this
# repository.
LINT_EXCLUDE_DIRS := --exclude-dir=.venv --exclude-dir=node_modules --exclude-dir=build \
                     --exclude-dir=managed_components --exclude-dir=__pycache__

# Each block distinguishes the three grep outcomes instead of the plain
# `if grep ...; then` it used to be: that form read every non-zero status as
# "no violations", so on a grep without PCRE support (BSD grep, i.e. macOS
# without Homebrew, where $(GREP) falls back to /usr/bin/grep) or on a mistyped
# directory the target printed OK and passed. 0 = violations found, 1 = clean,
# >1 = grep itself could not run.
lint-comments:
	@echo "Checking for Cyrillic characters in C/H comments..."
	@out=$$($(GREP) -rnP $(LINT_EXCLUDE_DIRS) --include='*.c' --include='*.h' '//[^\n]*\p{Cyrillic}' main/ unittests/); rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    echo "$$out"; \
	    echo "ERROR: Cyrillic characters found in C/H comments. All comments must be in English."; \
	    exit 1; \
	elif [ $$rc -gt 1 ]; then \
	    echo "ERROR: grep failed while checking C/H comments (no PCRE support in $(GREP), or a listed directory is missing)."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in C/H"
	@echo "Checking for Cyrillic characters in Python comments..."
	@out=$$($(GREP) -rnP $(LINT_EXCLUDE_DIRS) --include='*.py' '#[^\n]*\p{Cyrillic}' api_tests/ patches/ scripts/ unittests/ wb_test/); rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    echo "$$out"; \
	    echo "ERROR: Cyrillic characters found in Python comments. All comments must be in English."; \
	    exit 1; \
	elif [ $$rc -gt 1 ]; then \
	    echo "ERROR: grep failed while checking Python comments (no PCRE support in $(GREP), or a listed directory is missing)."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in Python"
	@echo "Checking for Cyrillic characters in shell comments..."
	@out=$$($(GREP) -rnP $(LINT_EXCLUDE_DIRS) --include='*.sh' '#[^\n]*\p{Cyrillic}' scripts/ patches/ unittests/ wb_test/); rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    echo "$$out"; \
	    echo "ERROR: Cyrillic characters found in shell comments. All comments must be in English."; \
	    exit 1; \
	elif [ $$rc -gt 1 ]; then \
	    echo "ERROR: grep failed while checking shell comments (no PCRE support in $(GREP), or a listed directory is missing)."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in shell"
	@echo "Checking for Cyrillic characters in frontend comments..."
	@out=$$($(GREP) -rnP $(LINT_EXCLUDE_DIRS) --include='*.ts' --include='*.vue' --include='*.js' '//[^\n]*\p{Cyrillic}' main/frontend/src/); rc=$$?; \
	if [ $$rc -eq 0 ]; then \
	    echo "$$out"; \
	    echo "ERROR: Cyrillic characters found in frontend comments. All comments must be in English."; \
	    exit 1; \
	elif [ $$rc -gt 1 ]; then \
	    echo "ERROR: grep failed while checking frontend comments (no PCRE support in $(GREP), or a listed directory is missing)."; \
	    exit 1; \
	fi
	@echo "OK: no Cyrillic characters in frontend"

test-frontend:
	@echo 'Running frontend tests'
	@mkdir -p $(CURDIR)/build
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run test -- --reporter=default --reporter=junit --outputFile.junit=$(CURDIR)/build/vitest_report.xml; \
	}

build-frontend:
	@echo 'Building frontend'
	@{ \
		set -e && \
		cd main/frontend/ && \
		npm install && \
		npm run build && \
		$(FIND) dist/ -type f -name "*.gz" -exec rm -f {} \; && \
		$(FIND) dist/ -type f ! -name "*.gz" ! -name "*.woff2" ! -name "*.webp" -exec gzip -k {} \; ; \
	}

# The IDF version is pinned in four places: EIM_IDF_VERSION here (local/EIM
# builds), the base image tag in Dockerfile (CI and container builds), the idf
# range in main/idf_component.yml (what the component manager accepts) and the
# resolved idf version in dependencies.lock. Until now they were kept together by
# a set of "keep in sync" comments, and a drift only surfaced mid-build, when
# scripts/idf_env.sh compared the running IDF with the expected one. This target
# catches it up front — a couple of seds, no measurable cost as a build
# prerequisite.
#
# What it compares: EIM_IDF_VERSION against EVERY 'FROM espressif/idf:<tag>' in
# Dockerfile (a multi-stage file must not pin two different IDFs), and against
# the 'idf: version:' entry in dependencies.lock, with the leading 'v' stripped
# and MAJOR.MINOR padded to MAJOR.MINOR.0 — the same normalisation scripts/idf_env.sh
# applies, because EIM names v5.4 what the lock records as 5.4.0. Without it a
# perfectly consistent v5.4 pin would be reported as a mismatch.
# What it does NOT compare: the version range in main/idf_component.yml. That is
# a constraint, not a pin — deciding whether a version satisfies it is the
# component manager's job, which it does at resolve time and reports as
# "no versions of idf match". Widen that range by hand when moving off the pin.
#
# A missing Dockerfile or dependencies.lock is an error, not a skip: both are
# tracked in the repo, so their absence means an incomplete checkout, and
# silently skipping the check here is exactly the kind of quiet pass this whole
# guard exists to remove.
#
# Escape hatch: IDF_PINS_CHECK=0 skips this target and nothing else — the same
# way IDF_VERSION_CHECK=0 skips only the installed-vs-expected comparison in
# scripts/idf_env.sh. It exists for deliberate off-pin builds (e.g. rebuilding on
# v5.4.2 to reproduce the uart_set_pin regression) and prints a warning rather
# than passing silently. See README.md, "Building against a different ESP-IDF".
check-idf-pins:
	@if [ "$${IDF_PINS_CHECK:-1}" = "0" ]; then \
	    echo "WARNING: IDF_PINS_CHECK=0 — ESP-IDF pin cross-check skipped (Makefile vs Dockerfile vs dependencies.lock)."; \
	    exit 0; \
	fi; \
	test -f "$(DOCKERFILE)" || { \
	    echo "ERROR: $(DOCKERFILE) not found — cannot verify it pins the same ESP-IDF as EIM_IDF_VERSION=$(EIM_IDF_VERSION)."; \
	    echo "       Run make from the repository root with a complete checkout."; \
	    exit 1; \
	}; \
	test -f "$(DEPENDENCIES_LOCK)" || { \
	    echo "ERROR: $(DEPENDENCIES_LOCK) not found — cannot verify it records the same ESP-IDF as EIM_IDF_VERSION=$(EIM_IDF_VERSION)."; \
	    echo "       Run make from the repository root with a complete checkout."; \
	    exit 1; \
	}; \
	DOCKER_IDF=$$($(SED) -n 's|^[[:space:]]*[Ff][Rr][Oo][Mm][[:space:]].*espressif/idf:\([^[:space:]][^[:space:]]*\).*|\1|p' "$(DOCKERFILE)"); \
	if [ -z "$$DOCKER_IDF" ]; then \
	    echo "ERROR: no 'FROM espressif/idf:<tag>' line in $(DOCKERFILE) — cannot verify the ESP-IDF pins."; \
	    exit 1; \
	fi; \
	DOCKER_MISMATCH=""; \
	for tag in $$DOCKER_IDF; do \
	    if [ "$$tag" != "$(EIM_IDF_VERSION)" ]; then DOCKER_MISMATCH="$$DOCKER_MISMATCH $$tag"; fi; \
	done; \
	if [ -n "$$DOCKER_MISMATCH" ]; then \
	    echo "ERROR: the ESP-IDF pins disagree — local and CI builds would use different IDF versions."; \
	    echo "       Makefile   (EIM_IDF_VERSION)   : $(EIM_IDF_VERSION)"; \
	    echo "       Dockerfile (FROM espressif/idf) : $$(echo "$$DOCKER_IDF" | tr '\n' ' ' | $(SED) 's/[[:space:]]*$$//')"; \
	    echo "       Update both pins (and main/idf_component.yml if the range no longer fits)."; \
	    echo "       IDF_PINS_CHECK=0 skips this check for a deliberate off-pin build."; \
	    exit 1; \
	fi; \
	LOCK_IDF=$$($(SED) -n '/^  idf:[[:space:]]*$$/,/^[^[:space:]]/ s/^[[:space:]]*version:[[:space:]]*[^0-9]*\([0-9][0-9.]*\).*/\1/p' "$(DEPENDENCIES_LOCK)" | head -n 1); \
	if [ -z "$$LOCK_IDF" ]; then \
	    echo "ERROR: no 'idf:' version entry in $(DEPENDENCIES_LOCK) — cannot verify the ESP-IDF pins."; \
	    exit 1; \
	fi; \
	EXPECTED_IDF="$(EIM_IDF_VERSION:v%=%)"; \
	case "$$EXPECTED_IDF" in \
	    *.*.*) ;; \
	    *.*)   EXPECTED_IDF="$$EXPECTED_IDF.0" ;; \
	    *)     EXPECTED_IDF="$$EXPECTED_IDF.0.0" ;; \
	esac; \
	if [ "$$LOCK_IDF" != "$$EXPECTED_IDF" ]; then \
	    echo "ERROR: the ESP-IDF pins disagree — the checked-in lock was resolved against another IDF."; \
	    echo "       Makefile          (EIM_IDF_VERSION) : $(EIM_IDF_VERSION) (expects $$EXPECTED_IDF)"; \
	    echo "       dependencies.lock (idf version)     : $$LOCK_IDF"; \
	    echo "       Re-resolve the dependencies on the pinned IDF and commit the lock."; \
	    echo "       IDF_PINS_CHECK=0 skips this check for a deliberate off-pin build."; \
	    exit 1; \
	fi; \
	echo "ESP-IDF pins match: $(EIM_IDF_VERSION) (Makefile, Dockerfile, dependencies.lock)"

# check-idf-pins first: under `make -j` a bare `build-idf-project: check-idf-pins
# apply-idf-patches` let the patches land while the pin check was still running,
# so a build that the check was about to reject had already modified the IDF
# sources. The prerequisite here makes the order a hard one.
apply-idf-patches: check-idf-pins
	@echo "Applying IDF patches..."
	@$(EIM_ACTIVATE) && python3 patches/apply_idf_patch.py bug01-uart-driver-delete-intr-order.patch

# NOTE: the build/ dir is shared across signatures. When switching TARGET from
# one signature to another run `make clean` first to avoid a stale/mixed artifact.
build-idf-project: check-idf-pins apply-idf-patches
	@echo 'Building ESP-IDF project'
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=$(SDKCONFIG_FILE) -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.$(TARGET)" $(addprefix -D, $(DEFS)) build
	@$(MAKE) prepare_release

prepare_release:
	@mkdir -p $(RELEASE_DIR)
	@rm -rf $(RELEASE_DIR)/*
	@cp $(BUILD_DIR)/$(TARGET).bin $(RELEASE_DIR)/$(RELEASE_FILE_NAME)
	@echo 'Release firmware: $(RELEASE_DIR)/$(RELEASE_FILE_NAME)'

clean:
	@echo 'Cleaning project'
	@rm -rf $(BUILD_DIR)
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=$(SDKCONFIG_FILE) fullclean
	@rm -rf $(RELEASE_DIR)
	@rm -rf $(COVERAGE_REPORT_DIR)
	@rm -rf main/frontend/dist
	@rm -f $(SDKCONFIG_GENERATED)
	@echo 'Cleaning unittests'
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			echo "  Cleaning $$dir"; \
			$(MAKE) -C $$dir clean --no-print-directory --silent; \
		fi; \
	done

#######################################
# Flash and monitor
#######################################

# check-idf-pins is a prerequisite because `idf.py flash` builds the project
# before flashing it: without it `make flash` would compile firmware on an
# unverified set of pins. flash-all, monitor and ota-flash need no such guard —
# they only push or read back artefacts that an earlier build produced.
flash: check-idf-pins
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=$(SDKCONFIG_FILE) -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.$(TARGET)" flash

# Flash all partitions (bootloader + partition table + OTA data + app) via esptool directly.
# Useful when idf.py flash cannot detect the port automatically.
flash-all:
	@$(EIM_ACTIVATE) && python -m esptool --chip esp32 -b 460800 \
		--before default_reset --after hard_reset write_flash \
		--flash_mode dio --flash_size 4MB --flash_freq 40m \
		0x1000  build/bootloader/bootloader.bin \
		0x8000  build/partition_table/partition-table.bin \
		0xd000  build/ota_data_initial.bin \
		0x90000 build/$(TARGET).bin

monitor:
	@$(EIM_ACTIVATE) && $(IDF_PY) -DSDKCONFIG=$(SDKCONFIG_FILE) -DSDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.$(TARGET)" monitor

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

.PHONY: all test unittests lint-frontend lint-c lint-comments test-frontend build-frontend check-idf-pins apply-idf-patches build-idf-project prepare_release clean flash flash-all monitor ota-flash coverage-combined

# Include coverage definitions and targets
-include unittests/build_common_coverage.mk

# Include QEMU targets
include qemu.mk

#######################################
# Combined coverage (unit tests + QEMU e2e)
#######################################

# Merge the host unit-test gcovr tracefiles with the QEMU e2e tracefile into one
# report. The two datasets come from different compilers, so they are merged at the
# gcovr JSON level (not raw .gcda). NOTE: line/function coverage merges as a true
# union (covered by unit tests OR e2e); branch coverage is pooled across the two
# compilers, not unioned, so treat combined branch numbers as indicative only.
#
# Prerequisites (run these first; this target only merges existing tracefiles):
#   make coverage        # unit-test tracefiles  -> unittests/*/covr_report/**/*_covr.json
#   make qemu-coverage   # e2e tracefile         -> build/qemu_coverage/qemu_covr.json
COMBINED_COVERAGE_DIR  := build/combined_coverage
QEMU_COVERAGE_JSON     := build/qemu_coverage/qemu_covr.json

coverage-combined:
	@test -f $(QEMU_COVERAGE_JSON) || { echo "ERROR: $(QEMU_COVERAGE_JSON) missing. Run 'make qemu-coverage' first."; exit 1; }
	@UNIT_JSONS=$$($(FIND) unittests -path '*/covr_report/*' -name '*_covr.json'); \
	    test -n "$$UNIT_JSONS" || { echo "ERROR: no unit-test tracefiles found. Run 'make coverage' first."; exit 1; }; \
	    mkdir -p $(COMBINED_COVERAGE_DIR); \
	    ADD_ARGS=""; for j in $$UNIT_JSONS; do ADD_ARGS="$$ADD_ARGS -a $$j"; done; \
	    $(EIM_ACTIVATE) && gcovr --root $(CURDIR) \
	        -a $(QEMU_COVERAGE_JSON) $$ADD_ARGS \
	        --filter 'main/.*\.c' \
	        --exclude 'main/frontend/.*' \
	        --exclude 'main/coverage_dump\.c' \
	        --txt $(COMBINED_COVERAGE_DIR)/summary.txt \
	        --html-details $(COMBINED_COVERAGE_DIR)/index.html \
	        --print-summary
	@echo ''
	@echo '============== COMBINED COVERAGE (unit tests + QEMU e2e) =============='
	@cat $(COMBINED_COVERAGE_DIR)/summary.txt
	@echo '======================================================================'
	@echo 'HTML report: $(COMBINED_COVERAGE_DIR)/index.html'
	@echo 'NOTE: line/function coverage is a union; branch coverage is pooled across compilers (indicative only).'
