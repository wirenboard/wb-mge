// Unit tests for the PHY driver choice in ethernet_init() (main/network/ethernet.c).
//
// The WB-MGE v3 PHY footprint takes the RTL8201FI and the pin-compatible Motorcomm YT8522H.
// The IDF RTL8201 driver refuses the YT8522H in its init() with "wrong chip ID", and
// ethernet_init() then installs the driver again with the generic IEEE 802.3 PHY. A failed
// esp_eth_driver_install() frees the mediator the MAC still points at, so the retry must not
// call anything on that MAC that goes through the mediator (init/deinit), and it must not
// dereference a MAC that was never created.

#include <stdio.h>

#include "unity.h"

#include "esp_eth_mock.h"

// Included rather than linked so that setUp() can reset the static driver handle: production
// code calls ethernet_init() once per boot and never clears it.
#include "ethernet.c"

#define ETHERNET_TEST_MAC_ALWAYS_FAILS  1000

static char ethernet_test_hostname[] = "wb-mge-test";

static void ethernet_test_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_base;
    (void)event_id;
    (void)event_data;
}

static esp_err_t ethernet_test_init(void)
{
    return ethernet_init(ethernet_test_event_handler, NULL, ethernet_test_hostname);
}

static int ethernet_test_phys_created(void)
{
    return (mock_eth.phy_created[MOCK_ETH_PHY_KIND_RTL8201] + mock_eth.phy_created[MOCK_ETH_PHY_KIND_GENERIC]);
}

void setUp(void)
{
    s_eth_handle = NULL;
    s_eth_netif = NULL;
}

void tearDown(void)
{
}

static void test_ethernet_init_keeps_rtl8201_driver_for_rtl8201(void)
{
    mock_eth_reset(MOCK_ETH_CHIP_RTL8201);

    TEST_ASSERT_EQUAL(ESP_OK, ethernet_test_init());

    TEST_ASSERT_EQUAL(1, mock_eth.install_calls);
    TEST_ASSERT_EQUAL(MOCK_ETH_PHY_KIND_RTL8201, mock_eth.installed_phy_kind);
    TEST_ASSERT_EQUAL(0, mock_eth.phy_created[MOCK_ETH_PHY_KIND_GENERIC]);
    TEST_ASSERT_EQUAL(1, mock_eth.mac_created);
    TEST_ASSERT_EQUAL(0, mock_eth.mac_deleted);
    TEST_ASSERT_EQUAL_PTR(mock_eth.installed_handle, ethernet_get_handle());
}

static void test_ethernet_init_falls_back_to_generic_driver_for_other_phy(void)
{
    mock_eth_reset(MOCK_ETH_CHIP_YT8522H);

    TEST_ASSERT_EQUAL(ESP_OK, ethernet_test_init());

    TEST_ASSERT_EQUAL(2, mock_eth.install_calls);
    TEST_ASSERT_EQUAL(MOCK_ETH_PHY_KIND_GENERIC, mock_eth.installed_phy_kind);
    TEST_ASSERT_EQUAL_PTR(mock_eth.installed_handle, ethernet_get_handle());
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_eth.stale_mediator_calls,
                              "the MAC was used through the mediator freed by the failed installation");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_eth.deleted_object_calls, "a deleted MAC or PHY was used");
    // Exactly the installed MAC and PHY stay alive: the objects of the failed attempt are released.
    TEST_ASSERT_EQUAL(1, (mock_eth.mac_created - mock_eth.mac_deleted));
    TEST_ASSERT_EQUAL(1, (ethernet_test_phys_created() - mock_eth.phy_deleted));
}

static void test_ethernet_init_releases_everything_when_no_phy_answers(void)
{
    mock_eth_reset(MOCK_ETH_CHIP_NONE);

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ethernet_test_init());

    TEST_ASSERT_NULL(ethernet_get_handle());
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_eth.stale_mediator_calls,
                              "the MAC was used through the mediator freed by the failed installation");
    TEST_ASSERT_EQUAL_MESSAGE(0, mock_eth.deleted_object_calls, "a deleted MAC or PHY was used");
    TEST_ASSERT_EQUAL(mock_eth.mac_created, mock_eth.mac_deleted);
    TEST_ASSERT_EQUAL(ethernet_test_phys_created(), mock_eth.phy_deleted);
}

// Before the fallback existed this case ended in a plain error return, and the board booted
// without Ethernet but with WiFi. A crash here (NULL MAC dereferenced) takes the whole test
// binary down, so the test runs last.
static void test_ethernet_init_survives_mac_creation_failure(void)
{
    mock_eth_reset(MOCK_ETH_CHIP_RTL8201);
    mock_eth.mac_new_failures = ETHERNET_TEST_MAC_ALWAYS_FAILS;

    TEST_ASSERT_NOT_EQUAL(ESP_OK, ethernet_test_init());

    TEST_ASSERT_NULL(ethernet_get_handle());
    TEST_ASSERT_EQUAL(0, mock_eth.mac_created);
    TEST_ASSERT_EQUAL(ethernet_test_phys_created(), mock_eth.phy_deleted);
}

int main(void)
{
    // The runner redirects stdout to a file, where it would be fully buffered: without this,
    // a crash in the last test would also swallow the results of the ones before it.
    setvbuf(stdout, NULL, _IONBF, 0);

    UNITY_BEGIN();

    RUN_TEST(test_ethernet_init_keeps_rtl8201_driver_for_rtl8201);
    RUN_TEST(test_ethernet_init_falls_back_to_generic_driver_for_other_phy);
    RUN_TEST(test_ethernet_init_releases_everything_when_no_phy_answers);
    RUN_TEST(test_ethernet_init_survives_mac_creation_failure);

    return UNITY_END();
}
