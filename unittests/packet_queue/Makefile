# Name of test / tests group
TEST_NAME = packet_queue_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/packet_queue.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/malloc.c
AUX_SRC += mocks/freertos/queue.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include/

# List of tests
TEST_LIST = packet_queue_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
