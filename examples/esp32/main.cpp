/**
 * ESP32 Convex Client Example
 * 
 * This example demonstrates using the Convex C++ client on ESP32.
 * 
 * Requirements:
 *   - ESP-IDF v4.4 or later
 *   - WiFi credentials configured
 * 
 * Setup:
 *   1. Copy convex-cplusplus to your ESP-IDF project components folder
 *   2. Configure WiFi in menuconfig
 *   3. Build and flash
 */

#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "convex/convex.hpp"

static const char* TAG = "convex_example";

// WiFi credentials - configure these or use menuconfig
#define WIFI_SSID      CONFIG_WIFI_SSID
#define WIFI_PASSWORD  CONFIG_WIFI_PASSWORD

// Convex deployment URL
#define CONVEX_URL     "https://your-app.convex.cloud"

// Event group for WiFi connection
static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, reconnecting...");
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// Initialize WiFi
static void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));
    
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization complete");
}

// Wait for WiFi connection
static void wait_for_wifi() {
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");
}

// Convex client task
static void convex_task(void* pvParameters) {
    ESP_LOGI(TAG, "Starting Convex client...");
    
    try {
        convex::ConvexClient client(CONVEX_URL);
        
        // Monitor connection state
        client.on_connection_change([](convex::ConnectionState state) {
            switch (state) {
                case convex::ConnectionState::Connecting:
                    ESP_LOGI(TAG, "Convex: Connecting...");
                    break;
                case convex::ConnectionState::Connected:
                    ESP_LOGI(TAG, "Convex: Connected!");
                    break;
                case convex::ConnectionState::Disconnected:
                    ESP_LOGW(TAG, "Convex: Disconnected");
                    break;
                default:
                    break;
            }
        });
        
        // Subscribe to a query
        auto subscription = client.subscribe(
            "sensors:getConfig",  // Your query path
            {},
            [](const convex::FunctionResult& result) {
                if (result.is_success()) {
                    ESP_LOGI(TAG, "Config update: %s", 
                             result.value.to_string().c_str());
                    
                    // Example: Extract a value from the config
                    if (result.value.is_object() && 
                        result.value.contains("interval")) {
                        double interval = result.value["interval"].as_float64();
                        ESP_LOGI(TAG, "Sensor interval: %g ms", interval);
                    }
                } else {
                    ESP_LOGE(TAG, "Query error: %s", 
                             result.error_message.c_str());
                }
            }
        );
        
        ESP_LOGI(TAG, "Subscribed to sensors:getConfig");
        
        // Example: Send sensor data periodically
        int reading_count = 0;
        while (true) {
            // Simulate sensor reading
            float temperature = 20.0f + (esp_random() % 100) / 10.0f;
            float humidity = 40.0f + (esp_random() % 400) / 10.0f;
            
            // Send to Convex
            client.mutation_async(
                "sensors:recordReading",
                {
                    {"temperature", static_cast<double>(temperature)},
                    {"humidity", static_cast<double>(humidity)},
                    {"readingNumber", static_cast<int64_t>(++reading_count)}
                },
                [](const convex::FunctionResult& result) {
                    if (result.is_success()) {
                        ESP_LOGI(TAG, "Reading recorded");
                    } else {
                        ESP_LOGE(TAG, "Failed to record: %s", 
                                 result.error_message.c_str());
                    }
                }
            );
            
            // Process events and wait
            for (int i = 0; i < 50; i++) {  // 5 seconds
                client.poll();
                vTaskDelay(pdMS_TO_TICKS(100));
            }
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Convex error: %s", e.what());
    }
    
    vTaskDelete(NULL);
}

extern "C" void app_main() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || 
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Convex C++ Client ESP32 Example");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    
    // Initialize and connect WiFi
    wifi_init();
    wait_for_wifi();
    
    // Start Convex client task
    xTaskCreate(convex_task, "convex_task", 8192, NULL, 5, NULL);
}
