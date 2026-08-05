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
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main
# cache_modbus_server.h — the cache Modbus TCP server API whose lifecycle settings_update drives
INC += $(PROJ_DIR)/main/bridge
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = settings_update_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
