# Name of test / tests group
TEST_NAME = gpio_shim_qemu_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/qemu/gpio_shim_qemu.c
TESTED_SRC += $(PROJ_DIR)/main/qemu/gpio_dir_map.c

# Include directories. The local mocks dir must come BEFORE any IDF path so
# "driver/gpio.h", "driver/uart.h", "hal/gpio_types.h" and "esp_attr.h" resolve
# to the local mocks (esp_err.h intentionally comes from the IDF esp_common include).
INC += .
INC += mocks
INC += $(PROJ_DIR)/main/qemu
INC += $(UNITTEST_DIR)
# Shared mocks dir: supplies sdkconfig.h pulled in by the IDF esp_err.h chain.
# Listed AFTER the local mocks so the local driver/hal headers take precedence.
INC += $(UNITTEST_DIR)/mocks
INC += $(IDF_PATH)/components/esp_common/include

# List of tests
TEST_LIST = gpio_shim_qemu_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
