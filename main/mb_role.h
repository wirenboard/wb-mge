#pragma once

/*
 * The build-time Modbus role, as three mutually exclusive compile-time constants.
 *
 * The role is chosen once for the whole firmware by the Kconfig choice in
 * main/Kconfig.projbuild (Makefile: MB_ROLE=slave|master|none). It is deliberately NOT a
 * runtime setting: "none" exists to keep the esp-modbus component out of the image
 * entirely, which no runtime flag can do.
 *
 * This header carries the macros and nothing else - no includes of mb_slave.h or
 * mb_master.h. That is what lets the files which only need to know the role
 * (setting_items.c, settings_manager.c) include it without dragging a transport header
 * into a translation unit - and, in the host unit-test build, without needing one of the
 * mb_* mock headers on their include path. The files that actually call into the stack
 * include the role's own header themselves, guarded the same way.
 *
 * CONFIG_WB_MODBUS_ROLE_* reach here through sdkconfig.h, which ESP-IDF force-includes
 * into every translation unit. The host unit-test build has no sdkconfig.h at all, so
 * none of them is defined there and the role resolves to slave - the role every existing
 * unit test is written against.
 */

#if defined(CONFIG_WB_MODBUS_ROLE_MASTER)
    #define WB_MB_ROLE_MASTER   1
#else
    #define WB_MB_ROLE_MASTER   0
#endif

#if defined(CONFIG_WB_MODBUS_ROLE_NONE)
    #define WB_MB_ROLE_NONE     1
#else
    #define WB_MB_ROLE_NONE     0
#endif

// Slave is the default: it is what "neither of the other two" means, so a build with no
// CONFIG_WB_MODBUS_ROLE_* at all (the unit tests) is a slave build.
#define WB_MB_ROLE_SLAVE        ((!WB_MB_ROLE_MASTER) && (!WB_MB_ROLE_NONE))
