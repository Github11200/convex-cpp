# ESP-IDF Component CMakeLists.txt for Convex C++ Client
#
# This file makes convex-cplusplus work as an ESP-IDF component.
# See ../docs/ESP32_INTEGRATION.md for setup instructions.

idf_component_register(
    SRCS
        "../src/value.cpp"
        "../src/protocol.cpp"
        "../src/base_client.cpp"
        "../src/client.cpp"
        "../src/transport.cpp"
        "../src/platform/esp32_transport.cpp"
    INCLUDE_DIRS
        "../include"
        "../third_party"
    REQUIRES
        esp_websocket_client
        esp_event
        nvs_flash
        json
)

# Enable C++20 features
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_20)

# Suppress some warnings that come from the library
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-missing-field-initializers
)
