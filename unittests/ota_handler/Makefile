# Name of test / tests group
TEST_NAME = ota_handler_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/ota_handler.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
# Real implementation: the handler validates the firmware descriptor with it, and a mock would only
# reimplement the very parsing under test.
AUX_SRC += $(PROJ_DIR)/main/wb_app_desc/wb_app_desc.c

# Include directories (order matters: mocks first to override the ESP-IDF headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
# sys_info.h pulls in bridge.h/serial.h and, through wb_app_desc.h, the ESP-IDF app-format headers;
# expose them on the host (mirrors the http_server and info_handlers unit-test setup).
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include

# List of tests. One binary per scenario on purpose: the OTA state guard is a file-static with no
# reset hook, so a test that leaves it set would decide the outcome of every test after it.
TEST_LIST = ota_guard_repeat_test ota_guard_reset_test ota_guard_auth_order_test ota_response_failure_test
TEST_LIST += ota_stall_abort_test ota_stall_recovery_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
