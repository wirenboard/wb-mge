# Name of test / tests group
TEST_NAME = serial_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/serial.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/event_groups.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/queue.c
AUX_SRC += $(UNITTEST_DIR)/mocks/driver/uart.c

# Include directories
# INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/bridge
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = serial_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
