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
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_efuse.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_psram.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
# sys_info.c is built twice, once per board configuration, because the PSRAM
# detection in sys_info_init() sits behind #if CONFIG_SPIRAM:
#   sys_info_test         - CONFIG_SPIRAM=1, WB-MGU (mgu_v1): runtime detection via the esp_psram mock
#   sys_info_nopsram_test - CONFIG_SPIRAM unset, WB-MGE (mge_v3): branch compiled out entirely
# Each test gets its own build/<test>/ directory, so the two objects (and their
# coverage data) never collide.
TEST_LIST = sys_info_test sys_info_nopsram_test

DEFS += CONFIG_EFUSE_VIRTUAL=n

# Board-specific define, applied only to the PSRAM-enabled binary.
# sys_info_nopsram_test is intentionally built without CONFIG_SPIRAM.
sys_info_test: DEFS += CONFIG_SPIRAM=1

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
