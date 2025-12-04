#include "uart_comm_task.h"
#include "project_defines.h"
#include "weather_parser.h"
#include "lcd_driver.h"
#include "cmsis_os.h"
#include <string.h>

/* 全局变量 */
static uint8_t uart_rx_buffer[UART_RX_BUFFER_SIZE];
static uint16_t uart_rx_index = 0;
static uint8_t uart_tx_buffer[UART_RX_BUFFER_SIZE];
static bool frame_decoding = false;
static uint8_t expected_length = 0;
static UARTFrame current_frame;

/* 私有函数原型 */
static void ProcessReceivedFrame(UARTFrame *frame);
static void SendUARTFrame(UARTFrame *frame);
static uint8_t CalculateChecksum(uint8_t *data, uint16_t length);
static bool ValidateFrame(UARTFrame *frame);

/* UART接收任务 */
void UartRxTask(void *argument) {
    EventType event = EVENT_UART_DATA_RECEIVED;
    uint8_t rx_byte;
    
    DEBUG_PRINT("UART RX Task Started");
    
    while (1) {
        /* 等待UART接收完成中断或轮询接收 */
        if (HAL_UART_Receive(&ESP32_UART, &rx_byte, 1, 10) == HAL_OK) {
            /* 帧开始检测 */
            if (!frame_decoding && rx_byte == PROTOCOL_START_BYTE) {
                frame_decoding = true;
                uart_rx_index = 0;
                current_frame.start_byte = rx_byte;
                uart_rx_buffer[uart_rx_index++] = rx_byte;
            }
            /* 帧解码中 */
            else if (frame_decoding) {
                uart_rx_buffer[uart_rx_index++] = rx_byte;
                
                /* 已收到足够字节可以解析头部 */
                if (uart_rx_index >= PROTOCOL_HEADER_SIZE) {
                    if (expected_length == 0) {
                        expected_length = uart_rx_buffer[2];  // 数据长度
                    }
                    
                    /* 检查是否收到完整帧 */
                    if (uart_rx_index >= (PROTOCOL_HEADER_SIZE + expected_length)) {
                        /* 提取帧数据 */
                        memcpy(&current_frame.command, &uart_rx_buffer[1], 1);
                        memcpy(&current_frame.data_length, &uart_rx_buffer[2], 1);
                        memcpy(&current_frame.checksum, &uart_rx_buffer[3], 1);
                        
                        if (expected_length > 0) {
                            memcpy(current_frame.data, 
                                   &uart_rx_buffer[PROTOCOL_HEADER_SIZE], 
                                   expected_length);
                        }
                        
                        /* 验证帧 */
                        if (ValidateFrame(&current_frame)) {
                            ProcessReceivedFrame(&current_frame);
                            osMessageQueuePut(eventQueueHandle, &event, 0, 0);
                        }
                        
                        /* 重置解码状态 */
                        frame_decoding = false;
                        expected_length = 0;
                        uart_rx_index = 0;
                        memset(&current_frame, 0, sizeof(UARTFrame));
                    }
                }
            }
        }
        
        osDelay(1);  // 让出CPU时间
    }
}

/* 处理接收到的帧 */
static void ProcessReceivedFrame(UARTFrame *frame) {
    switch (frame->command) {
        case CMD_GET_WEATHER: {
            WeatherData weather_data;
            if (ParseWeatherData(frame->data, frame->data_length, &weather_data)) {
                /* 发送到显示任务 */
                osMessageQueuePut(weatherQueueHandle, &weather_data, 0, 0);
                system_status.weather_data_valid = true;
                
                /* 发送ACK确认 */
                UARTFrame ack_frame = {
                    .start_byte = PROTOCOL_START_BYTE,
                    .command = CMD_ACK,
                    .data_length = 0,
                    .checksum = CalculateChecksum(NULL, 0)
                };
                SendUARTFrame(&ack_frame);
            }
            break;
        }
            
        case CMD_GET_TIME: {
            /* 从RTC获取当前时间并发送给ESP32 */
            TimeData current_time;
            // TODO: 从RTC读取时间
            // 将时间数据打包到帧中发送
            break;
        }
            
        case CMD_SYSTEM_STATUS: {
            /* 发送系统状态给ESP32 */
            UARTFrame status_frame;
            uint8_t status_data[8];
            
            status_data[0] = system_status.wifi_connected;
            status_data[1] = system_status.weather_data_valid;
            status_data[2] = system_status.time_synced;
            status_data[3] = system_status.battery_level;
            memcpy(&status_data[4], &system_status.uptime_seconds, 4);
            
            status_frame.start_byte = PROTOCOL_START_BYTE;
            status_frame.command = CMD_SYSTEM_STATUS;
            status_frame.data_length = 8;
            memcpy(status_frame.data, status_data, 8);
            status_frame.checksum = CalculateChecksum(status_data, 8);
            
            SendUARTFrame(&status_frame);
            break;
        }
            
        case CMD_ACK:
            DEBUG_PRINT("Received ACK from ESP32");
            break;
            
        case CMD_NACK:
            DEBUG_PRINT("Received NACK from ESP32");
            break;
            
        case CMD_ERROR:
            DEBUG_PRINT("Received ERROR from ESP32: %s", frame->data);
            break;
            
        default:
            DEBUG_PRINT("Unknown command: 0x%02X", frame->command);
            break;
    }
}

/* 发送UART帧 */
static void SendUARTFrame(UARTFrame *frame) {
    if (osSemaphoreAcquire(uartTxSemaphore, 100) == osOK) {
        /* 构建发送缓冲区 */
        uint8_t tx_buffer[PROTOCOL_HEADER_SIZE + PROTOCOL_MAX_DATA_SIZE];
        uint16_t total_length = PROTOCOL_HEADER_SIZE + frame->data_length;
        
        tx_buffer[0] = frame->start_byte;
        tx_buffer[1] = frame->command;
        tx_buffer[2] = frame->data_length;
        tx_buffer[3] = frame->checksum;
        
        if (frame->data_length > 0) {
            memcpy(&tx_buffer[PROTOCOL_HEADER_SIZE], frame->data, frame->data_length);
        }
        
        /* 发送数据 */
        HAL_UART_Transmit(&ESP32_UART, tx_buffer, total_length, 100);
        
        osSemaphoreRelease(uartTxSemaphore);
    }
}

/* 向ESP32请求天气数据 */
void RequestWeatherData(void) {
    UARTFrame request_frame;
    char city_id[32];
    
    /* 构建城市ID数据 */
    strcpy(city_id, CITY_ID);
    
    request_frame.start_byte = PROTOCOL_START_BYTE;
    request_frame.command = CMD_GET_WEATHER;
    request_frame.data_length = strlen(city_id);
    memcpy(request_frame.data, city_id, request_frame.data_length);
    request_frame.checksum = CalculateChecksum(request_frame.data, 
                                               request_frame.data_length);
    
    SendUARTFrame(&request_frame);
}

/* 计算校验和 */
static uint8_t CalculateChecksum(uint8_t *data, uint16_t length) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < length; i++) {
        checksum ^= data[i];
    }
    return checksum;
}

/* 验证帧 */
static bool ValidateFrame(UARTFrame *frame) {
    /* 检查起始字节 */
    if (frame->start_byte != PROTOCOL_START_BYTE) {
        return false;
    }
    
    /* 检查数据长度 */
    if (frame->data_length > PROTOCOL_MAX_DATA_SIZE) {
        return false;
    }
    
    /* 检查校验和 */
    uint8_t calculated_checksum = CalculateChecksum(frame->data, 
                                                   frame->data_length);
    if (calculated_checksum != frame->checksum) {
        return false;
    }
    
    return true;
}