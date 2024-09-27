
UNIT_FOLDER = ../main
MOCK_FOLDER = mocks
TEST_LIST = setting_items_test

INC = . ../main/ $(MOCK_FOLDER)

all: $(TEST_LIST)

%_test: %_test.c $(UNIT_FOLDER)/%.c $(wildcard $(MOCK_FOLDER)/*.c)
	gcc -g -mx32 $(addprefix -I, $(INC)) $^ -o $@
	./$@

clean:
	rm -f $(TEST_LIST)
