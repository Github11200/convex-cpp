# ESP-IDF Component CMakeLists.txt for Convex C++ Client
#
# To use this as a component in your ESP-IDF project:
# 1. Copy the convex-cplusplus folder to your project's components directory
# 2. Add this CMakeLists.txt to the root of the copied folder
# 3. Build your project with idf.py build

idf_component_register(
    SRCS
        "../../src/value.cpp"
        "../../src/protocol.cpp"
        "../../src/base_client.cpp"
        "../../src/client.cpp"
        "../../src/transport.cpp"
        "../../src/platform/esp32_transport.cpp"
    INCLUDE_DIRS
        "../../include"
        "../../third_party"
    REQUIRES
        esp_websocket_client
        esp_event
        nvs_flash
)

# Enable C++17
target_compile_features(${COMPONENT_LIB} PUBLIC cxx_std_17)
