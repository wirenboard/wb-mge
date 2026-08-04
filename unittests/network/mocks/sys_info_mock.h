#pragma once

// Control side of the sys_info mock. The struct itself is the real one from main/sys_info.h;
// only its storage is provided here, because main/sys_info.c would drag in efuse, PSRAM and
// the app descriptor while network.c does nothing but write fields of the global.

// Clear every field, so each test starts from a cold boot. network_init() and a successful
// network_update_wifi_settings() both write here, and the global outlives a test just like
// network.c's own cached settings do.
void mock_sys_info_reset(void);
