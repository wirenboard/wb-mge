#include "ethernet_qemu.h"
#include "esp_netif.h"
#include "esp_netif_types.h"
#include "esp_eth.h"
#include "esp_mac.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_check.h"

#ifdef CONFIG_ETH_USE_OPENETH
    #include "esp_eth_driver.h"
    #include "esp_eth_netif_glue.h"
#endif

static const char *TAG = "ethernet_qemu";
static esp_eth_handle_t s_eth_handle = NULL;

#ifdef CONFIG_ETH_USE_OPENETH

    esp_err_t ethernet_init_qemu(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname)
    {
        ESP_LOGI(TAG, "Initializing Ethernet for QEMU with OpenEth");

        esp_err_t err = ESP_OK;

        // OpenEth configuration for QEMU
        eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
        mac_config.rx_task_stack_size = 4096;
        mac_config.rx_task_prio = 15;

        esp_eth_mac_t *mac = esp_eth_mac_new_openeth(&mac_config);
        ESP_RETURN_ON_FALSE(mac, ESP_ERR_NO_MEM, TAG, "create OpenEth MAC failed");

        eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
        esp_eth_phy_t *phy = esp_eth_phy_new_dp83848(&phy_config); // Use DP83848 PHY for QEMU
        ESP_RETURN_ON_FALSE(phy, ESP_ERR_NO_MEM, TAG, "create PHY failed");

        esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);

        // Install Ethernet driver
        err = esp_eth_driver_install(&config, &s_eth_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Ethernet driver install failed: %s", esp_err_to_name(err));
            if (s_eth_handle != NULL) {
                esp_eth_driver_uninstall(s_eth_handle);
            }
            if (mac != NULL) {
                mac->del(mac);
            }
            if (phy != NULL) {
                phy->del(phy);
            }
            return err;
        }

        // Initialize network interface
        ESP_ERROR_CHECK(esp_netif_init());
        esp_netif_t *eth_netif;
        esp_eth_netif_glue_handle_t eth_netif_glue;

        esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
        eth_netif = esp_netif_new(&cfg);
        ESP_RETURN_ON_FALSE(eth_netif, ESP_ERR_NO_MEM, TAG, "create netif failed");

        eth_netif_glue = esp_eth_new_netif_glue(s_eth_handle);
        ESP_RETURN_ON_FALSE(eth_netif_glue, ESP_ERR_NO_MEM, TAG, "create netif glue failed");

        ESP_ERROR_CHECK(esp_netif_attach(eth_netif, eth_netif_glue));

        // Set hostname
        if (netif_hostname != NULL) {
            esp_netif_set_hostname(eth_netif, netif_hostname);
        }

        // Configure IP settings
        if (static_ip != NULL) {
            ESP_LOGI(TAG, "Setting static IP configuration");
            esp_netif_dhcpc_stop(eth_netif);
            err = esp_netif_set_ip_info(eth_netif, static_ip);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to set static IP: %s", esp_err_to_name(err));
                return err;
            }
        } else {
            ESP_LOGI(TAG, "Starting DHCP client");
            err = esp_netif_dhcpc_start(eth_netif);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to start DHCP client: %s", esp_err_to_name(err));
                return err;
            }
        }

        // Register event handlers
        if (eth_event_handler != NULL) {
            err = esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register ETH event handler: %s", esp_err_to_name(err));
                return err;
            }

            err = esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, eth_event_handler, NULL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to register IP event handler: %s", esp_err_to_name(err));
                return err;
            }
        }

        // Start Ethernet
        err = esp_eth_start(s_eth_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start Ethernet: %s", esp_err_to_name(err));
            return err;
        }

        ESP_LOGI(TAG, "OpenEth Ethernet initialized successfully for QEMU");
        return ESP_OK;
    }

#else

    esp_err_t ethernet_init_qemu(esp_event_handler_t eth_event_handler, esp_netif_ip_info_t* static_ip, char * netif_hostname)
    {
        ESP_LOGE(TAG, "QEMU Ethernet not supported - OpenEth not enabled in configuration");
        return ESP_ERR_NOT_SUPPORTED;
    }

#endif // CONFIG_ETH_USE_OPENETH
