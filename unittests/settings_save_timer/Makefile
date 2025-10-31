# Name of test / tests group
TEST_NAME = settings_save_timer_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/settings_save_timer.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/event_groups.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/timers.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = settings_save_timer_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
