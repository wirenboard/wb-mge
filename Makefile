
TARGET := MGE

#######################################
# version parsing
#######################################

# get version string from ChangeLog if VERSION_STRING is not defined
VERSION_STRING ?= $(shell cat ChangeLog | grep version: | head -n 1 | sed 's/.*version:[ ]*//')
# check version string format using regexp
VERSION := $(shell echo $(VERSION_STRING) | awk '/[0-9]+\.[0-9]+\.[0-9]+(\+wb[1-9][0-9]*|-rc[1-9][0-9]*)?$$/{print $$0}')
# global defines with version in different formats
DEFS += FW_VERSION_STRING=$(VERSION)


#######################################
# git info
#######################################

GIT_HASH := $(shell git rev-parse HEAD | cut -c -7 )
GIT_BRANCH := $(shell git rev-parse --abbrev-ref HEAD | sed "s/\//_/")
GIT_INFO := $(shell echo "$(GIT_HASH)"_"$(GIT_BRANCH)" | head -c 56)
GIT_INFO := $(shell echo "\\\"$(GIT_INFO)\\\"")

TARGET_GIT_INFO := $(shell echo $(TARGET)__$(VERSION)_$(GIT_BRANCH)_$(GIT_HASH))

DEFS += TARGET_GIT_INFO=$(TARGET_GIT_INFO)


#######################################
# unittests
#######################################

UNITTESTS_DIRS += $(shell find -type d | grep unittests)
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
	set -e; \
	cd main/frontend/; \
	npm install;\
	npm run build; \
    find dist/ -type f -name "*.gz" -exec rm -f {} \; ; \
    find dist/ -type f -exec gzip -k {} \; ; \
	cd ../../

build-idf-project:
	idf.py $(addprefix -D, $(DEFS)) build

clean:
	idf.py fullclean
	rm -rf build
	rm -rf main/frontend/dist
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done
