# Name of test / tests group
TEST_NAME = gpio_expander_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/gpio_expander.c

# Auxiliary source files used in this test
AUX_SRC += mocks/driver/i2c_master.c
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/debug_log.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_log_level.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main/
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include/

# List of tests
TEST_LIST = gpio_expander_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
