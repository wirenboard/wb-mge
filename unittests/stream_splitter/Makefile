# Name of test / tests group
TEST_NAME = stream_splitter_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/stream_splitter.c

# Auxiliary source files used in this test
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = stream_splitter_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
