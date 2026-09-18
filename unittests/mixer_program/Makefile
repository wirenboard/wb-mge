# Name of test / tests group
TEST_NAME = mixer_program_test

# Project root directory
PROJ_DIR = ../..

# Unit test directory
UNITTEST_DIR = ..

# Source files to be checked
TESTED_SRC += $(PROJ_DIR)/main/mixer/program.c

# Auxiliary source files used in this test
# The REAL mbedtls base64, the same implementation the firmware links: a mock of it would
# only prove that the codec this test wrote agrees with itself.
AUX_SRC += $(IDF_PATH)/components/mbedtls/mbedtls/library/base64.c
AUX_SRC += $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c

# Include directories
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/mixer
INC += $(IDF_PATH)/components/esp_common/include
INC += $(IDF_PATH)/components/mbedtls/mbedtls/include
# base64.c includes the library-private "common.h"
INC += $(IDF_PATH)/components/mbedtls/mbedtls/library

# List of tests
TEST_LIST = mixer_program_test

# Include base unit test build system
include $(PROJ_DIR)/unittests/build_unittests.mk
