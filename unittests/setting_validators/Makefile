# Name of test / tests group
TEST_NAME = setting_validators_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/setting_validators.c

# Auxiliary source files used in this test
# setting_items.c is linked in (as AUX, so it stays out of this suite's coverage filter)
# so that test_setting_defaults_pass_their_validators can check the settings table against
# the REAL validators. Its own suite substitutes mocks for setting_validators.c and could
# therefore never prove that a default is actually accepted.
AUX_SRC += $(PROJ_DIR)/main/setting_items.c
# validate_mixer_program() delegates to the program model, which decodes base64 with the
# same mbedtls the firmware links. Both are AUX, so neither shows up in this suite's
# coverage filter — main/mixer/program.c has a suite of its own (unittests/mixer_program).
AUX_SRC += $(PROJ_DIR)/main/mixer/program.c
AUX_SRC += $(IDF_PATH)/components/mbedtls/mbedtls/library/base64.c
AUX_SRC += mocks/nv_storage.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_efuse.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/
INC += $(PROJ_DIR)/main/mixer
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/mbedtls/mbedtls/include
# base64.c includes the library-private "common.h"
INC += $(IDF_PATH)/components/mbedtls/mbedtls/library
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/nvs_flash/include/

# List of tests
TEST_LIST = setting_validators_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
