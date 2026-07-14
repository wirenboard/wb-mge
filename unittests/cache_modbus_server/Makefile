# Name of test / tests group
TEST_NAME = cache_modbus_server_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked.
# cache_modbus_server.c is compiled via cache_modbus_server_shims.c, which #includes
# it so the test-only shims can reach its statics (not compiled separately -> not
# listed here, or the linker would see duplicate symbols).
TESTED_SRC +=

# Auxiliary source files used in this test
AUX_SRC += cache_modbus_server_shims.c
AUX_SRC += mocks/cache_multimaster.c
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/tcp_server.c
AUX_SRC += mocks/mb_device.c
AUX_SRC += $(PROJ_DIR)/main/bridge/mbtcp_reasm.c
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c

# Include directories (order matters: mocks first to override ESP-IDF headers)
INC += .
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = cache_modbus_server_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
