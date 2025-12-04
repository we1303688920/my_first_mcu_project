#include <stdio.h>
#include <string.h>
#include <time.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "cJSON.h"
#include "weather_api.h"
#include "esp32_config.h"

/* 日志标签 */
static const char *TAG = "WEATHER_API";

/* API响应缓冲区 */
static char api_response_buffer[4096];

/* HTTP事件处理器 */
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    static int total_len = 0;

    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP_EVENT_ERROR");
            break;

        case HTTP_EVENT_ON_CONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_CONNECTED");
            total_len = 0;
            break;

        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGI(TAG, "HTTP_EVENT_HEADER_SENT");
            break;

        case HTTP_EVENT_ON_HEADER:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_HEADER: key=%s, value=%s",
                    evt->header_key, evt->header_value);
            break;

        case HTTP_EVENT_ON_DATA:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            if (total_len + evt->data_len < sizeof(api_response_buffer) - 1) {
                memcpy(api_response_buffer + total_len, evt->data, evt->data_len);
                total_len += evt->data_len;
                api_response_buffer[total_len] = '\0';
            } else {
                ESP_LOGE(TAG, "Response buffer overflow");
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(TAG, "HTTP_EVENT_ON_FINISH, total_len=%d", total_len);
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP_EVENT_DISCONNECTED");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/* 解析天气JSON数据 */
static bool parse_weather_json(const char *json_str, WeatherData *weather) {
    if (!json_str || !weather) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return false;
    }

    bool success = true;

    /* 解析主信息 */
    cJSON *name = cJSON_GetObjectItem(root, "name");
    if (cJSON_IsString(name)) {
        strncpy(weather->city, name->valuestring, sizeof(weather->city) - 1);
    }

    /* 解析天气数组 */
    cJSON *weather_array = cJSON_GetObjectItem(root, "weather");
    if (cJSON_IsArray(weather_array)) {
        cJSON *weather_item = cJSON_GetArrayItem(weather_array, 0);
        if (weather_item) {
            cJSON *description = cJSON_GetObjectItem(weather_item, "description");
            cJSON *main_weather = cJSON_GetObjectItem(weather_item, "main");
            cJSON *icon = cJSON_GetObjectItem(weather_item, "icon");

            if (cJSON_IsString(description)) {
                strncpy(weather->description, description->valuestring,
                        sizeof(weather->description) - 1);
            }

            if (cJSON_IsString(main_weather)) {
                /* 转换天气状况 */
                weather->condition = weather_string_to_condition(main_weather->valuestring);
            }
        }
    }

    /* 解析主数据 */
    cJSON *main = cJSON_GetObjectItem(root, "main");
    if (main) {
        cJSON *temp = cJSON_GetObjectItem(main, "temp");
        cJSON *feels_like = cJSON_GetObjectItem(main, "feels_like");
        cJSON *humidity = cJSON_GetObjectItem(main, "humidity");
        cJSON *pressure = cJSON_GetObjectItem(main, "pressure");

        if (cJSON_IsNumber(temp)) {
            weather->temperature = temp->valuedouble;
        }

        if (cJSON_IsNumber(feels_like)) {
            weather->feels_like = feels_like->valuedouble;
        }

        if (cJSON_IsNumber(humidity)) {
            weather->humidity = humidity->valueint;
        }

        if (cJSON_IsNumber(pressure)) {
            weather->pressure = pressure->valueint;
        }
    }

    /* 解析能见度 */
    cJSON *visibility = cJSON_GetObjectItem(root, "visibility");
    if (cJSON_IsNumber(visibility)) {
        weather->visibility = visibility->valueint;
    }

    /* 解析风数据 */
    cJSON *wind = cJSON_GetObjectItem(root, "wind");
    if (wind) {
        cJSON *speed = cJSON_GetObjectItem(wind, "speed");
        cJSON *deg = cJSON_GetObjectItem(wind, "deg");

        if (cJSON_IsNumber(speed)) {
            weather->wind_speed = speed->valuedouble;
        }

        if (cJSON_IsNumber(deg)) {
            weather->wind_degree = deg->valueint;
        }
    }

    /* 解析云量 */
    cJSON *clouds = cJSON_GetObjectItem(root, "clouds");
    if (clouds) {
        cJSON *all = cJSON_GetObjectItem(clouds, "all");
        if (cJSON_IsNumber(all)) {
            weather->cloudiness = all->valueint;
        }
    }

    /* 解析时间戳 */
    cJSON *dt = cJSON_GetObjectItem(root, "dt");
    if (cJSON_IsNumber(dt)) {
        weather->timestamp = dt->valueint;
    }

    /* 解析时区 */
    cJSON *timezone = cJSON_GetObjectItem(root, "timezone");
    if (cJSON_IsNumber(timezone)) {
        weather->timezone_offset = timezone->valueint;
    }

    /* 解析日出日落时间 */
    cJSON *sys = cJSON_GetObjectItem(root, "sys");
    if (sys) {
        cJSON *sunrise = cJSON_GetObjectItem(sys, "sunrise");
        cJSON *sunset = cJSON_GetObjectItem(sys, "sunset");
        cJSON *country = cJSON_GetObjectItem(sys, "country");

        if (cJSON_IsNumber(sunrise)) {
            weather->sunrise = sunrise->valueint;
        }

        if (cJSON_IsNumber(sunset)) {
            weather->sunset = sunset->valueint;
        }

        if (cJSON_IsString(country)) {
            strncpy(weather->country, country->valuestring,
                    sizeof(weather->country) - 1);
        }
    }

    cJSON_Delete(root);
    return success;
}

/* 天气字符串转换为枚举 */
WeatherCondition weather_string_to_condition(const char *weather_str) {
    if (!weather_str) {
        return WEATHER_UNKNOWN;
    }

    if (strcmp(weather_str, "Clear") == 0) {
        return WEATHER_SUNNY;
    } else if (strcmp(weather_str, "Clouds") == 0) {
        return WEATHER_CLOUDY;
    } else if (strcmp(weather_str, "Rain") == 0) {
        return WEATHER_RAIN;
    } else if (strcmp(weather_str, "Thunderstorm") == 0) {
        return WEATHER_THUNDERSTORM;
    } else if (strcmp(weather_str, "Snow") == 0) {
        return WEATHER_SNOW;
    } else if (strcmp(weather_str, "Mist") == 0 ||
               strcmp(weather_str, "Fog") == 0) {
        return WEATHER_FOG;
    } else if (strcmp(weather_str, "Drizzle") == 0) {
        return WEATHER_DRIZZLE;
    } else if (strcmp(weather_str, "Wind") == 0) {
        return WEATHER_WINDY;
    } else {
        return WEATHER_UNKNOWN;
    }
}

/* 获取当前天气数据 */
bool get_current_weather(const char *city_id, WeatherData *weather) {
    if (!city_id || !weather) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/weather?q=%s&units=metric&appid=%s",
             city_id, WEATHER_API_KEY);

    ESP_LOGI(TAG, "Fetching weather from: %s", url);

    /* 配置HTTP客户端 */
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .buffer_size = sizeof(api_response_buffer),
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    /* 执行HTTP请求 */
    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Weather data received successfully");
            success = parse_weather_json(api_response_buffer, weather);
        } else {
            ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
            ESP_LOGE(TAG, "Response: %s", api_response_buffer);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

/* 获取天气预报 */
bool get_weather_forecast(const char *city_id, WeatherForecast *forecast,
                         int days) {
    if (!city_id || !forecast || days <= 0 || days > 7) {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/forecast?q=%s&units=metric&cnt=%d&appid=%s",
             city_id, days * 8, WEATHER_API_KEY);  /* 8 forecasts per day */

    ESP_LOGI(TAG, "Fetching forecast from: %s", url);

    /* 配置HTTP客户端 */
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .buffer_size = sizeof(api_response_buffer),
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    /* 执行HTTP请求 */
    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Forecast data received successfully");
            success = parse_forecast_json(api_response_buffer, forecast, days);
        } else {
            ESP_LOGE(TAG, "HTTP request failed with status: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

/* 解析天气预报JSON */
static bool parse_forecast_json(const char *json_str, WeatherForecast *forecast,
                               int days) {
    if (!json_str || !forecast) {
        return false;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse forecast JSON");
        return false;
    }

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (!cJSON_IsArray(list)) {
        cJSON_Delete(root);
        return false;
    }

    int item_count = cJSON_GetArraySize(list);
    int forecasts_per_day = item_count / days;

    /* 解析每个预测条目 */
    for (int day = 0; day < days && day < MAX_FORECAST_DAYS; day++) {
        /* 选择每天中间时间的预测（例如12:00） */
        int index = day * forecasts_per_day + forecasts_per_day / 2;
        if (index >= item_count) {
            index = item_count - 1;
        }

        cJSON *item = cJSON_GetArrayItem(list, index);
        if (item) {
            /* 解析时间戳 */
            cJSON *dt = cJSON_GetObjectItem(item, "dt");
            if (cJSON_IsNumber(dt)) {
                forecast->days[day].timestamp = dt->valueint;

                /* 转换为本地时间 */
                time_t timestamp = dt->valueint;
                struct tm *timeinfo = localtime(&timestamp);
                forecast->days[day].day_of_week = timeinfo->tm_wday;
            }

            /* 解析天气数据 */
            cJSON *weather_array = cJSON_GetObjectItem(item, "weather");
            if (cJSON_IsArray(weather_array)) {
                cJSON *weather_item = cJSON_GetArrayItem(weather_array, 0);
                if (weather_item) {
                    cJSON *description = cJSON_GetObjectItem(weather_item, "description");
                    cJSON *main_weather = cJSON_GetObjectItem(weather_item, "main");

                    if (cJSON_IsString(description)) {
                        strncpy(forecast->days[day].description,
                                description->valuestring,
                                sizeof(forecast->days[day].description) - 1);
                    }

                    if (cJSON_IsString(main_weather)) {
                        forecast->days[day].condition =
                            weather_string_to_condition(main_weather->valuestring);
                    }
                }
            }

            /* 解析温度数据 */
            cJSON *main = cJSON_GetObjectItem(item, "main");
            if (main) {
                cJSON *temp = cJSON_GetObjectItem(main, "temp");
                cJSON *temp_min = cJSON_GetObjectItem(main, "temp_min");
                cJSON *temp_max = cJSON_GetObjectItem(main, "temp_max");
                cJSON *humidity = cJSON_GetObjectItem(main, "humidity");

                if (cJSON_IsNumber(temp)) {
                    forecast->days[day].temperature = temp->valuedouble;
                }

                if (cJSON_IsNumber(temp_min)) {
                    forecast->days[day].temp_min = temp_min->valuedouble;
                }

                if (cJSON_IsNumber(temp_max)) {
                    forecast->days[day].temp_max = temp_max->valuedouble;
                }

                if (cJSON_IsNumber(humidity)) {
                    forecast->days[day].humidity = humidity->valueint;
                }
            }

            /* 解析降水概率 */
            cJSON *pop = cJSON_GetObjectItem(item, "pop");
            if (cJSON_IsNumber(pop)) {
                forecast->days[day].precipitation_probability = (int)(pop->valuedouble * 100);
            }
        }
    }

    forecast->num_days = (days < MAX_FORECAST_DAYS) ? days : MAX_FORECAST_DAYS;

    cJSON_Delete(root);
    return true;
}

/* 获取空气质量数据 */
bool get_air_quality(const char *city_id, AirQualityData *air_quality) {
    if (!city_id || !air_quality) {
        return false;
    }

    /* 先获取经纬度 */
    WeatherData weather;
    if (!get_current_weather(city_id, &weather)) {
        return false;
    }

    /* 注意：这里需要实现经纬度获取，简化处理 */
    char url[256];
    snprintf(url, sizeof(url),
             "http://api.openweathermap.org/data/2.5/air_pollution?lat=%.6f&lon=%.6f&appid=%s",
             39.9042, 116.4074, WEATHER_API_KEY);  /* 北京坐标示例 */

    ESP_LOGI(TAG, "Fetching air quality from: %s", url);

    /* 配置HTTP客户端 */
    esp_http_client_config_t config = {
        .url = url,
        .event_handler = http_event_handler,
        .buffer_size = sizeof(api_response_buffer),
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        return false;
    }

    /* 执行HTTP请求 */
    esp_err_t err = esp_http_client_perform(client);

    bool success = false;
    if (err == ESP_OK) {
        int status_code = esp_http_client_get_status_code(client);
        if (status_code == 200) {
            ESP_LOGI(TAG, "Air quality data received");
            success = parse_air_quality_json(api_response_buffer, air_quality);
        } else {
            ESP_LOGE(TAG, "Air quality request failed: %d", status_code);
        }
    } else {
        ESP_LOGE(TAG, "Air quality request failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return success;
}

/* 解析空气质量JSON */
static bool parse_air_quality_json(const char *json_str, AirQualityData *air_quality) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        return false;
    }

    cJSON *list = cJSON_GetObjectItem(root, "list");
    if (cJSON_IsArray(list) && cJSON_GetArraySize(list) > 0) {
        cJSON *first_item = cJSON_GetArrayItem(list, 0);
        if (first_item) {
            cJSON *main = cJSON_GetObjectItem(first_item, "main");
            cJSON *components = cJSON_GetObjectItem(first_item, "components");

            if (cJSON_IsObject(main)) {
                cJSON *aqi = cJSON_GetObjectItem(main, "aqi");
                if (cJSON_IsNumber(aqi)) {
                    air_quality->aqi = aqi->valueint;
                }
            }

            if (cJSON_IsObject(components)) {
                cJSON *pm25 = cJSON_GetObjectItem(components, "pm2_5");
                cJSON *pm10 = cJSON_GetObjectItem(components, "pm10");
                cJSON *co = cJSON_GetObjectItem(components, "co");
                cJSON *no2 = cJSON_GetObjectItem(components, "no2");
                cJSON *so2 = cJSON_GetObjectItem(components, "so2");
                cJSON *o3 = cJSON_GetObjectItem(components, "o3");

                if (cJSON_IsNumber(pm25)) air_quality->pm2_5 = pm25->valuedouble;
                if (cJSON_IsNumber(pm10)) air_quality->pm10 = pm10->valuedouble;
                if (cJSON_IsNumber(co)) air_quality->co = co->valuedouble;
                if (cJSON_IsNumber(no2)) air_quality->no2 = no2->valuedouble;
                if (cJSON_IsNumber(so2)) air_quality->so2 = so2->valuedouble;
                if (cJSON_IsNumber(o3)) air_quality->o3 = o3->valuedouble;
            }
        }
    }

    cJSON_Delete(root);
    return true;
}

/* 获取AQI描述 */
const char* get_aqi_description(int aqi) {
    switch (aqi) {
        case 1: return "Good";
        case 2: return "Fair";
        case 3: return "Moderate";
        case 4: return "Poor";
        case 5: return "Very Poor";
        default: return "Unknown";
    }
}