# Name of test / tests group
TEST_NAME = slot_relay_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/slot_relay.c

# Auxiliary source files used in this test
AUX_SRC += $(UNITTEST_DIR)/mocks/driver/gpio.c

# Include directories (order matters: local mocks first)
INC += mocks
INC += $(UNITTEST_DIR)/port_manager/mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = slot_relay_test

DEFS += MODEL_mgu_v1

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
