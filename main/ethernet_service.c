#include "ethernet_service.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_check.h"
#include "esp_eth.h"
#include "esp_eth_mac_esp.h"
#include "esp_eth_netif_glue.h"
#include "esp_eth_phy_ip101.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/event_groups.h"
#include "lwip/ip4_addr.h"

#define ETH_STARTED_BIT BIT0
#define ETH_LINK_UP_BIT BIT1
#define ETH_GOT_IP_BIT BIT2

#define ETH_PHY_ADDR 1
#define ETH_PHY_RESET_GPIO 51
#define ETH_MDC_GPIO 31
#define ETH_MDIO_GPIO 52
#define ETH_RMII_REF_CLK_GPIO 50
#define ETH_RMII_TX_EN_GPIO 49
#define ETH_RMII_TXD0_GPIO 34
#define ETH_RMII_TXD1_GPIO 35
#define ETH_RMII_CRS_DV_GPIO 28
#define ETH_RMII_RXD0_GPIO 29
#define ETH_RMII_RXD1_GPIO 30

static const char *TAG = "ethernet_service";

typedef struct {
    esp_eth_handle_t handle;
    esp_netif_t *netif;
    esp_eth_netif_glue_handle_t glue;
    EventGroupHandle_t event_group;
    bool initialized;
} ethernet_service_state_t;

static ethernet_service_state_t s_state;

static void eth_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    uint8_t mac_addr[6] = {0};
    esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

    switch (event_id) {
    case ETHERNET_EVENT_CONNECTED:
        esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
        xEventGroupSetBits(s_state.event_group, ETH_LINK_UP_BIT);
        ESP_LOGI(TAG, "Ethernet link up");
        ESP_LOGI(TAG, "Ethernet MAC %02x:%02x:%02x:%02x:%02x:%02x",
                 mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        xEventGroupClearBits(s_state.event_group, ETH_LINK_UP_BIT | ETH_GOT_IP_BIT);
        ESP_LOGW(TAG, "Ethernet link down");
        break;
    case ETHERNET_EVENT_START:
        xEventGroupSetBits(s_state.event_group, ETH_STARTED_BIT);
        ESP_LOGI(TAG, "Ethernet started");
        break;
    case ETHERNET_EVENT_STOP:
        xEventGroupClearBits(s_state.event_group, ETH_STARTED_BIT | ETH_LINK_UP_BIT | ETH_GOT_IP_BIT);
        ESP_LOGI(TAG, "Ethernet stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    const esp_netif_ip_info_t *ip_info = &event->ip_info;

    xEventGroupSetBits(s_state.event_group, ETH_GOT_IP_BIT);

    ESP_LOGI(TAG, "Ethernet got IPv4 address");
    ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
    ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
    ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
}

static esp_err_t ethernet_driver_create(esp_eth_handle_t *out_handle)
{
    esp_eth_mac_t *mac = NULL;
    esp_eth_phy_t *phy = NULL;

    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    eth_esp32_emac_config_t emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();

    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_PHY_RESET_GPIO;

    emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    emac_config.interface = EMAC_DATA_INTERFACE_RMII;
    emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    emac_config.clock_config.rmii.clock_gpio = ETH_RMII_REF_CLK_GPIO;
    emac_config.emac_dataif_gpio.rmii.tx_en_num = ETH_RMII_TX_EN_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd0_num = ETH_RMII_TXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.txd1_num = ETH_RMII_TXD1_GPIO;
    emac_config.emac_dataif_gpio.rmii.crs_dv_num = ETH_RMII_CRS_DV_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd0_num = ETH_RMII_RXD0_GPIO;
    emac_config.emac_dataif_gpio.rmii.rxd1_num = ETH_RMII_RXD1_GPIO;

    mac = esp_eth_mac_new_esp32(&emac_config, &mac_config);
    ESP_RETURN_ON_FALSE(mac != NULL, ESP_FAIL, TAG, "failed to create EMAC");

    phy = esp_eth_phy_new_ip101(&phy_config);
    if (phy == NULL) {
        mac->del(mac);
        ESP_LOGE(TAG, "failed to create IP101 PHY");
        return ESP_FAIL;
    }

    esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_err_t err = esp_eth_driver_install(&config, out_handle);
    if (err != ESP_OK) {
        mac->del(mac);
        phy->del(phy);
        ESP_LOGE(TAG, "failed to install Ethernet driver: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t ethernet_service_init(void)
{
    if (s_state.initialized) {
        return ESP_OK;
    }

    memset(&s_state, 0, sizeof(s_state));

    s_state.event_group = xEventGroupCreate();
    ESP_RETURN_ON_FALSE(s_state.event_group != NULL, ESP_ERR_NO_MEM, TAG, "failed to create event group");

    ESP_RETURN_ON_ERROR(ethernet_driver_create(&s_state.handle), TAG, "failed to initialize Ethernet driver");

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    s_state.netif = esp_netif_new(&netif_cfg);
    ESP_RETURN_ON_FALSE(s_state.netif != NULL, ESP_ERR_NO_MEM, TAG, "failed to create Ethernet netif");

    s_state.glue = esp_eth_new_netif_glue(s_state.handle);
    ESP_RETURN_ON_FALSE(s_state.glue != NULL, ESP_ERR_NO_MEM, TAG, "failed to create Ethernet netif glue");

    ESP_RETURN_ON_ERROR(esp_netif_attach(s_state.netif, s_state.glue), TAG, "failed to attach Ethernet netif");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, eth_event_handler, NULL),
                        TAG, "failed to register Ethernet event handler");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, got_ip_event_handler, NULL),
                        TAG, "failed to register IP event handler");

    s_state.initialized = true;
    return ESP_OK;
}

esp_err_t ethernet_service_start(void)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "Ethernet service not initialized");
    return esp_eth_start(s_state.handle);
}

esp_err_t ethernet_service_wait_for_ip(TickType_t timeout_ticks)
{
    ESP_RETURN_ON_FALSE(s_state.initialized, ESP_ERR_INVALID_STATE, TAG, "Ethernet service not initialized");

    EventBits_t bits = xEventGroupWaitBits(
        s_state.event_group,
        ETH_GOT_IP_BIT,
        pdFALSE,
        pdFALSE,
        timeout_ticks);

    return (bits & ETH_GOT_IP_BIT) ? ESP_OK : ESP_ERR_TIMEOUT;
}

bool ethernet_service_is_up(void)
{
    return ethernet_service_is_link_up();
}

bool ethernet_service_is_link_up(void)
{
    if (s_state.event_group == NULL) {
        return false;
    }

    EventBits_t bits = xEventGroupGetBits(s_state.event_group);
    return (bits & ETH_LINK_UP_BIT) != 0;
}

esp_err_t ethernet_service_get_ipv4_string(char *buffer, size_t buffer_len)
{
    esp_netif_ip_info_t ip_info = {0};

    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid IPv4 buffer");

    buffer[0] = '\0';

    if (s_state.netif == NULL) {
        snprintf(buffer, buffer_len, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_state.netif, &ip_info), TAG, "failed to get IP info");
    snprintf(buffer, buffer_len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

esp_err_t ethernet_service_get_gateway_string(char *buffer, size_t buffer_len)
{
    esp_netif_ip_info_t ip_info = {0};

    ESP_RETURN_ON_FALSE(buffer != NULL && buffer_len > 0, ESP_ERR_INVALID_ARG, TAG, "invalid gateway buffer");

    buffer[0] = '\0';

    if (s_state.netif == NULL) {
        snprintf(buffer, buffer_len, "0.0.0.0");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_RETURN_ON_ERROR(esp_netif_get_ip_info(s_state.netif, &ip_info), TAG, "failed to get IP info");

    if (ip4_addr_isany_val(ip_info.gw)) {
        snprintf(buffer, buffer_len, "0.0.0.0");
        return ESP_ERR_NOT_FOUND;
    }

    snprintf(buffer, buffer_len, IPSTR, IP2STR(&ip_info.gw));
    return ESP_OK;
}
