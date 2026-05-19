# Name of test / tests group
TEST_NAME = voltage_monitor_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/voltage_monitor/voltage_monitor.c

# Auxiliary source files used in this test
AUX_SRC += mocks/system_voltage.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/debug_log.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/voltage_monitor
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = voltage_monitor_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
