# Name of test / tests group
TEST_NAME = tcp_server_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/bridge/tcp_server.c

# Auxiliary source files used in this test
AUX_SRC += mocks/lwip/sockets.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/event_groups.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/malloc.c
AUX_SRC += $(UNITTEST_DIR)/mocks/debug_log.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main
INC += $(IDF_PATH)/components/esp_common/include

# Force-include esp_bit_defs.h so that BIT1/BIT8/etc macros resolve when
# compiling tcp_server.c (the real IDF FreeRTOS would pull these in, but the
# unit-test mock FreeRTOS.h does not).
GCC_FLAGS += -include mocks/esp_bit_defs.h

# List of tests
TEST_LIST = tcp_server_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
