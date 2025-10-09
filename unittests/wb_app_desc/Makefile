# Name of test / tests group
TEST_NAME = wb_app_desc_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/wb_app_desc/wb_app_desc.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_app_format/include

# List of tests
TEST_LIST = wb_app_desc_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
