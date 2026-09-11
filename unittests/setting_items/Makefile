# Name of test / tests group
TEST_NAME = setting_items_tests

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/setting_items.c

# Build the settings table as it is built for a WB-MGU. Without a MODEL_* signature
# config.h falls back to the mge_v3 values and setting_items_migrate_port2_line_format()
# is not compiled at all, so the one-time port 2 line-format migration — which exists
# only on that board — could not be tested here. The board difference this pins down is
# the port 2 line format (115200 8E1 for the internal WBE2 bus to the Z-Wave board
# instead of port 1's 9600 8N2); expected_items in setting_items_test.c carries both
# variants, so the table check itself stays honest either way.
DEFS += MODEL_mgu_v1

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_efuse.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/nvs_flash/include/

# List of tests
TEST_LIST = setting_items_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
