# Unit test runner for wb_modbus_bridge PoC
# Uses Unity (ThrowTheSwitch), same approach as wb-mge.
#
# Usage:
#   make          - build and run all tests
#   make clean    - remove build artifacts

CC       = gcc
CFLAGS   = -std=c99 -Wall -Wextra -g -O0 \
            -D_POSIX_C_SOURCE=200809L \
            -D_DEFAULT_SOURCE \
            -Iunity \
            -I../src \
            -I../thirdparty/cJSON

# Source files under test (no main, no platform I/O)
SRC_MODBUS_FRAME = ../src/modbus_frame.c
SRC_VALUE_CONV   = ../src/value_conv.c
SRC_TEMPLATE     = ../src/template.c ../thirdparty/cJSON/cJSON.c
SRC_UNITY        = unity/unity.c

BUILD = build
TESTS = test_crc test_value_conv test_template

.PHONY: all clean $(TESTS)

all: $(TESTS)
	@echo ""
	@echo "All tests passed."

test_crc: $(BUILD)
	@echo "=== Building test_crc ==="
	$(CC) $(CFLAGS) -o $(BUILD)/test_crc \
	    test_crc.c $(SRC_MODBUS_FRAME) $(SRC_UNITY) -lm
	@echo "=== Running test_crc ==="
	@$(BUILD)/test_crc

test_value_conv: $(BUILD)
	@echo "=== Building test_value_conv ==="
	$(CC) $(CFLAGS) -o $(BUILD)/test_value_conv \
	    test_value_conv.c $(SRC_VALUE_CONV) $(SRC_UNITY) -lm
	@echo "=== Running test_value_conv ==="
	@$(BUILD)/test_value_conv

# Template tests need to run from the workspace root so ../templates/ paths resolve.
# We pass the templates dir as an env variable TMPL_DIR.
test_template: $(BUILD)
	@echo "=== Building test_template ==="
	$(CC) $(CFLAGS) -DTMPL_DIR='"$(abspath ../..)/templates"' \
	    -o $(BUILD)/test_template \
	    test_template.c $(SRC_TEMPLATE) $(SRC_UNITY) -lm
	@echo "=== Running test_template ==="
	@$(BUILD)/test_template

$(BUILD):
	@mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD)
