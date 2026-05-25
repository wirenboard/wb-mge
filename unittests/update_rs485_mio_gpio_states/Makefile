# Name of test / tests group
TEST_NAME = update_rs485_mio_gpio_states_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/update_rs485_mio_gpio_states.c

# Auxiliary source files used in this test
AUX_SRC += mocks/rs485_control.c
AUX_SRC += mocks/mio_control.c
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/port_manager.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = update_rs485_mio_gpio_states_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
