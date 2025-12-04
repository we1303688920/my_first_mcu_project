#include "display_task.h"
#include "ui_render.h"
#include "project_defines.h"
#include "lcd_driver.h"
#include "cmsis_os.h"
#include <string.h>

/* 私有变量 */
static WeatherData current_weather;
static TimeData current_time;
static bool weather_updated = false;
static bool time_updated = false;
static uint32_t last_refresh_time = 0;

/* 显示任务 */
void DisplayTask(void *argument) {
    EventType event;
    TickType_t last_wake_time;

    DEBUG_PRINT("Display Task Started");

    /* 初始化LCD显示屏 */
    LCD_Init();
    LCD_Clear(COLOR_BLACK);

    last_wake_time = xTaskGetTickCount();

    while (1) {
        /* 检查是否有新的事件 */
        if (osMessageQueueGet(eventQueueHandle, &event, 0, 0) == osOK) {
            switch (event) {
                case EVENT_WEATHER_UPDATE:
                    weather_updated = true;
                    break;

                case EVENT_TIME_UPDATE:
                    time_updated = true;
                    break;

                case EVENT_MODE_CHANGE:
                    RenderModeChange(ui_config.current_mode);
                    break;

                default:
                    break;
            }
        }

        /* 获取最新的天气数据 */
        if (weather_updated) {
            WeatherData new_weather;
            if (osMessageQueueGet(weatherQueueHandle, &new_weather, 0, 0) == osOK) {
                current_weather = new_weather;
                weather_updated = false;
            }
        }

        /* 获取最新的时间数据 */
        if (time_updated) {
            TimeData new_time;
            if (osMessageQueueGet(timeQueueHandle, &new_time, 0, 0) == osOK) {
                current_time = new_time;
                time_updated = false;
            }
        }

        /* 根据当前模式渲染显示 */
        RenderDisplay(&current_time, &current_weather, &ui_config, &system_status);

        /* 等待下一次刷新 */
        vTaskDelayUntil(&last_wake_time, pdMS_TO_TICKS(ui_config.refresh_interval));
    }
}

/* 改变显示模式 */
void ChangeDisplayMode(DisplayMode new_mode) {
    if (new_mode < DISPLAY_MODE_COUNT) {
        ui_config.current_mode = new_mode;

        EventType event = EVENT_MODE_CHANGE;
        osMessageQueuePut(eventQueueHandle, &event, 0, 0);
    }
}

/* 调整亮度 */
void AdjustBrightness(uint8_t brightness) {
    if (brightness >= 0 && brightness <= 100) {
        ui_config.brightness = brightness;
        LCD_SetBrightness(brightness);
    }
}

/* 切换温度单位 */
void ToggleTemperatureUnit(void) {
    if (ui_config.temp_unit == TEMP_UNIT_CELSIUS) {
        ui_config.temp_unit = TEMP_UNIT_FAHRENHEIT;
    } else {
        ui_config.temp_unit = TEMP_UNIT_CELSIUS;
    }
}

/* 切换时间格式 */
void ToggleTimeFormat(void) {
    if (ui_config.time_format == TIME_FORMAT_24H) {
        ui_config.time_format = TIME_FORMAT_12H;
    } else {
        ui_config.time_format = TIME_FORMAT_24H;
    }
}