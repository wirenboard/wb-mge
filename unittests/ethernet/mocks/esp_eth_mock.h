#pragma once

// Fake ESP-IDF Ethernet driver for the ethernet_init() tests.
//
// It keeps the parts of the IDF v5.4 behaviour that decide whether the PHY fallback in
// ethernet_init() is correct:
//   * esp_eth_driver_install() hands the MAC and the PHY a mediator that lives inside its
//     own driver object, and when the installation fails it frees that object while the
//     MAC still points at the mediator inside it;
//   * the MAC's init() and deinit() go through that mediator (emac_esp32_init() and
//     emac_esp32_deinit() call eth->on_state_changed()), its del() does not;
//   * the RTL8201 driver refuses any chip whose ID is not the RTL8201 one, the generic
//     IEEE 802.3 driver accepts any PHY that answers on MDIO.
// Every object comes from a static pool and is never really freed, so a call through a
// dead mediator or into a deleted object is counted instead of being undefined behaviour.

#include <stdbool.h>

#include "esp_eth.h"

// The PHY soldered on the fake board.
typedef enum {
    MOCK_ETH_CHIP_RTL8201,
    MOCK_ETH_CHIP_YT8522H,                              // pin-compatible, fails the RTL8201 ID check
    MOCK_ETH_CHIP_NONE,                                 // nothing answers on MDIO, every PHY init fails
} mock_eth_chip_t;

// The PHY driver an object was created with.
typedef enum {
    MOCK_ETH_PHY_KIND_RTL8201,
    MOCK_ETH_PHY_KIND_GENERIC,
    MOCK_ETH_PHY_KIND_COUNT,
} mock_eth_phy_kind_t;

typedef struct {
    // Inputs, set by the test before calling ethernet_init().
    mock_eth_chip_t chip;
    int mac_new_failures;                               // how many next esp_eth_mac_new_esp32() calls return NULL

    // Outputs.
    int mac_created;
    int mac_deleted;
    int phy_created[MOCK_ETH_PHY_KIND_COUNT];
    int phy_deleted;
    int install_calls;
    int stale_mediator_calls;                           // MAC init()/deinit() through the mediator of a failed install
    int deleted_object_calls;                           // any method called on a MAC or a PHY after its del()
    esp_eth_handle_t installed_handle;                  // handle of the last successful installation
    mock_eth_phy_kind_t installed_phy_kind;             // PHY driver of the last successful installation
} mock_eth_t;

extern mock_eth_t mock_eth;

void mock_eth_reset(mock_eth_chip_t chip);
