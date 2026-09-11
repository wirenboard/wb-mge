#pragma once

/* Minimal mock of esp_system.h for the cmd_handler unit test. Only esp_restart() is referenced,
 * from the reboot task; it is declared without the real header's noreturn attribute so the mock
 * can count the call and return. */

void esp_restart(void);

extern int mock_esp_restart_called;

void mock_esp_system_reset(void);
