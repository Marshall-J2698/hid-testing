#include "eth_req.h"

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_eth.h"
#include "esp_netif.h"
#include "nvs_flash.h"
#include "esp_http_client.h"
#include "driver/gpio.h"


static const char *TAG = "eth_access_control";
static EventGroupHandle_t s_eth_event_group;
#define ETH_CONNECTED_BIT BIT0

// Waveshare ESP32-P4 IP101 pin definitions
#define ETH_MDC_GPIO 31
#define ETH_MDIO_GPIO 52
#define ETH_RESET_GPIO 51
#define ETH_PHY_ADDR 1

static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data)
{
    switch (event_id)
    {
    case ETHERNET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Up"); // LOGI is just the logging system;
                                           // logs to a given tag in output
        break;
    case ETHERNET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "Ethernet Link Down");
        break;
    case ETHERNET_EVENT_START:
        ESP_LOGI(TAG, "Ethernet Started");
        break;
    case ETHERNET_EVENT_STOP:
        ESP_LOGI(TAG, "Ethernet Stopped");
        break;
    default:
        break;
    }
}

static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data)
{
    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    xEventGroupSetBits(s_eth_event_group, ETH_CONNECTED_BIT);
}

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id)
    {
    case HTTP_EVENT_ON_DATA:
        if (evt->user_data && evt->data_len > 0){
            memcpy(evt->user_data,evt->data,1);
        }
        ESP_LOGI(TAG, "Response: %.*s", evt->data_len, (char *)evt->data);
        break;
    default:
        break;
    }
    return ESP_OK;
}


/**
 * returns 1 if server says input is from admin!
 */
int http_get_task(const scan_buffer_received input)
{
    char res[2] = {0};
    esp_http_client_config_t config = {
        .url = "http://137.22.5.222:3000/check",
        .event_handler = http_event_handler,
        .port = 3000,
        .user_data = res,
    };
    esp_http_client_handle_t client = esp_http_client_init(&config);
    char post_data[128];
    snprintf(post_data,128,"{\"id\": \"%s\", \"machine\": \"LAT01\"}",input.id_message);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK)
    {
        esp_http_client_read(client,res,1);
        printf("res: %s\n",res);
        ESP_LOGI(TAG, "HTTP GET Status = %d, content_length = %lld",
                 esp_http_client_get_status_code(client),
                 esp_http_client_get_content_length(client));
    }
    else
    {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    if (res[0] == '1'){
        return 1;
    }

    return 0;
    // TODO: decide if this should be it's own task or just wrapped by 
    // receive_ID_task
    // vTaskDelete(NULL);
}

void init_eth(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_eth_event_group = xEventGroupCreate();

    // Create default Ethernet netif
    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    // MAC config — use ESP32-P4 defaults
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
    esp32_emac_config.smi_gpio.mdc_num = ETH_MDC_GPIO;
    esp32_emac_config.smi_gpio.mdio_num = ETH_MDIO_GPIO;
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio =
        50; // GPIO50 on Waveshare P4

    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

    // PHY config — IP101
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    phy_config.phy_addr = ETH_PHY_ADDR;
    phy_config.reset_gpio_num = ETH_RESET_GPIO;

    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config);

    // Install Ethernet driver
    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    uint8_t custom_mac[] = {0xb8, 0x27, 0xeb, 0x23, 0x99, 0xf3};
    ESP_ERROR_CHECK(esp_eth_ioctl(eth_handle, ETH_CMD_S_MAC_ADDR, custom_mac));

    // Attach Ethernet driver to TCP/IP stack
    ESP_ERROR_CHECK(
        esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    // Set static IP
    ESP_ERROR_CHECK(esp_netif_dhcpc_stop(eth_netif));
    esp_netif_ip_info_t ip_info = {
        .ip.addr = ESP_IP4TOADDR(137, 22, 7, 142),
        .netmask.addr = ESP_IP4TOADDR(255, 255, 255, 255),
        .gw.addr = ESP_IP4TOADDR(137, 22, 1, 254),
    };
    ESP_ERROR_CHECK(esp_netif_set_ip_info(eth_netif, &ip_info));

    esp_netif_dns_info_t dns_info = {
        .ip.u_addr.ip4.addr = ESP_IP4TOADDR(137, 22, 1, 7),
    };
    ESP_ERROR_CHECK(
        esp_netif_set_dns_info(eth_netif, ESP_NETIF_DNS_MAIN, &dns_info));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                               &eth_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                               &got_ip_event_handler, NULL));

    // Start Ethernet
    ESP_ERROR_CHECK(esp_eth_start(eth_handle));

    // Wait for IP
    ESP_LOGI(TAG, "Waiting for Ethernet connection...");
    xEventGroupWaitBits(s_eth_event_group, ETH_CONNECTED_BIT, pdFALSE, pdFALSE,
                        pdMS_TO_TICKS(30000));

    // confirm that MAC is set properly
    uint8_t read_mac[6];
    esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, read_mac);
    ESP_LOGI(TAG, "MAC: %02x:%02x:%02x:%02x:%02x:%02x", read_mac[0],
             read_mac[1], read_mac[2], read_mac[3], read_mac[4], read_mac[5]);
}

// void app_main(void)
// {

//     // Fire off the HTTP request
//     xTaskCreate(&http_get_task, "http_get_task", 8192, NULL, 5, NULL);
// }