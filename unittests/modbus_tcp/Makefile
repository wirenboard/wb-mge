# Name of test / tests group
TEST_NAME = modbus_tcp_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/modbus_tcp.c

# Auxiliary source files used in this test
AUX_SRC += mocks/packet_queue.c
AUX_SRC += mocks/tcp_server.c
AUX_SRC += mocks/rs485_stats.c
AUX_SRC += mocks/fast_modbus.c
AUX_SRC += mocks/serial.c
AUX_SRC += mocks/mb_device.c
AUX_SRC += $(PROJ_DIR)/main/bridge/mbtcp_reasm.c
AUX_SRC += $(PROJ_DIR)/main/bridge/modbus_helpers.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/event_groups.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/queue.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += mocks/event_groups_extra.c

# Include directories (order matters: mocks first to override ESP-IDF headers)
INC += .
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(UNITTEST_DIR)/mocks/driver
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# Define configTICK_RATE_HZ used by calc_response_timeout_ticks (unused in tests but must compile)
DEFS += configTICK_RATE_HZ=500

# Force-include esp_bit_defs.h so that BIT0/BIT8/etc macros resolve when compiling modbus_tcp.c
# (The real IDF FreeRTOS would pull these in, but the unit-test mock FreeRTOS.h does not.)
GCC_FLAGS += -include mocks/esp_bit_defs.h

# List of tests
TEST_LIST = modbus_tcp_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
