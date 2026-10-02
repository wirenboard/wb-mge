# Name of test / tests group
TEST_NAME = fp_acs_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# fp_acs.c is #included by the test, which calls its static helpers and its scan and enrollment
# steps one at a time and resets its statics; TESTED_SRC stays empty, and the coverage filter names
# it below.

# Auxiliary source files used in this test
AUX_SRC += mocks/fp_log_fake.c
AUX_SRC += mocks/r503_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_heap_caps.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_log.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_lock_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_store_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/trace.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_timer.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/queue.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories (order matters: the fp_acs mocks shadow the shared esp_log.h)
INC += mocks
INC += $(UNITTEST_DIR)/mocks/fp_acs
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/fp_acs
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(IDF_PATH)/components/esp_common/include

# The one build the feature exists in: master role on WB-MGU (see main/fp_acs/fp_acs.h)
DEFS += CONFIG_WB_MODBUS_ROLE_MASTER
DEFS += MODEL_mgu_v1
# esp_err_to_name() names the errors, as on the device, rather than "UNKNOWN ERROR"
DEFS += CONFIG_ESP_ERR_TO_NAME_LOOKUP

# List of tests
TEST_LIST = fp_acs_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk

# The module under test is #included by the test, so it is not in TESTED_SRC; filter on it here
GCOVR_FILTERS_STR = -f 'main/fp_acs/fp_acs.c'
