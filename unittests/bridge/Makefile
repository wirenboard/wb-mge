# Name of test / tests group
TEST_NAME = bridge_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/bridge.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/bridge
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = bridge_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
