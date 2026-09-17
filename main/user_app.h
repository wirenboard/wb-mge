#pragma once

// Start the example application task. Called from app_main().
//
// There is one implementation per Modbus role, and exactly one of them is compiled:
// main/mb_slave/user_app.c in the slave role, main/mb_master/user_app.c in the master role.
// In the "none" role neither is compiled and app_main does not call this at all.
void user_app_start(void);
