# Name of test / tests group
TEST_NAME = port_manager_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/port_manager.c

# Auxiliary source files used in this test
AUX_SRC += mocks/bridge.c
AUX_SRC += mocks/sniffer.c
AUX_SRC += mocks/cache_multimaster.c
AUX_SRC += mocks/cache_modbus_server.c
AUX_SRC += mocks/serial.c
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/rs485_stats.c
AUX_SRC += mocks/auth.c
AUX_SRC += mocks/json_utils.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += $(UNITTEST_DIR)/mocks/debug_log.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = port_manager_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
