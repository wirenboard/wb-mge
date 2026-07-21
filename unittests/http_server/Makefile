# Name of test / tests group
TEST_NAME = http_server_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/http_server.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/bridge
# template_handler.h — the device-template endpoints http_server.c registers
INC += $(PROJ_DIR)/main/mqtt_serial
# sys_info.h (used by http_server.c for the build-identity ETag) pulls in
# wb_app_desc.h and the ESP-IDF app-format headers; expose them on the host.
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include

# List of tests
TEST_LIST = http_server_test http_server_deinit_test http_server_settings_changed_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
