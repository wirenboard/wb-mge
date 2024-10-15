IDF_PY = docker run --rm -v "$(PWD)":/project -w /project -u "$(UID)" -e HOME=/tmp espressif/idf idf.py

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
	docker build -t frontend-build .; \
	docker create --name temp-container frontend-build; \
	docker cp temp-container:/dist/. ./dist; \
	docker rm temp-container; \
	find dist/ -type f -name "*.gz" -exec rm -f {} \;; \
	find dist/ -type f -exec gzip -k {} \;; \
	cd ../../; \
	echo "Frontend build completed"

build-idf-project:
	$(IDF_PY) build

clean:
	$(IDF_PY) fullclean
	rm -rf build
	rm -rf main/frontend/dist
	@for dir in $(UNITTESTS_DIRS); do \
		if [ -f  $$dir/Makefile ]; then \
			cd $$dir && $(MAKE) clean --no-print-directory; cd -; \
		fi; \
	done
