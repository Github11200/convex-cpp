/**
 * Simple Subscription Example
 * 
 * Demonstrates how to subscribe to a Convex query and receive real-time updates.
 * 
 * Usage:
 *   ./simple_subscribe <deployment_url>
 * 
 * Example:
 *   ./simple_subscribe https://your-app.convex.cloud
 */

#include <convex/convex.hpp>
#include <iostream>
#include <csignal>

// Global client pointer for signal handler
convex::ConvexClient* g_client = nullptr;

void signal_handler(int signal) {
    std::cout << "\nReceived signal " << signal << ", stopping..." << std::endl;
    if (g_client) {
        g_client->stop();
    }
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <deployment_url>" << std::endl;
        std::cerr << "Example: " << argv[0] << " https://your-app.convex.cloud" << std::endl;
        return 1;
    }
    
    std::string deployment_url = argv[1];
    
    std::cout << "Convex C++ Client v" << convex::VERSION << std::endl;
    std::cout << "Connecting to: " << deployment_url << std::endl;
    
    try {
        // Create client
        convex::ConvexClient client(deployment_url);
        g_client = &client;
        
        // Set up signal handler for graceful shutdown
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        // Monitor connection state
        client.on_connection_change([](convex::ConnectionState state) {
            switch (state) {
                case convex::ConnectionState::Connecting:
                    std::cout << "[Connection] Connecting..." << std::endl;
                    break;
                case convex::ConnectionState::Connected:
                    std::cout << "[Connection] Connected!" << std::endl;
                    break;
                case convex::ConnectionState::Disconnected:
                    std::cout << "[Connection] Disconnected" << std::endl;
                    break;
                default:
                    break;
            }
        });
        
        // Subscribe to a query
        // Change this to match your Convex schema
        auto subscription = client.subscribe(
            "messages:list",  // Your query path
            {},               // Query arguments
            [](const convex::FunctionResult& result) {
                if (result.is_success()) {
                    std::cout << "[Query Update] " << result.value.to_string() << std::endl;
                } else {
                    std::cerr << "[Query Error] " << result.error_message << std::endl;
                }
            }
        );
        
        std::cout << "Subscribed to messages:list" << std::endl;
        std::cout << "Press Ctrl+C to exit" << std::endl;
        
        // Run the event loop
        client.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Goodbye!" << std::endl;
    return 0;
}
