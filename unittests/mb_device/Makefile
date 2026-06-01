# Name of test / tests group
TEST_NAME = mb_device_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/mb_device.c

# Auxiliary source files used in this test
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += mocks/voltage_monitor.c
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/cache_multimaster.c
AUX_SRC += mocks/esp_heap_caps.c
AUX_SRC += mocks/esp_system.c
AUX_SRC += mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_timer.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories (order matters: mocks first to override ESP-IDF headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/driver
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(PROJ_DIR)/main/voltage_monitor
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/bootloader_support/include

# List of tests
TEST_LIST = mb_device_test

DEFS += CONFIG_EFUSE_VIRTUAL=n

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
