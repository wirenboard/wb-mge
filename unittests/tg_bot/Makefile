# Name of test / tests group
TEST_NAME = tg_bot_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# tg_bot.c is #included by the test, which calls its poll, its command handlers and its status
# directly and resets its statics; TESTED_SRC stays empty, and the coverage filter names it below.

# Auxiliary source files used in this test
AUX_SRC += mocks/esp_http_client.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_heap_caps.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/esp_log.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_acs_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_lock_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/fp_store_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/setting_items_fake.c
AUX_SRC += $(UNITTEST_DIR)/mocks/fp_acs/trace.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
# Real implementation: the bot parses the Bot API's answers with it, and a mock would only
# reimplement the very parsing under test.
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories (order matters: the local and fp_acs mocks shadow ESP-IDF and the shared
# esp_log.h; main/config.h, not the shared mock one, has the Bot API default)
INC += mocks
INC += $(UNITTEST_DIR)/mocks/fp_acs
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/fp_acs
INC += $(PROJ_DIR)/main/telegram
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
TEST_LIST = tg_bot_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk

# The module under test is #included by the test, so it is not in TESTED_SRC; filter on it here
GCOVR_FILTERS_STR = -f 'main/telegram/tg_bot.c'
