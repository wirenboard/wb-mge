# Name of test / tests group
TEST_NAME = info_handlers_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/info_handlers.c

# Auxiliary source files used in this test
AUX_SRC += mocks/setting_items.c
AUX_SRC += mocks/auth.c
AUX_SRC += mocks/json_utils.c
AUX_SRC += mocks/port_manager.c
AUX_SRC += mocks/bridge.c
AUX_SRC += mocks/repeater.c
AUX_SRC += mocks/voltage_monitor.c
AUX_SRC += mocks/config_button.c
AUX_SRC += mocks/sys_info.c
AUX_SRC += mocks/esp_wifi.c
AUX_SRC += mocks/esp_netif.c
AUX_SRC += mocks/esp_heap_caps.c
AUX_SRC += $(UNITTEST_DIR)/mocks/esp_timer.c
AUX_SRC += $(IDF_PATH)/components/json/cJSON/cJSON.c

# Include directories (order matters: mocks first to override ESP-IDF headers)
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/network
INC += $(PROJ_DIR)/main/voltage_monitor
# sys_info.h pulls in wb_app_desc.h and the ESP-IDF app-format headers; expose
# them on the host (mirrors the http_server unit-test setup).
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/json/cJSON
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/esp_app_format/include

# List of tests
TEST_LIST = info_handlers_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
