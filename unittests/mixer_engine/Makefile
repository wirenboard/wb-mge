# Name of test / tests group
TEST_NAME = mixer_engine_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/mixer/mixer.c

# Auxiliary source files used in this test
# The REAL drive driver and the REAL program model sit under the engine: what the tests
# assert on is the register traffic the engine ends up producing, so mocking the driver
# would mock away half of what is under test. Only the Modbus master and the settings
# storage below it are mocks.
AUX_SRC += $(PROJ_DIR)/main/mixer/vfd_t13.c
AUX_SRC += $(PROJ_DIR)/main/mixer/program.c
AUX_SRC += mocks/mb_master.c
AUX_SRC += mocks/setting_items.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_timer.c
AUX_SRC += $(IDF_PATH)/components/mbedtls/mbedtls/library/base64.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories (order matters: mocks first to override the real headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/mixer
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/mbedtls/mbedtls/include
# base64.c includes the library-private "common.h"
INC += $(IDF_PATH)/components/mbedtls/mbedtls/library

# List of tests
TEST_LIST = mixer_engine_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
