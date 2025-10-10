# Name of test / tests group
TEST_NAME = setting_validators_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/setting_validators.c

# Auxiliary source files used in this test

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = setting_validators_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
