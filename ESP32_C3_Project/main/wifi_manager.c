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
#include "wifi_manager.h"

/* WiFi配置 */
#define WIFI_SSID           CONFIG_WIFI_SSID
#define WIFI_PASSWORD       CONFIG_WIFI_PASSWORD
#define WIFI_MAX_RETRY      CONFIG_WIFI_MAXIMUM_RETRY

/* 事件组位定义 */
#define WIFI_CONNECTED_BIT  BIT0
#define WIFI_FAIL_BIT       BIT1

/* 标签 */
static const char *TAG = "WiFi";

/* 事件组 */
static EventGroupHandle_t s_wifi_event_group;
static int s_retry_num = 0;

/* WiFi状态回调 */
static wifi_connected_callback_t s_wifi_callback = NULL;

/* WiFi事件处理 */
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;

            case WIFI_EVENT_STA_DISCONNECTED:
                if (s_retry_num < WIFI_MAX_RETRY) {
                    esp_wifi_connect();
                    s_retry_num++;
                    ESP_LOGI(TAG, "Retry to connect to AP (%d/%d)",
                            s_retry_num, WIFI_MAX_RETRY);
                } else {
                    xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
                    ESP_LOGE(TAG, "Failed to connect after %d retries", WIFI_MAX_RETRY);

                    /* 通知回调 */
                    if (s_wifi_callback) {
                        s_wifi_callback(false);
                    }
                }
                break;

            case WIFI_EVENT_STA_CONNECTED:
                ESP_LOGI(TAG, "Connected to AP");
                break;

            default:
                break;
        }
    } else if (event_base == IP_EVENT) {
        switch (event_id) {
            case IP_EVENT_STA_GOT_IP: {
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
                s_retry_num = 0;
                xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);

                /* 通知回调 */
                if (s_wifi_callback) {
                    s_wifi_callback(true);
                }
                break;
            }

            case IP_EVENT_STA_LOST_IP:
                ESP_LOGI(TAG, "Lost IP address");
                break;

            default:
                break;
        }
    }
}

/* WiFi初始化 */
void wifi_init(void) {
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    /* 注册事件处理器 */
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                               &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                               &wifi_event_handler, NULL));

    /* WiFi配置 */
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished");
    ESP_LOGI(TAG, "Connecting to %s...", WIFI_SSID);
}

/* 等待WiFi连接 */
bool wifi_wait_for_connection(int timeout_ms) {
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(timeout_ms));

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        return true;
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
        return false;
    } else {
        ESP_LOGE(TAG, "WiFi connection timeout");
        return false;
    }
}

/* 获取WiFi状态 */
wifi_status_t wifi_get_status(void) {
    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return WIFI_STATUS_CONNECTED;
    } else {
        return WIFI_STATUS_DISCONNECTED;
    }
}

/* 获取RSSI */
int8_t wifi_get_rssi(void) {
    wifi_ap_record_t ap_info;

    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        return ap_info.rssi;
    } else {
        return 0;
    }
}

/* 获取连接信息 */
bool wifi_get_connection_info(wifi_connection_info_t *info) {
    wifi_ap_record_t ap_info;
    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");

    if (!netif) {
        return false;
    }

    if (esp_wifi_sta_get_ap_info(&ap_info) != ESP_OK) {
        return false;
    }

    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK) {
        return false;
    }

    memcpy(info->ssid, ap_info.ssid, sizeof(ap_info.ssid));
    info->rssi = ap_info.rssi;
    info->channel = ap_info.primary;
    info->ip_address = ip_info.ip.addr;

    return true;
}

/* 断开WiFi连接 */
void wifi_disconnect(void) {
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_stop());

    /* 通知回调 */
    if (s_wifi_callback) {
        s_wifi_callback(false);
    }
}

/* 重新连接WiFi */
void wifi_reconnect(void) {
    ESP_LOGI(TAG, "Reconnecting to WiFi...");
    ESP_ERROR_CHECK(esp_wifi_disconnect());
    ESP_ERROR_CHECK(esp_wifi_connect());
}

/* 设置WiFi回调 */
void set_wifi_connected_callback(wifi_connected_callback_t callback) {
    s_wifi_callback = callback;
}

/* 扫描WiFi网络 */
void wifi_scan_networks(void) {
    uint16_t ap_count = 0;
    wifi_ap_record_t *ap_list = NULL;

    /* 开始扫描 */
    ESP_LOGI(TAG, "Starting WiFi scan...");
    esp_wifi_scan_start(NULL, true);

    /* 获取扫描结果数量 */
    esp_wifi_scan_get_ap_num(&ap_count);

    if (ap_count == 0) {
        ESP_LOGI(TAG, "No APs found");
        return;
    }

    /* 分配内存并获取结果 */
    ap_list = malloc(sizeof(wifi_ap_record_t) * ap_count);
    if (ap_list == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for AP list");
        return;
    }

    esp_wifi_scan_get_ap_records(&ap_count, ap_list);

    /* 打印扫描结果 */
    ESP_LOGI(TAG, "Found %d APs:", ap_count);
    for (int i = 0; i < ap_count; i++) {
        ESP_LOGI(TAG, "  %d: %s (RSSI: %d, Channel: %d)",
                i + 1, ap_list[i].ssid, ap_list[i].rssi, ap_list[i].primary);
    }

    free(ap_list);
}