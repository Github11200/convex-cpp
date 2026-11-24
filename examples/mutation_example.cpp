/**
 * Mutation Example
 * 
 * Demonstrates how to call Convex mutations from C++.
 * 
 * Usage:
 *   ./mutation_example <deployment_url>
 */

#include <convex/convex.hpp>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <deployment_url>" << std::endl;
        return 1;
    }
    
    std::string deployment_url = argv[1];
    
    std::cout << "Convex C++ Client v" << convex::VERSION << std::endl;
    std::cout << "Connecting to: " << deployment_url << std::endl;
    
    try {
        convex::ConvexClient client(deployment_url);
        
        // Wait for connection
        bool connected = false;
        client.on_connection_change([&connected](convex::ConnectionState state) {
            if (state == convex::ConnectionState::Connected) {
                connected = true;
            }
        });
        
        // Start connection in background
        std::thread run_thread([&client]() {
            client.run();
        });
        
        // Wait for connection
        while (!connected) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "Connected!" << std::endl;
        
        // Call a mutation (blocking)
        std::cout << "Calling mutation..." << std::endl;
        
        auto result = client.mutation(
            "messages:send",  // Your mutation path
            {
                {"body", "Hello from C++!"},
                {"author", "C++ Client"}
            }
        );
        
        if (result.is_success()) {
            std::cout << "Mutation succeeded: " << result.value.to_string() << std::endl;
        } else {
            std::cerr << "Mutation failed: " << result.error_message << std::endl;
        }
        
        // You can also use async mutations
        std::cout << "Calling async mutation..." << std::endl;
        
        std::atomic<bool> done{false};
        client.mutation_async(
            "messages:send",
            {
                {"body", "Async hello from C++!"},
                {"author", "C++ Client"}
            },
            [&done](const convex::FunctionResult& result) {
                if (result.is_success()) {
                    std::cout << "Async mutation succeeded!" << std::endl;
                } else {
                    std::cerr << "Async mutation failed: " << result.error_message << std::endl;
                }
                done = true;
            }
        );
        
        // Wait for async mutation to complete
        while (!done) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Stop the client
        client.stop();
        run_thread.join();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    std::cout << "Done!" << std::endl;
    return 0;
}
