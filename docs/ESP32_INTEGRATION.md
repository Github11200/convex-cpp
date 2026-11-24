# ESP32 Integration Guide

This guide explains how to integrate the Convex C++ client into your ESP32 project using ESP-IDF.

## Prerequisites

- ESP-IDF v5.0 or later (v5.1+ recommended)
- ESP32 with WiFi (ESP32, ESP32-S2, ESP32-S3, ESP32-C3, etc.)
- A Convex deployment

## Method 1: As an ESP-IDF Component (Recommended)

### Step 1: Copy the Component

Copy the `convex-cplusplus` folder into your project's `components` directory:

```bash
# From your ESP-IDF project root
mkdir -p components
cp -r /path/to/convex-cplusplus components/convex
```

### Step 2: Create the Component CMakeLists.txt

Create `components/convex/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS
        "src/value.cpp"
        "src/protocol.cpp"
        "src/base_client.cpp"
        "src/client.cpp"
        "src/transport.cpp"
        "src/platform/esp32_transport.cpp"
    INCLUDE_DIRS
        "include"
        "third_party"
    REQUIRES
        esp_websocket_client
        esp_event
        nvs_flash
)

target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)
```

### Step 3: Configure sdkconfig

Add these options to your `sdkconfig` or use `idf.py menuconfig`:

```
# Enable C++ exceptions (optional but recommended)
CONFIG_COMPILER_CXX_EXCEPTIONS=y

# Increase main task stack size (Convex client needs ~8KB)
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192

# Enable WebSocket client
CONFIG_ESP_WEBSOCKET_CLIENT_ENABLE=y

# Increase WebSocket buffer if you have large queries
CONFIG_WEBSOCKET_CLIENT_BUFFER_SIZE=4096
```

### Step 4: Use in Your Code

```cpp
// main.cpp
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "convex/convex.hpp"

static const char* TAG = "main";

// Your Convex deployment URL
#define CONVEX_URL "https://your-app.convex.cloud"

void convex_task(void* param) {
    ESP_LOGI(TAG, "Starting Convex client...");
    
    try {
        convex::ConvexClient client(CONVEX_URL);
        
        // Subscribe to a query
        auto sub = client.subscribe("settings:get", {},
            [](const convex::FunctionResult& result) {
                if (result.is_success()) {
                    ESP_LOGI(TAG, "Settings: %s", result.value.to_string().c_str());
                } else {
                    ESP_LOGE(TAG, "Query error: %s", result.error_message.c_str());
                }
            });
        
        // Main loop - poll for updates
        while (true) {
            client.poll();
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
    } catch (const std::exception& e) {
        ESP_LOGE(TAG, "Convex error: %s", e.what());
    }
    
    vTaskDelete(NULL);
}

extern "C" void app_main() {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Initialize WiFi (your WiFi setup code here)
    // ...
    
    // Wait for WiFi connection
    // ...
    
    // Start Convex task
    xTaskCreate(convex_task, "convex", 8192, NULL, 5, NULL);
}
```

## Method 2: As a Standalone Library

If you prefer not to use the component system:

### Step 1: Add Source Files to Your Project

In your project's `CMakeLists.txt`:

```cmake
set(CONVEX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/lib/convex-cplusplus")

set(CONVEX_SOURCES
    ${CONVEX_DIR}/src/value.cpp
    ${CONVEX_DIR}/src/protocol.cpp
    ${CONVEX_DIR}/src/base_client.cpp
    ${CONVEX_DIR}/src/client.cpp
    ${CONVEX_DIR}/src/transport.cpp
    ${CONVEX_DIR}/src/platform/esp32_transport.cpp
)

idf_component_register(
    SRCS 
        "main.cpp"
        ${CONVEX_SOURCES}
    INCLUDE_DIRS 
        "."
        "${CONVEX_DIR}/include"
        "${CONVEX_DIR}/third_party"
    REQUIRES
        esp_websocket_client
        esp_event
        nvs_flash
)
```

## Memory Considerations

The ESP32 has limited RAM. Here are tips for optimal usage:

| Resource | Recommendation |
|----------|----------------|
| Task Stack | 8KB minimum for Convex task |
| Subscriptions | Limit to 2-3 active subscriptions |
| Query Results | Keep result data small (<1KB each) |
| WebSocket Buffer | 2-4KB is usually sufficient |

### Reducing Memory Usage

```cpp
// 1. Unsubscribe when not needed
{
    auto sub = client.subscribe("data:get", {}, callback);
    // ... use subscription
} // Automatically unsubscribes when sub goes out of scope

// 2. Use poll() instead of run() for more control
while (running) {
    client.poll();
    
    // Do other work between polls
    read_sensors();
    
    vTaskDelay(pdMS_TO_TICKS(100));
}

// 3. Process results immediately, don't store them
client.subscribe("data:get", {}, [](const auto& result) {
    if (result.is_success()) {
        // Process immediately
        process_data(result.value);
        // Don't store result.value
    }
});
```

## WiFi Setup Example

Here's a complete WiFi setup example:

```cpp
#include "esp_wifi.h"
#include "esp_event.h"
#include "freertos/event_groups.h"

#define WIFI_SSID "your-ssid"
#define WIFI_PASS "your-password"

static EventGroupHandle_t wifi_event_group;
const int WIFI_CONNECTED_BIT = BIT0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
        xEventGroupClearBits(wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

void wifi_init() {
    wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));
    
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASS);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    // Wait for connection
    xEventGroupWaitBits(wifi_event_group, WIFI_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);
}
```

## Troubleshooting

### "Stack overflow" or crashes

Increase the task stack size:
```cpp
xTaskCreate(convex_task, "convex", 16384, NULL, 5, NULL);  // 16KB
```

### WebSocket connection fails

1. Check WiFi is connected before creating ConvexClient
2. Verify the Convex URL is correct (use https://, not wss://)
3. Check ESP32 has internet access (try pinging)

### SSL/TLS errors

The ESP32 uses mbedTLS. Make sure:
```
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_ESP_TLS_USING_MBEDTLS=y
```

### Out of memory

- Reduce number of subscriptions
- Use smaller query results
- Reduce WebSocket buffer size
- Consider using ESP32 with PSRAM

## Example Project Structure

```
my-esp32-project/
├── CMakeLists.txt
├── main/
│   ├── CMakeLists.txt
│   └── main.cpp
├── components/
│   └── convex/              # Copy convex-cplusplus here
│       ├── CMakeLists.txt   # Use esp32_component/CMakeLists.txt
│       ├── include/
│       ├── src/
│       └── third_party/
├── sdkconfig
└── sdkconfig.defaults
```

## Minimal Example

Here's the simplest possible ESP32 + Convex example:

```cpp
#include "convex/convex.hpp"
#include "esp_log.h"

void app_main() {
    // ... WiFi setup ...
    
    convex::ConvexClient client("https://your-app.convex.cloud");
    
    // One-shot query
    auto result = client.query("config:get", {});
    if (result.is_success()) {
        ESP_LOGI("convex", "Config: %s", result.value.to_string().c_str());
    }
    
    // Send data
    client.mutation("sensors:record", {
        {"temperature", 25.5},
        {"humidity", 60.0}
    });
}
```
