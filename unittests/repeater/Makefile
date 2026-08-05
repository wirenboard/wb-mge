# Name of test / tests group
TEST_NAME = repeater_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/repeater.c

# Auxiliary source files used in this test
AUX_SRC += mocks/serial.c
AUX_SRC += mocks/rs485_stats.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_timer.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = repeater_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
