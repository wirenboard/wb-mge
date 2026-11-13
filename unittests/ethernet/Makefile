# Name of test / tests group
TEST_NAME = ethernet_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# No TESTED_SRC: ethernet.c is #included by the test, so that setUp() can reset its static
# driver handle.

# Auxiliary source files used in this test
AUX_SRC += mocks/*.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories.
#
# mocks/ comes first, so driver/gpio.h resolves to the suite's own stub (it needs the
# Ethernet pins, which the shared mock does not list). The shared mocks then cover esp_log,
# sdkconfig and FreeRTOS; the esp_eth, esp_netif and esp_event headers are the real IDF ones.
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main/network
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/esp_netif/include
INC += $(IDF_PATH)/components/esp_eth/include
INC += $(IDF_PATH)/components/esp_event/include
INC += $(IDF_PATH)/components/hal/include
INC += $(IDF_PATH)/components/soc/include
INC += $(IDF_PATH)/components/soc/esp32/include
# soc/esp32/include being on the path makes esp_err_to_name.c pick up soc/soc.h through its
# __has_include probe, and that header needs the register definitions.
INC += $(IDF_PATH)/components/soc/esp32/register

# The hardware branch of ethernet.c, with the ESP32 EMAC and its RMII clock taken from the
# PHY on GPIO0 — the same choice sdkconfig makes for the board.
DEFS += QEMU_BUILD=0
DEFS += CONFIG_IDF_TARGET_ESP32=1
DEFS += CONFIG_ETH_USE_ESP32_EMAC=1
DEFS += CONFIG_ETH_RMII_CLK_INPUT=1
DEFS += CONFIG_ETH_RMII_CLK_IN_GPIO=0

# List of tests
TEST_LIST = ethernet_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
