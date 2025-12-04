#include "uart_protocol.h"
#include "esp_system.h"
#include "driver/uart.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"

#define UART_PORT_NUM      UART_NUM_0
#define UART_BAUD_RATE     115200
#define UART_BUFFER_SIZE   256

static QueueHandle_t uart_queue;

void uart_init(void) {
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    uart_param_config(UART_PORT_NUM, &uart_config);
    uart_set_pin(UART_PORT_NUM, 1, 3, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    uart_driver_install(UART_PORT_NUM, UART_BUFFER_SIZE * 2,
                       UART_BUFFER_SIZE * 2, 20, &uart_queue, 0);
}

void uart_send_weather_data(WeatherData *weather) {
    uint8_t buffer[128];
    uint16_t index = 0;

    // 构建帧头
    buffer[index++] = 0xAA;  // 起始字节
    buffer[index++] = 0x01;  // 命令: 天气数据
    buffer[index++] = 0;     // 数据长度 (稍后填充)

    // 序列化天气数据
    memcpy(&buffer[index], weather, sizeof(WeatherData));
    index += sizeof(WeatherData);

    // 更新数据长度
    buffer[2] = sizeof(WeatherData);

    // 计算校验和
    uint8_t checksum = 0;
    for (uint16_t i = 4; i < index; i++) {
        checksum ^= buffer[i];
    }
    buffer[3] = checksum;

    // 发送数据
    uart_write_bytes(UART_PORT_NUM, buffer, index);
}