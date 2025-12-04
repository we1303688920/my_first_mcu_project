#include <stdio.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"
#include "cJSON.h"
#include "wifi_manager.h"
#include "weather_api.h"
#include "uart_protocol.h"
#include "esp32_config.h"

/* 标签用于日志记录 */
static const char *TAG = "MAIN";

/* 全局变量 */
static TaskHandle_t weather_task_handle = NULL;
static TaskHandle_t uart_task_handle = NULL;
static EventGroupHandle_t weather_event_group;

/* WiFi状态 */
static volatile bool wifi_connected = false;
static volatile bool time_synced = false;
static char current_city[32] = DEFAULT_CITY;

/* 函数原型 */
static void weather_task(void *pvParameters);
static void uart_receive_task(void *pvParameters);
static void time_sync_task(void *pvParameters);
static void process_stm32_command(uint8_t *data, uint16_t length);
static void send_weather_data_to_stm32(void);
static void send_system_status_to_stm32(void);

/* 系统初始化 */
static void system_init(void) {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    /* 创建事件组 */
    weather_event_group = xEventGroupCreate();

    /* 初始化UART */
    uart_init();

    /* 初始化WiFi */
    wifi_init();

    /* 设置系统时间 */
    setenv("TZ", "CST-8", 1);
    tzset();
}

/* 主函数 */
void app_main(void) {
    ESP_LOGI(TAG, "Weather Clock ESP32-C3 Started");
    ESP_LOGI(TAG, "Free heap: %d bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "SDK version: %s", esp_get_idf_version());

    /* 系统初始化 */
    system_init();

    /* 创建任务 */
    xTaskCreate(weather_task, "weather_task", 4096, NULL, 5, &weather_task_handle);
    xTaskCreate(uart_receive_task, "uart_task", 4096, NULL, 4, &uart_task_handle);
    xTaskCreate(time_sync_task, "time_task", 2048, NULL, 3, NULL);

    /* 设置WiFi连接回调 */
    set_wifi_connected_callback([](bool connected) {
        wifi_connected = connected;
        if (connected) {
            ESP_LOGI(TAG, "WiFi connected, requesting time sync");
            xEventGroupSetBits(weather_event_group, TIME_SYNC_BIT);
        }
    });

    ESP_LOGI(TAG, "System initialization completed");
}

/* 天气任务 - 定期获取天气数据 */
static void weather_task(void *pvParameters) {
    WeatherData weather_data;
    time_t last_update = 0;
    time_t now;
    struct tm timeinfo;

    ESP_LOGI(TAG, "Weather task started");

    while (1) {
        now = time(NULL);
        localtime_r(&now, &timeinfo);

        /* 检查是否需要更新天气数据 */
        if (wifi_connected && (now - last_update > WEATHER_UPDATE_INTERVAL)) {
            ESP_LOGI(TAG, "Updating weather data for city: %s", current_city);

            /* 获取天气数据 */
            if (get_current_weather(current_city, &weather_data)) {
                ESP_LOGI(TAG, "Weather data obtained: %.1f°C, %s",
                        weather_data.temperature, weather_data.description);

                /* 发送到STM32 */
                send_weather_data_to_stm32();

                /* 保存更新时间 */
                last_update = now;

                /* 设置事件标志 */
                xEventGroupSetBits(weather_event_group, WEATHER_UPDATED_BIT);
            } else {
                ESP_LOGE(TAG, "Failed to get weather data");
            }
        }

        /* 每30秒检查一次 */
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* UART接收任务 */
static void uart_receive_task(void *pvParameters) {
    uint8_t data[UART_BUFFER_SIZE];
    uint16_t data_length = 0;
    uint8_t rx_byte;
    bool frame_started = false;
    uint8_t expected_length = 0;
    uint16_t bytes_received = 0;

    ESP_LOGI(TAG, "UART receive task started");

    while (1) {
        /* 读取UART数据 */
        int len = uart_read_bytes(UART_NUM_0, &rx_byte, 1, pdMS_TO_TICKS(100));

        if (len > 0) {
            /* 帧起始检测 */
            if (!frame_started && rx_byte == PROTOCOL_START_BYTE) {
                frame_started = true;
                bytes_received = 0;
                data[bytes_received++] = rx_byte;
            }
            /* 帧接收中 */
            else if (frame_started) {
                data[bytes_received++] = rx_byte;

                /* 解析帧头 */
                if (bytes_received >= PROTOCOL_HEADER_SIZE) {
                    if (expected_length == 0) {
                        expected_length = data[2];  // 数据长度
                    }

                    /* 检查是否收到完整帧 */
                    if (bytes_received >= (PROTOCOL_HEADER_SIZE + expected_length)) {
                        /* 处理命令 */
                        process_stm32_command(data, bytes_received);

                        /* 重置状态 */
                        frame_started = false;
                        expected_length = 0;
                        bytes_received = 0;
                    }
                }
            }
        }

        /* 短暂延时让出CPU */
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* 处理STM32命令 */
static void process_stm32_command(uint8_t *data, uint16_t length) {
    uint8_t command = data[1];
    uint8_t checksum = data[3];
    uint8_t calculated_checksum = 0;

    /* 计算校验和 */
    for (int i = PROTOCOL_HEADER_SIZE; i < length; i++) {
        calculated_checksum ^= data[i];
    }

    /* 验证校验和 */
    if (calculated_checksum != checksum) {
        ESP_LOGW(TAG, "Checksum error: received 0x%02X, calculated 0x%02X",
                checksum, calculated_checksum);
        return;
    }

    /* 处理命令 */
    switch (command) {
        case CMD_GET_WEATHER: {
            char city_id[32] = {0};
            if (length > PROTOCOL_HEADER_SIZE) {
                strncpy(city_id, (char*)&data[PROTOCOL_HEADER_SIZE],
                        length - PROTOCOL_HEADER_SIZE);
                strncpy(current_city, city_id, sizeof(current_city) - 1);
                ESP_LOGI(TAG, "City changed to: %s", current_city);
            }
            send_weather_data_to_stm32();
            break;
        }

        case CMD_GET_TIME:
            send_system_time_to_stm32();
            break;

        case CMD_SET_CITY: {
            if (length > PROTOCOL_HEADER_SIZE) {
                char new_city[32] = {0};
                strncpy(new_city, (char*)&data[PROTOCOL_HEADER_SIZE],
                        length - PROTOCOL_HEADER_SIZE);
                strncpy(current_city, new_city, sizeof(current_city) - 1);
                ESP_LOGI(TAG, "City set to: %s", current_city);

                /* 立即更新天气数据 */
                xEventGroupSetBits(weather_event_group, FORCE_UPDATE_BIT);
            }
            break;
        }

        case CMD_SYSTEM_STATUS:
            send_system_status_to_stm32();
            break;

        case CMD_GET_FORECAST:
            /* TODO: 实现天气预报功能 */
            ESP_LOGI(TAG, "Forecast requested");
            break;

        default:
            ESP_LOGW(TAG, "Unknown command: 0x%02X", command);
            break;
    }
}

/* 发送天气数据到STM32 */
static void send_weather_data_to_stm32(void) {
    WeatherData weather;

    /* 获取最新天气数据 */
    if (get_current_weather(current_city, &weather)) {
        /* 发送数据到UART */
        uart_send_weather_data(&weather);
        ESP_LOGI(TAG, "Weather data sent to STM32");
    } else {
        ESP_LOGE(TAG, "Failed to get weather data for sending");
    }
}

/* 发送系统状态到STM32 */
static void send_system_status_to_stm32(void) {
    uint8_t status_data[8] = {0};

    status_data[0] = wifi_connected ? 1 : 0;
    status_data[1] = 1;  /* weather_data_valid */
    status_data[2] = time_synced ? 1 : 0;
    status_data[3] = 100;  /* battery_level (模拟) */

    /* 发送系统状态 */
    uart_send_system_status(status_data, sizeof(status_data));
    ESP_LOGI(TAG, "System status sent to STM32");
}

/* 时间同步任务 */
static void time_sync_task(void *pvParameters) {
    EventBits_t bits;

    ESP_LOGI(TAG, "Time sync task started");

    while (1) {
        /* 等待时间同步事件 */
        bits = xEventGroupWaitBits(weather_event_group,
                                   TIME_SYNC_BIT | FORCE_UPDATE_BIT,
                                   pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & TIME_SYNC_BIT) {
            ESP_LOGI(TAG, "Synchronizing time via NTP");

            /* 配置NTP服务器 */
            sntp_setoperatingmode(SNTP_OPMODE_POLL);
            sntp_setservername(0, "pool.ntp.org");
            sntp_init();

            /* 等待时间同步完成 */
            int retry = 0;
            while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && retry++ < 10) {
                ESP_LOGI(TAG, "Waiting for system time to be set... (%d/10)", retry);
                vTaskDelay(2000 / portTICK_PERIOD_MS);
            }

            if (sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
                time_synced = true;
                ESP_LOGI(TAG, "Time synchronized successfully");

                /* 发送时间到STM32 */
                send_system_time_to_stm32();
            } else {
                ESP_LOGE(TAG, "Failed to synchronize time");
            }
        }

        if (bits & FORCE_UPDATE_BIT) {
            /* 强制更新天气数据 */
            send_weather_data_to_stm32();
        }
    }
}