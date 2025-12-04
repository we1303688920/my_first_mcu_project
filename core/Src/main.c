#include "main.h"
#include "cmsis_os.h"
#include "project_config.h"
#include "project_defines.h"
#include "display_task.h"
#include "uart_comm_task.h"
#include "rtc_task.h"
#include "button_task.h"

/* 外设句柄 */
SPI_HandleTypeDef hspi2;
UART_HandleTypeDef huart2;
UART_HandleTypeDef huart3;
RTC_HandleTypeDef hrtc;
TIM_HandleTypeDef htim6;

/* FreeRTOS句柄 */
osThreadId_t displayTaskHandle;
osThreadId_t uartRxTaskHandle;
osThreadId_t buttonTaskHandle;
osThreadId_t rtcTaskHandle;
osThreadId_t uiUpdateTaskHandle;

/* 队列和信号量句柄 */
osMessageQueueId_t weatherQueueHandle;
osMessageQueueId_t timeQueueHandle;
osMessageQueueId_t commandQueueHandle;
osMessageQueueId_t eventQueueHandle;
osSemaphoreId_t uartTxSemaphore;
osSemaphoreId_t spiSemaphore;
osMutexId_t displayMutex;
EventGroupHandle_t systemEventGroup;

/* 全局变量 */
SystemStatus system_status = {0};
UIConfig ui_config = {
    .current_mode = DISPLAY_MODE_CLOCK,
    .time_format = TIME_FORMAT_24H,
    .temp_unit = TEMP_UNIT_CELSIUS,
    .brightness = 80,
    .refresh_interval = 1000,
    .auto_switch_mode = true,
    .show_seconds = true,
    .show_date = true
};

/* 函数原型 */
static void SystemClock_Config(void);
static void MX_GPIO_Init(void);
static void MX_USART2_UART_Init(void);
static void MX_USART3_UART_Init(void);
static void MX_SPI2_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM6_Init(void);
static void CreateFreeRTOSObjects(void);
static void CreateApplicationTasks(void);
static void Error_Handler(void);

int main(void) {
    /* 复位所有外设，初始化Flash接口和Systick */
    HAL_Init();

    /* 配置系统时钟 */
    SystemClock_Config();

    /* 初始化所有外设 */
    MX_GPIO_Init();
    MX_USART2_UART_Init();
    MX_USART3_UART_Init();
    MX_SPI2_Init();
    MX_RTC_Init();
    MX_TIM6_Init();

    /* 初始化系统状态 */
    system_status.wifi_connected = false;
    system_status.weather_data_valid = false;
    system_status.time_synced = false;
    system_status.battery_level = 100;
    system_status.error_code = 0;
    system_status.uptime_seconds = 0;

    /* 创建FreeRTOS对象 */
    CreateFreeRTOSObjects();

    /* 创建应用任务 */
    CreateApplicationTasks();

    /* 启动调度器 */
    osKernelStart();

    /* 程序不应该执行到这里 */
    while (1) {
        Error_Handler();
    }
}

static void CreateFreeRTOSObjects(void) {
    /* 创建队列 */
    weatherQueueHandle = osMessageQueueNew(5, sizeof(WeatherData), NULL);
    timeQueueHandle = osMessageQueueNew(5, sizeof(TimeData), NULL);
    commandQueueHandle = osMessageQueueNew(10, sizeof(UARTFrame), NULL);
    eventQueueHandle = osMessageQueueNew(20, sizeof(EventType), NULL);

    /* 创建信号量 */
    uartTxSemaphore = osSemaphoreNew(1, 1, NULL);
    spiSemaphore = osSemaphoreNew(1, 1, NULL);

    /* 创建互斥量 */
    displayMutex = osMutexNew(NULL);

    /* 创建事件组 */
    systemEventGroup = xEventGroupCreate();
}

static void CreateApplicationTasks(void) {
    const osThreadAttr_t displayTask_attributes = {
        .name = "DisplayTask",
        .stack_size = STACK_SIZE_DISPLAY,
        .priority = TASK_PRIORITY_DISPLAY,
    };

    const osThreadAttr_t uartRxTask_attributes = {
        .name = "UartRxTask",
        .stack_size = STACK_SIZE_UART_RX,
        .priority = TASK_PRIORITY_UART_RX,
    };

    const osThreadAttr_t buttonTask_attributes = {
        .name = "ButtonTask",
        .stack_size = STACK_SIZE_BUTTON,
        .priority = TASK_PRIORITY_BUTTON,
    };

    const osThreadAttr_t rtcTask_attributes = {
        .name = "RTCTask",
        .stack_size = STACK_SIZE_RTC,
        .priority = TASK_PRIORITY_RTC,
    };

    const osThreadAttr_t uiUpdateTask_attributes = {
        .name = "UIUpdateTask",
        .stack_size = STACK_SIZE_UI_UPDATE,
        .priority = TASK_PRIORITY_UI_UPDATE,
    };

    /* 创建任务 */
    displayTaskHandle = osThreadNew(DisplayTask, NULL, &displayTask_attributes);
    uartRxTaskHandle = osThreadNew(UartRxTask, NULL, &uartRxTask_attributes);
    buttonTaskHandle = osThreadNew(ButtonTask, NULL, &buttonTask_attributes);
    rtcTaskHandle = osThreadNew(RTCTask, NULL, &rtcTask_attributes);
    uiUpdateTaskHandle = osThreadNew(UIUpdateTask, NULL, &uiUpdateTask_attributes);
}

/* 系统时钟配置 */
static void SystemClock_Config(void) {
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

    /* 配置主PLL */
    RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
    RCC_OscInitStruct.HSEState = RCC_HSE_ON;
    RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
    RCC_OscInitStruct.PLL.PLLM = 8;
    RCC_OscInitStruct.PLL.PLLN = 336;
    RCC_OscInitStruct.PLL.PLLP = RCC_PLLP_DIV2;
    RCC_OscInitStruct.PLL.PLLQ = 7;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) {
        Error_Handler();
    }

    /* 初始化CPU，AHB和APB总线时钟 */
    RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV4;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV2;

    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_5) != HAL_OK) {
        Error_Handler();
    }

    /* 外设时钟配置 */
    PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_RTC;
    PeriphClkInitStruct.RTCClockSelection = RCC_RTCCLKSOURCE_LSE;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK) {
        Error_Handler();
    }
}

/* UART3初始化 - ESP32通信 */
static void MX_USART3_UART_Init(void) {
    huart3.Instance = USART3;
    huart3.Init.BaudRate = UART_BAUDRATE;
    huart3.Init.WordLength = UART_WORDLENGTH_8B;
    huart3.Init.StopBits = UART_STOPBITS_1;
    huart3.Init.Parity = UART_PARITY_NONE;
    huart3.Init.Mode = UART_MODE_TX_RX;
    huart3.Init.HwFlowCtl = UART_HWCONTROL_NONE;
    huart3.Init.OverSampling = UART_OVERSAMPLING_16;
    if (HAL_UART_Init(&huart3) != HAL_OK) {
        Error_Handler();
    }

    /* 启用DMA接收 */
    HAL_UART_Receive_DMA(&huart3, uart_rx_buffer, UART_RX_BUFFER_SIZE);
}

/* SPI2初始化 - LCD显示屏 */
static void MX_SPI2_Init(void) {
    hspi2.Instance = SPI2;
    hspi2.Init.Mode = SPI_MODE_MASTER;
    hspi2.Init.Direction = SPI_DIRECTION_2LINES;
    hspi2.Init.DataSize = SPI_DATASIZE_8BIT;
    hspi2.Init.CLKPolarity = SPI_POLARITY_LOW;
    hspi2.Init.CLKPhase = SPI_PHASE_1EDGE;
    hspi2.Init.NSS = SPI_NSS_SOFT;
    hspi2.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
    hspi2.Init.FirstBit = SPI_FIRSTBIT_MSB;
    hspi2.Init.TIMode = SPI_TIMODE_DISABLE;
    hspi2.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    hspi2.Init.CRCPolynomial = 10;
    if (HAL_SPI_Init(&hspi2) != HAL_OK) {
        Error_Handler();
    }
}

/* 错误处理 */
static void Error_Handler(void) {
    __disable_irq();
    while (1) {
        HAL_GPIO_TogglePin(LED_ERROR_GPIO_Port, LED_ERROR_Pin);
        HAL_Delay(500);
    }
}