cmake_minimum_required(VERSION 3.14)
project(convex-cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(CONVEX_BUILD_EXAMPLES "Build example programs" ON)
option(CONVEX_BUILD_TESTS "Build tests" OFF)

# Platform detection
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(CONVEX_PLATFORM "POSIX")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    set(CONVEX_PLATFORM "POSIX")
elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
    set(CONVEX_PLATFORM "WINDOWS")
elseif(DEFINED ESP_PLATFORM)
    set(CONVEX_PLATFORM "ESP32")
else()
    message(FATAL_ERROR "Unsupported platform: ${CMAKE_SYSTEM_NAME}")
endif()

message(STATUS "Building for platform: ${CONVEX_PLATFORM}")

# Source files
set(CONVEX_SOURCES
    src/value.cpp
    src/protocol.cpp
    src/base_client.cpp
    src/client.cpp
    src/transport.cpp
)

# Platform-specific sources
if(CONVEX_PLATFORM STREQUAL "POSIX")
    list(APPEND CONVEX_SOURCES src/platform/posix_transport.cpp)
elseif(CONVEX_PLATFORM STREQUAL "WINDOWS")
    list(APPEND CONVEX_SOURCES src/platform/win_transport.cpp)
elseif(CONVEX_PLATFORM STREQUAL "ESP32")
    list(APPEND CONVEX_SOURCES src/platform/esp32_transport.cpp)
endif()

# Library
add_library(convex ${CONVEX_SOURCES})

target_include_directories(convex
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Platform-specific dependencies
if(CONVEX_PLATFORM STREQUAL "POSIX")
    find_package(OpenSSL REQUIRED)
    target_link_libraries(convex PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    target_link_libraries(convex PUBLIC pthread)
elseif(CONVEX_PLATFORM STREQUAL "WINDOWS")
    target_link_libraries(convex PUBLIC winhttp ws2_32)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(convex PRIVATE -Wall -Wextra -Wpedantic)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    target_compile_options(convex PRIVATE /W4)
endif()

# Examples
if(CONVEX_BUILD_EXAMPLES AND NOT CONVEX_PLATFORM STREQUAL "ESP32")
    add_executable(simple_subscribe examples/simple_subscribe.cpp)
    target_link_libraries(simple_subscribe convex)
    
    add_executable(mutation_example examples/mutation_example.cpp)
    target_link_libraries(mutation_example convex)
endif()

# Tests
if(CONVEX_BUILD_TESTS AND NOT CONVEX_PLATFORM STREQUAL "ESP32")
    enable_testing()
    
    set(TEST_SOURCES
        tests/test_main.cpp
        tests/test_value.cpp
        tests/test_value_ordering.cpp
        tests/test_protocol.cpp
        tests/test_base_client.cpp
        tests/test_identifier.cpp
        tests/test_udf_path.cpp
        tests/test_transport.cpp
        tests/test_mock_transport.cpp
        tests/test_integration.cpp
    )
    
    add_executable(convex_tests ${TEST_SOURCES})
    target_link_libraries(convex_tests convex)
    target_include_directories(convex_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    )
    
    # Register tests with CTest
    add_test(NAME convex_tests COMMAND convex_tests)
    
    # Convenience target
    add_custom_target(test_all
        COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
        DEPENDS convex_tests
    )
endif()

# Installation
include(GNUInstallDirs)

install(TARGETS convex
    EXPORT convex-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY include/convex
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT convex-targets
    FILE convex-config.cmake
    NAMESPACE convex::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/convex
)
