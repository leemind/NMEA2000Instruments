#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "lwip/err.h"
#include "lwip/sys.h"

#include "wifi.h"
#include "settings.h"

static const char *TAG = "WIFI_STATION";

/* The event group allows multiple bits for each event, but we only care about:
 * - WIFI_CONNECTED_BIT: connected to the AP with an IP
 * - WIFI_FAIL_BIT: failed to connect after max retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;
static wifi_status_t s_status = WIFI_STATUS_DISCONNECTED;
static char s_status_str[64] = "Disconnected";

static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
        s_status = WIFI_STATUS_CONNECTING;
        strcpy(s_status_str, "Connecting...");
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < 5) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP (%d/5)", s_retry_num);
            s_status = WIFI_STATUS_CONNECTING;
            snprintf(s_status_str, sizeof(s_status_str), "Retrying (%d/5)...", s_retry_num);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            s_status = WIFI_STATUS_ERROR;
            strcpy(s_status_str, "Connection Failed");
        }
        ESP_LOGI(TAG,"Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        s_status = WIFI_STATUS_CONNECTED;
        snprintf(s_status_str, sizeof(s_status_str), "Connected\n" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    
    // Load credentials from settings
    app_settings_t settings = settings_get();
    if (settings.wifi_ssid[0] != '\0') {
        wifi_config_t wifi_config = {
            .sta = {
                .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            },
        };
        strncpy((char*)wifi_config.sta.ssid, settings.wifi_ssid, sizeof(wifi_config.sta.ssid));
        strncpy((char*)wifi_config.sta.password, settings.wifi_pass, sizeof(wifi_config.sta.password));
        
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
        ESP_LOGI(TAG, "WiFi started with SSID: %s", settings.wifi_ssid);
    } else {
        ESP_LOGI(TAG, "WiFi credentials not found. Waiting for user input.");
        strcpy(s_status_str, "Ready to Connect");
    }
}

void wifi_connect(const char *ssid, const char *pass)
{
    if (!ssid || ssid[0] == '\0') {
        ESP_LOGW(TAG, "Cannot connect: SSID is empty");
        return;
    }

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strncpy((char*)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid));
    strncpy((char*)wifi_config.sta.password, pass ? pass : "", sizeof(wifi_config.sta.password));

    ESP_LOGI(TAG, "Connecting to SSID: %s", ssid);
    s_retry_num = 0;
    s_status = WIFI_STATUS_CONNECTING;
    strcpy(s_status_str, "Connecting...");
    
    // Stop if already running
    esp_wifi_stop();
    
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

wifi_status_t wifi_get_status(void) { return s_status; }
const char* wifi_get_status_str(void) { return s_status_str; }
