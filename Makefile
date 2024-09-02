RUN_IDF = . $(HOME)/esp/esp-idf-5.3/export.sh

all: unittests
	$(RUN_IDF)
	idf.py build


UNITTESTS_DIRS += $(shell find -type d | grep unittests)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

unittests: $(UNITTESTS_TARGETS)

clean:
	rm -rf build
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done
