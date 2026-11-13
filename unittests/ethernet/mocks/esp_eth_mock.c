#include <stddef.h>
#include <string.h>

#include "esp_eth_mock.h"

#define MOCK_ETH_POOL_SIZE  8

typedef struct {
    esp_eth_mediator_t mediator;                        // first member: a mediator pointer is also a driver pointer
    bool alive;                                         // false once the installation that created it failed
} mock_eth_driver_t;

typedef struct {
    esp_eth_mac_t parent;                               // first member: casts to and from esp_eth_mac_t work
    esp_eth_mediator_t *mediator;
    bool deleted;
} mock_eth_mac_t;

typedef struct {
    esp_eth_phy_t parent;                               // first member: casts to and from esp_eth_phy_t work
    esp_eth_mediator_t *mediator;
    mock_eth_phy_kind_t kind;
    bool deleted;
} mock_eth_phy_t;

mock_eth_t mock_eth;

static mock_eth_driver_t mock_eth_drivers[MOCK_ETH_POOL_SIZE];
static mock_eth_mac_t mock_eth_macs[MOCK_ETH_POOL_SIZE];
static mock_eth_phy_t mock_eth_phys[MOCK_ETH_POOL_SIZE];
static int mock_eth_drivers_used;
static int mock_eth_macs_used;
static int mock_eth_phys_used;

static void mock_eth_check_mediator(esp_eth_mediator_t *mediator);
static esp_err_t mock_eth_mac_set_mediator(esp_eth_mac_t *mac, esp_eth_mediator_t *eth);
static esp_err_t mock_eth_mac_init(esp_eth_mac_t *mac);
static esp_err_t mock_eth_mac_deinit(esp_eth_mac_t *mac);
static esp_err_t mock_eth_mac_del(esp_eth_mac_t *mac);
static esp_err_t mock_eth_phy_set_mediator(esp_eth_phy_t *phy, esp_eth_mediator_t *eth);
static esp_err_t mock_eth_phy_init(esp_eth_phy_t *phy);
static esp_err_t mock_eth_phy_deinit(esp_eth_phy_t *phy);
static esp_err_t mock_eth_phy_del(esp_eth_phy_t *phy);
static esp_eth_phy_t *mock_eth_phy_new(mock_eth_phy_kind_t kind);

void mock_eth_reset(mock_eth_chip_t chip)
{
    memset(&mock_eth, 0, sizeof(mock_eth));
    memset(mock_eth_drivers, 0, sizeof(mock_eth_drivers));
    memset(mock_eth_macs, 0, sizeof(mock_eth_macs));
    memset(mock_eth_phys, 0, sizeof(mock_eth_phys));
    mock_eth_drivers_used = 0;
    mock_eth_macs_used = 0;
    mock_eth_phys_used = 0;
    mock_eth.chip = chip;
}

static void mock_eth_check_mediator(esp_eth_mediator_t *mediator)
{
    mock_eth_driver_t *driver = (mock_eth_driver_t *)mediator;
    if ((driver == NULL) || (!driver->alive)) {
        mock_eth.stale_mediator_calls++;
    }
}

static esp_err_t mock_eth_mac_set_mediator(esp_eth_mac_t *mac, esp_eth_mediator_t *eth)
{
    mock_eth_mac_t *mock_mac = (mock_eth_mac_t *)mac;
    if (mock_mac->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_mac->mediator = eth;
    return ESP_OK;
}

// emac_esp32_init() reports ETH_STATE_LLINIT through the mediator.
static esp_err_t mock_eth_mac_init(esp_eth_mac_t *mac)
{
    mock_eth_mac_t *mock_mac = (mock_eth_mac_t *)mac;
    if (mock_mac->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_eth_check_mediator(mock_mac->mediator);
    return ESP_OK;
}

// emac_esp32_deinit() reports ETH_STATE_DEINIT through the mediator.
static esp_err_t mock_eth_mac_deinit(esp_eth_mac_t *mac)
{
    mock_eth_mac_t *mock_mac = (mock_eth_mac_t *)mac;
    if (mock_mac->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_eth_check_mediator(mock_mac->mediator);
    return ESP_OK;
}

// emac_esp32_del() frees the driver object without touching the mediator.
static esp_err_t mock_eth_mac_del(esp_eth_mac_t *mac)
{
    mock_eth_mac_t *mock_mac = (mock_eth_mac_t *)mac;
    if (mock_mac->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_mac->deleted = true;
    mock_eth.mac_deleted++;
    return ESP_OK;
}

static esp_err_t mock_eth_phy_set_mediator(esp_eth_phy_t *phy, esp_eth_mediator_t *eth)
{
    mock_eth_phy_t *mock_phy = (mock_eth_phy_t *)phy;
    if (mock_phy->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_phy->mediator = eth;
    return ESP_OK;
}

static esp_err_t mock_eth_phy_init(esp_eth_phy_t *phy)
{
    mock_eth_phy_t *mock_phy = (mock_eth_phy_t *)phy;
    if (mock_phy->deleted) {
        mock_eth.deleted_object_calls++;
    }
    if (mock_eth.chip == MOCK_ETH_CHIP_NONE) {
        return ESP_FAIL;
    }
    // rtl8201_init() fails with "wrong chip ID" unless OUI 0x732 and model 0x1 come back.
    if ((mock_phy->kind == MOCK_ETH_PHY_KIND_RTL8201) && (mock_eth.chip != MOCK_ETH_CHIP_RTL8201)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t mock_eth_phy_deinit(esp_eth_phy_t *phy)
{
    mock_eth_phy_t *mock_phy = (mock_eth_phy_t *)phy;
    if (mock_phy->deleted) {
        mock_eth.deleted_object_calls++;
    }
    return ESP_OK;
}

static esp_err_t mock_eth_phy_del(esp_eth_phy_t *phy)
{
    mock_eth_phy_t *mock_phy = (mock_eth_phy_t *)phy;
    if (mock_phy->deleted) {
        mock_eth.deleted_object_calls++;
    }
    mock_phy->deleted = true;
    mock_eth.phy_deleted++;
    return ESP_OK;
}

esp_eth_mac_t *esp_eth_mac_new_esp32(const eth_esp32_emac_config_t *esp32_config, const eth_mac_config_t *config)
{
    (void)esp32_config;
    (void)config;
    if (mock_eth.mac_new_failures > 0) {
        mock_eth.mac_new_failures--;
        return NULL;
    }
    if (mock_eth_macs_used >= MOCK_ETH_POOL_SIZE) {
        return NULL;
    }
    mock_eth_mac_t *mock_mac = &mock_eth_macs[mock_eth_macs_used];
    mock_eth_macs_used++;
    mock_mac->parent.set_mediator = mock_eth_mac_set_mediator;
    mock_mac->parent.init = mock_eth_mac_init;
    mock_mac->parent.deinit = mock_eth_mac_deinit;
    mock_mac->parent.del = mock_eth_mac_del;
    mock_eth.mac_created++;
    return &mock_mac->parent;
}

static esp_eth_phy_t *mock_eth_phy_new(mock_eth_phy_kind_t kind)
{
    if (mock_eth_phys_used >= MOCK_ETH_POOL_SIZE) {
        return NULL;
    }
    mock_eth_phy_t *mock_phy = &mock_eth_phys[mock_eth_phys_used];
    mock_eth_phys_used++;
    mock_phy->kind = kind;
    mock_phy->parent.set_mediator = mock_eth_phy_set_mediator;
    mock_phy->parent.init = mock_eth_phy_init;
    mock_phy->parent.deinit = mock_eth_phy_deinit;
    mock_phy->parent.del = mock_eth_phy_del;
    mock_eth.phy_created[kind]++;
    return &mock_phy->parent;
}

esp_eth_phy_t *esp_eth_phy_new_rtl8201(const eth_phy_config_t *config)
{
    (void)config;
    return mock_eth_phy_new(MOCK_ETH_PHY_KIND_RTL8201);
}

esp_eth_phy_t *esp_eth_phy_new_generic(const eth_phy_config_t *config)
{
    (void)config;
    return mock_eth_phy_new(MOCK_ETH_PHY_KIND_GENERIC);
}

esp_err_t esp_eth_driver_install(const esp_eth_config_t *config, esp_eth_handle_t *out_hdl)
{
    mock_eth.install_calls++;
    if ((config == NULL) || (out_hdl == NULL) || (config->mac == NULL) || (config->phy == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }
    if (mock_eth_drivers_used >= MOCK_ETH_POOL_SIZE) {
        return ESP_ERR_NO_MEM;
    }
    mock_eth_driver_t *driver = &mock_eth_drivers[mock_eth_drivers_used];
    mock_eth_drivers_used++;
    driver->alive = true;

    config->mac->set_mediator(config->mac, &driver->mediator);
    config->phy->set_mediator(config->phy, &driver->mediator);
    esp_err_t ret = config->mac->init(config->mac);
    if (ret == ESP_OK) {
        ret = config->phy->init(config->phy);
    }
    if (ret != ESP_OK) {
        // The real function frees the driver object here; the MAC and the PHY are left
        // pointing at the mediator inside it, and the MAC stays initialised.
        driver->alive = false;
        return ret;
    }

    mock_eth.installed_handle = (esp_eth_handle_t)driver;
    mock_eth.installed_phy_kind = ((mock_eth_phy_t *)config->phy)->kind;
    *out_hdl = (esp_eth_handle_t)driver;
    return ESP_OK;
}

esp_err_t esp_eth_driver_uninstall(esp_eth_handle_t hdl)
{
    (void)hdl;
    return ESP_OK;
}

esp_err_t esp_eth_start(esp_eth_handle_t hdl)
{
    (void)hdl;
    return ESP_OK;
}

esp_err_t esp_eth_stop(esp_eth_handle_t hdl)
{
    (void)hdl;
    return ESP_OK;
}

esp_eth_netif_glue_handle_t esp_eth_new_netif_glue(esp_eth_handle_t eth_hdl)
{
    return (esp_eth_netif_glue_handle_t)eth_hdl;
}
