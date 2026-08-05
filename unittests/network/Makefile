# Name of test / tests group
TEST_NAME = network_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/network/network.c

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c

# Include directories.
#
# Order matters twice here:
#   * mocks/ first, so lwip/ip4_addr.h, sys/socket.h and mdns.h resolve to the stubs in
#     this suite instead of headers that need the IDF port layer or a managed component;
#   * esp_hw_support/include BEFORE the shared unittests/mocks, because network.c formats
#     MAC addresses with MACSTR / MAC2STR, which live in the real esp_mac.h. The shared
#     mock of that header stops at esp_read_mac() and is not needed here at all.
# Everything else is ordinary: the real IDF headers, and the shared mocks for esp_log and
# sdkconfig.
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(IDF_PATH)/components/esp_hw_support/include
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/network
INC += $(PROJ_DIR)/main/bridge
INC += $(PROJ_DIR)/main/wb_app_desc
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_netif/include
INC += $(IDF_PATH)/components/esp_wifi/include
INC += $(IDF_PATH)/components/esp_eth/include
INC += $(IDF_PATH)/components/esp_event/include
INC += $(IDF_PATH)/components/esp_app_format/include
INC += $(IDF_PATH)/components/bootloader_support/include
INC += $(IDF_PATH)/components/hal/include
INC += $(IDF_PATH)/components/soc/include
INC += $(IDF_PATH)/components/soc/esp32/include
# soc/esp32/include being on the path makes esp_err_to_name.c pick up soc/soc.h through its
# __has_include probe, and that header needs the register definitions.
INC += $(IDF_PATH)/components/soc/esp32/register

# esp_eth_driver.h declares esp_eth_mac_t only through one of the CONFIG_ETH_USE_* branches;
# the ESP32 EMAC is the one this board actually has. Nothing in this suite drives Ethernet —
# it only has to compile, because network.c and ethernet.h share a translation unit.
DEFS += CONFIG_ETH_USE_ESP32_EMAC=1

# List of tests
TEST_LIST = network_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
