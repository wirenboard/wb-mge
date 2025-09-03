#include "setting_items.h"
#include <string.h>

static int mock_web_port = 80;

int setting_items_read_int(const char *key)
{
    if (strcmp(key, KEY_WEB_PORT) == 0) {
        return mock_web_port;
    }
    return 0; // Default value for unknown keys
}

void mock_setting_items_set_web_port(int port)
{
    mock_web_port = port;
}
