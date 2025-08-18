
UNIT_FOLDER = ../main
MOCK_FOLDER = mocks
TEST_LIST = setting_items_test

# Include both main project and mocks for headers  
INC = . ../main/ $(MOCK_FOLDER) $(IDF_PATH)/components/esp_common/include

all: $(TEST_LIST)

%_test: %_test.c $(UNIT_FOLDER)/%.c $(wildcard $(MOCK_FOLDER)/*.c) $(IDF_PATH)/components/esp_common/src/esp_err_to_name.c
	gcc -g -D_GNU_SOURCE $(addprefix -I, $(INC)) $^ -o $@
	./$@

clean:
	rm -f $(TEST_LIST)
