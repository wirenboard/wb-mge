# Name of test / tests group
TEST_NAME = auth_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/auth.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories (order matters: mocks first to override the ESP-IDF headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = auth_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
