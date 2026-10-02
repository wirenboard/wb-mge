# Name of test / tests group
TEST_NAME = wb_device_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked: the test goes through the public API only, with a recording
# transport underneath and wb_device_init() resetting every static between tests
TESTED_SRC += $(PROJ_DIR)/main/mqtt/wb_device.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_heap_caps.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_log.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_acs_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_lock_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_store_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/trace.c
# Real implementation: the meta JSON is what is under test, and the test parses it back
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories (order matters: the fp_acs mocks shadow the shared esp_log.h; main/config.h,
# not the shared mock one, has BASE_HOSTNAME)
INC += $(UNITTEST_DIR)/mocks/fp_acs
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/fp_acs
INC += $(PROJ_DIR)/main/mqtt
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/json/cJSON

# The one build the feature exists in: master role on WB-MGU (see main/fp_acs/fp_acs.h)
DEFS += CONFIG_WB_MODBUS_ROLE_MASTER
DEFS += MODEL_mgu_v1
# esp_err_to_name() names the errors, as on the device, rather than "UNKNOWN ERROR"
DEFS += CONFIG_ESP_ERR_TO_NAME_LOOKUP

# List of tests
TEST_LIST = wb_device_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
