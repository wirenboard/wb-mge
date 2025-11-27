# Name of test / tests group
TEST_NAME = fast_modbus_tests

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += ${PROJ_DIR}/main/bridge/fast_modbus.c

# Auxiliary source files used in this test
AUX_SRC += mocks/tcp_server.c
AUX_SRC += $(UNITTEST_DIR)/mocks/malloc.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_gdbstub/test_gdbstub_host/rv_decode/include

# List of tests
TEST_LIST = fast_modbus_send_probe_response_test fast_modbus_truncate_ff_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
