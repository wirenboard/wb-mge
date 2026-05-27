# Name of test / tests group
TEST_NAME = settings_manager_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/settings_manager.c

# Auxiliary source files used in this test
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/cache_modbus_server.c
AUX_SRC += mocks/settings_update.c
AUX_SRC += mocks/settings_save_timer.c
AUX_SRC += mocks/auth.c
AUX_SRC += mocks/json_utils.c
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/bridge
INC += $(IDF_PATH)/components/json/cJSON
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/nvs_flash/include

# List of tests
TEST_LIST = settings_manager_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
