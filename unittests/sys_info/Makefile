# Name of test / tests group
TEST_NAME = sys_info_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/sys_info.c

# Auxiliary source files used in this test
AUX_SRC += $(PROJ_DIR)/main/wb_app_desc/wb_app_desc.c
AUX_SRC += mocks/esp_efuse.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_log_level.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main/
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include/
INC += $(IDF_PATH)/components/esp_hw_support/include/

# List of tests
TEST_LIST = sys_info_test

DEFS += CONFIG_EFUSE_VIRTUAL=n

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
