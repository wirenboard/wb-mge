#include "ethernet.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"

// QEMU build conditional includes
#if (QEMU_BUILD)
    #include "ethernet_qemu.h"
#else
    #include "driver/gpio.h"
#endif

#if (!QEMU_BUILD)
    #define ETH_PHY_RTL8201     1
    #define ETH_MDC_GPIO        GPIO_NUM_23
    #define ETH_MDIO_GPIO       GPIO_NUM_18
    #define ETH_PHY_RST_GPIO    GPIO_NUM_5
    #define ETH_PHY_ADDR        0           // LED0, LED1 are pulled down
    #define ETH_EXT_CLK_GPIO    GPIO_NUM_0  // External clock on GPIO0
#endif

static const char *TAG = "ethernet";
static esp_eth_handle_t s_eth_handle = NULL;
static esp_netif_t* s_eth_netif = NULL;

#if (QEMU_BUILD)

    esp_err_t ethernet_init(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname)
    {
        ESP_LOGI(TAG, "Initializing Ethernet for QEMU environment");
        return ethernet_init_qemu(eth_event_handler, static_ip, netif_hostname);
    }

#else

    esp_err_t ethernet_init(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname)
    {
        ESP_LOGI(TAG, "Initializing Ethernet for hardware");
        esp_err_t err = ESP_OK;

        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

        phy_config.phy_addr = ETH_PHY_ADDR;
        phy_config.reset_gpio_num = ETH_PHY_RST_GPIO;

        eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

        esp32_emac_config.clock_config.rmii.clock_gpio = ETH_EXT_CLK_GPIO;      // External clock on GPIO0

        esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
        esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;

        esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

        #if ETH_PHY_IP101
            esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);
        #elif ETH_PHY_RTL8201
            esp_eth_phy_t *phy = esp_eth_phy_new_rtl8201(&phy_config);
        #elif ETH_PHY_LAN87XX
            esp_eth_phy_t *phy = esp_eth_phy_new_lan87xx(&phy_config);
        #elif ETH_PHY_DP83848
            esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config);
        #elif ETH_PHY_KSZ80XX
            esp_eth_phy_t *phy = esp_eth_phy_new_ksz80xx(&phy_config);
        #endif

        esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

        err = esp_eth_driver_install(&config, &s_eth_handle);
        if (err != ESP_OK){
            if (s_eth_handle != NULL) {
                esp_eth_driver_uninstall(s_eth_handle);
                s_eth_handle = NULL;
            }
            if (mac != NULL) {
                mac->del(mac);
            }
            if (phy != NULL) {
                phy->del(phy);
            }
            return err;
        }

        ESP_ERROR_CHECK(esp_netif_init());
        esp_netif_t *eth_netif;
        esp_eth_netif_glue_handle_t eth_netif_glue;

        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netif = esp_netif_new(&cfg);
        eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_netif_glue));
        esp_netif_set_hostname(eth_netif, netif_hostname);
        if (static_ip != NULL) {
            esp_netif_dhcpc_stop(eth_netif);
            err = esp_netif_set_ip_info(eth_netif, static_ip);
            if (err != ESP_OK) {
                return err;
            }
        } else {
            err = esp_netif_dhcpc_start(eth_netif);
            if (err != ESP_OK) {
                return err;
            }
        }
        if (eth_event_handler != NULL) {
            err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
            if (err != ESP_OK) {
                return err;
            }
        }

        err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_event_handler, NULL);
        if (err != ESP_OK) {
            return err;
        }

        err = esp_eth_start(s_eth_handle);
        if (err != ESP_OK) {
            return err;
        }

        s_eth_netif = eth_netif;

        return ESP_OK;
    }

#endif // QEMU_BUILD

esp_eth_handle_t ethernet_get_handle(void)
{
    return s_eth_handle;
}

esp_err_t ethernet_set_ip_hostname(esp_netif_ip_info_t* static_ip, char * netif_hostname)
{
    if (s_eth_netif == NULL || s_eth_handle == NULL) {
        ESP_LOGE(TAG, "Ethernet is not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t result = ESP_OK;
    esp_err_t ret = esp_netif_set_hostname(s_eth_netif, netif_hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to set hostname");
        result = ESP_FAIL;
    }

    esp_netif_dhcpc_stop(s_eth_netif);

    if (static_ip) {
        ret = esp_netif_set_ip_info(s_eth_netif, static_ip);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to set static IP address");
            result = ESP_FAIL;
        }
    } else {
        ret = esp_netif_dhcpc_start(s_eth_netif);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Unable to start DHCP client");
            result = ESP_FAIL;
        }
    }

    esp_eth_stop(s_eth_handle);
    vTaskDelay(pdMS_TO_TICKS(500));
    ret = esp_eth_start(s_eth_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Unable to restart Ethernet interface");
        result = ESP_FAIL;
    }

    return result;
}
