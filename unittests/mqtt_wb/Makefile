# Name of test / tests group
TEST_NAME = mqtt_wb_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/mqtt/wb_device.c
TESTED_SRC += $(PROJ_DIR)/main/mqtt/mqtt_client_wb.c

# Auxiliary source files used in this test.
# esp-mqtt, the mixer engine and the settings storage are mocks; the JSON library is the real
# one, because what the tests assert on is the meta document the panel will actually receive.
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_mac.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/queue.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories. main/ comes BEFORE the shared mocks on purpose: a quoted include is
# resolved against the including file's own directory first and the -I list second, so
# "config.h" from main/mqtt/ would otherwise find the stub in unittests/mocks, which carries no
# BASE_HOSTNAME - the model name the default device id is built from.
INC += mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/mqtt
INC += $(PROJ_DIR)/main/mixer
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(IDF_PATH)/components/json/cJSON
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = mqtt_wb_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
