# Name of test / tests group
TEST_NAME = settings_update_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/settings_update.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = settings_update_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
