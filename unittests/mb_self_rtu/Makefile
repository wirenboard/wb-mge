# Name of test / tests group
TEST_NAME = mb_self_rtu_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/mb_self_rtu.c

# Auxiliary source files used in this test.
# mb_device is mocked: this suite is about the interception state machine, and the
# responder itself has its own suite (unittests/mb_device).
AUX_SRC += $(PROJ_DIR)/main/bridge/stream_splitter.c
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += mocks/mb_device.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = mb_self_rtu_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
