# Name of test / tests group
TEST_NAME = cmd_handler_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/cmd_handler.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c

# Include directories (order matters: mocks first to override the ESP-IDF headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
# sys_info.h pulls in bridge.h/serial.h and, through wb_app_desc.h, the ESP-IDF app-format
# headers; expose them on the host (mirrors the ota_handler and info_handlers unit-test setup).
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/json/cJSON
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include

# List of tests
TEST_LIST = cmd_handler_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
