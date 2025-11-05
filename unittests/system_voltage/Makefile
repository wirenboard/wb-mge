# Name of test / tests group
TEST_NAME = system_voltage_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/voltage_monitor/system_voltage.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main/voltage_monitor
INC += $(IDF_PATH)/components/esp_adc/include
INC += $(IDF_PATH)/components/esp_adc/esp32/include
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/hal/include
INC += $(IDF_PATH)/components/soc/esp32/include
INC += $(IDF_PATH)/components/soc/esp32/register

# List of tests
TEST_LIST = system_voltage_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
