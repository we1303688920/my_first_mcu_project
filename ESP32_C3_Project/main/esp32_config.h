#ifndef __ESP32_CONFIG_H
#define __ESP32_CONFIG_H

/* WiFi配置 */
#define WIFI_SSID           "Your_WiFi_SSID"
#define WIFI_PASSWORD       "Your_WiFi_Password"

/* 天气API配置 */
#define WEATHER_API_KEY     "your_openweathermap_api_key_here"
#define DEFAULT_CITY        "Beijing,CN"
#define WEATHER_UPDATE_INTERVAL 300  /* 5分钟更新一次 */

/* 串口配置 */
#define UART_PORT_NUM       UART_NUM_0
#define UART_BAUD_RATE      115200
#define UART_BUFFER_SIZE    256
#define UART_TX_PIN         1
#define UART_RX_PIN         3

/* 协议配置 */
#define PROTOCOL_START_BYTE 0xAA
#define PROTOCOL_END_BYTE   0x55
#define PROTOCOL_HEADER_SIZE 4
#define PROTOCOL_MAX_DATA_SIZE 128

/* 命令定义 */
#define CMD_GET_WEATHER     0x01
#define CMD_GET_TIME        0x02
#define CMD_SET_TIME        0x03
#define CMD_SET_CITY        0x04
#define CMD_GET_FORECAST    0x05
#define CMD_SYSTEM_STATUS   0x06
#define CMD_ACK             0x07
#define CMD_NACK            0x08
#define CMD_ERROR           0xFF

/* 事件组位定义 */
#define WIFI_CONNECTED_BIT  BIT0
#define TIME_SYNC_BIT       BIT1
#define WEATHER_UPDATED_BIT BIT2
#define FORCE_UPDATE_BIT    BIT3

/* 天气预报最大天数 */
#define MAX_FORECAST_DAYS   7

#endif /* __ESP32_CONFIG_H */