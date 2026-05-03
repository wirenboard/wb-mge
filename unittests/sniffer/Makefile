# Name of test / tests group
TEST_NAME = sniffer_tests

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be tested
TESTED_SRC += $(PROJ_DIR)/main/bridge/sniffer.c

# Auxiliary source files used in this test
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += $(PROJ_DIR)/main/bridge/fast_modbus.c
AUX_SRC += $(PROJ_DIR)/main/bridge/stream_splitter.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/queue.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/timers.c
AUX_SRC += $(UNITTEST_DIR)/mocks/malloc.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += mocks/tcp_server.c
AUX_SRC += mocks/serial.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/bridge
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include

# List of tests
TEST_LIST = sniffer_helpers_test sniffer_process_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
