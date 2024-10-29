all: unittests build-frontend build-idf-project

UNITTESTS_DIRS += $(shell find -type d | grep unittests)
UNITTESTS_TARGETS = $(addprefix UNITTEST_, $(UNITTESTS_DIRS))

$(UNITTESTS_TARGETS):
	$(eval UT_DIR := $(subst UNITTEST_,,$@))
	@if [ -f $(UT_DIR)/Makefile ]; then \
		cd $(UT_DIR) && $(MAKE) --no-print-directory && cd -; \
	fi

unittests: $(UNITTESTS_TARGETS)

build-frontend:
	set -e; \
	cd main/frontend/; \
	npm install;\
	npm run build; \
    find dist/ -type f -name "*.gz" -exec rm -f {} \; ; \
    find dist/ -type f -exec gzip -k {} \; ; \
	cd ../../; \
	echo "Frontend build completed"

build-idf-project:
	idf.py build

clean:
	idf.py fullclean
	rm -rf build
	rm -rf main/frontend/dist
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done
