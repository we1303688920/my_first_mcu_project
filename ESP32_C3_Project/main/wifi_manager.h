#ifndef __WIFI_MANAGER_H
#define __WIFI_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* WiFi状态枚举 */
typedef enum {
    WIFI_STATUS_DISCONNECTED = 0,
    WIFI_STATUS_CONNECTING,
    WIFI_STATUS_CONNECTED,
    WIFI_STATUS_FAILED
} wifi_status_t;

/* 连接信息结构体 */
typedef struct {
    char ssid[32];
    int8_t rssi;
    uint8_t channel;
    uint32_t ip_address;
} wifi_connection_info_t;

/* WiFi连接回调函数类型 */
typedef void (*wifi_connected_callback_t)(bool connected);

/* 函数声明 */
void wifi_init(void);
bool wifi_wait_for_connection(int timeout_ms);
wifi_status_t wifi_get_status(void);
int8_t wifi_get_rssi(void);
bool wifi_get_connection_info(wifi_connection_info_t *info);
void wifi_disconnect(void);
void wifi_reconnect(void);
void set_wifi_connected_callback(wifi_connected_callback_t callback);
void wifi_scan_networks(void);

#ifdef __cplusplus
}
#endif

#endif /* __WIFI_MANAGER_H */