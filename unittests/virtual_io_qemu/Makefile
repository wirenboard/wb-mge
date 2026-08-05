TEST_NAME = virtual_io_qemu_test
PROJ_DIR = ../..
UNITTEST_DIR = ..
# virtual_io_qemu.c is #included by the test (not compiled separately) -> TESTED_SRC empty.
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/task.c
AUX_SRC += $(UNITTEST_DIR)/mocks/freertos/semphr.c
AUX_SRC += $(UNITTEST_DIR)/call_sequence.c
AUX_SRC += mocks/lwip/sockets.c
INC += .
INC += mocks
INC += $(UNITTEST_DIR)
INC += $(UNITTEST_DIR)/mocks
INC += $(UNITTEST_DIR)/mocks/freertos
INC += $(PROJ_DIR)/main
INC += $(PROJ_DIR)/main/qemu
INC += $(IDF_PATH)/components/esp_common/include
TEST_LIST = virtual_io_qemu_test
include $(PROJ_DIR)/unittests/build_unittests.mk
