# Name of test / tests group
TEST_NAME = rs485_control_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/rs485_control.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/gpio_expander/gpio_expander.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories
INC += .
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/gpio_expander
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = rs485_control_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
