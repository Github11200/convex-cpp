#pragma once

#include "base_client.hpp"
#include "transport.hpp"
#include "platform/platform_detect.hpp"
#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <functional>

namespace convex {

/**
 * Query subscription handle.
 * Automatically unsubscribes when destroyed.
 */
class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;
    Subscription(Subscription&& other) noexcept;
    Subscription& operator=(Subscription&& other) noexcept;
    ~Subscription();
    
    /**
     * Get the current result (if available)
     */
    std::optional<FunctionResult> current() const;
    
    /**
     * Check if subscription is active
     */
    bool is_active() const { return client_ != nullptr; }
    
    /**
     * Explicitly unsubscribe
     */
    void unsubscribe();
    
private:
    friend class ConvexClient;
    Subscription(class ConvexClient* client, SubscriberId id);
    
    class ConvexClient* client_ = nullptr;
    SubscriberId id_;
};

/**
 * ConvexClient - High-level client for interacting with Convex.
 * 
 * This class manages:
 * - WebSocket connection with automatic reconnection
 * - Background thread for message processing
 * - Query subscriptions with real-time updates
 * - Mutation and action requests
 * - Authentication
 * 
 * Example usage:
 * ```cpp
 * ConvexClient client("https://your-app.convex.cloud");
 * 
 * auto sub = client.subscribe("messages:list", {}, [](const Value& result) {
 *     std::cout << "Got update: " << result.to_string() << std::endl;
 * });
 * 
 * client.mutation("messages:send", {
 *     {"body", "Hello!"},
 *     {"author", "C++"}
 * });
 * 
 * client.run();  // Run until stopped
 * ```
 */
class ConvexClient {
public:
    /**
     * Construct a client connected to the given deployment URL
     * @param deployment_url The Convex deployment URL (e.g., "https://your-app.convex.cloud")
     */
    explicit ConvexClient(const std::string& deployment_url);
    
    /**
     * Construct with custom transport (for testing or special platforms)
     */
    ConvexClient(const std::string& deployment_url, std::unique_ptr<ITransport> transport);
    
    ~ConvexClient();
    
    // Non-copyable, non-movable
    ConvexClient(const ConvexClient&) = delete;
    ConvexClient& operator=(const ConvexClient&) = delete;
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * Connect to the Convex server.
     * Usually called automatically, but can be called explicitly.
     */
    void connect();
    
    /**
     * Disconnect from the server
     */
    void disconnect();
    
    /**
     * Check if connected
     */
    bool is_connected() const;
    
    /**
     * Set callback for connection state changes
     */
    void on_connection_change(std::function<void(ConnectionState)> callback);
    
    // ========================================================================
    // Query Subscriptions
    // ========================================================================
    
    /**
     * Subscribe to a query with real-time updates
     * @param udf_path Path to the query function (e.g., "messages:list")
     * @param args Query arguments
     * @param on_update Called whenever the query result changes
     * @return Subscription handle (unsubscribes when destroyed)
     */
    Subscription subscribe(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_update
    );
    
    /**
     * One-shot query (subscribes, gets result, unsubscribes)
     * Blocks until result is available.
     */
    FunctionResult query(
        const std::string& udf_path,
        const Object& args
    );
    
    // ========================================================================
    // Mutations and Actions
    // ========================================================================
    
    /**
     * Call a mutation (async)
     * @param udf_path Path to the mutation function
     * @param args Mutation arguments
     * @param on_complete Called when mutation completes
     */
    void mutation_async(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_complete
    );
    
    /**
     * Call a mutation (blocking)
     * Blocks until mutation completes.
     */
    FunctionResult mutation(
        const std::string& udf_path,
        const Object& args
    );
    
    /**
     * Call an action (async)
     */
    void action_async(
        const std::string& udf_path,
        const Object& args,
        std::function<void(const FunctionResult&)> on_complete
    );
    
    /**
     * Call an action (blocking)
     */
    FunctionResult action(
        const std::string& udf_path,
        const Object& args
    );
    
    // ========================================================================
    // Authentication
    // ========================================================================
    
    /**
     * Set authentication token (e.g., JWT from Auth0/Clerk)
     */
    void set_auth(const std::string& token);
    
    /**
     * Clear authentication
     */
    void clear_auth();
    
    /**
     * Set auth token fetcher for automatic refresh
     */
    void set_auth_fetcher(std::function<std::string(bool force_refresh)> fetcher);
    
    // ========================================================================
    // Event Loop
    // ========================================================================
    
    /**
     * Process pending events (non-blocking)
     * Call this regularly if not using run().
     */
    void poll();
    
    /**
     * Run the event loop (blocking)
     * Processes events until stop() is called.
     */
    void run();
    
    /**
     * Stop the event loop
     */
    void stop();
    
private:
    friend class Subscription;
    
    void unsubscribe(const SubscriberId& id);
    void send_message(const ClientMessage& msg);
    void handle_open();
    void handle_message(const std::string& msg);
    void handle_close(const std::string& reason);
    void handle_error(const std::string& error);
    void attempt_reconnect();
    std::string deployment_to_ws_url(const std::string& deployment_url);
    
    // Configuration
    std::string deployment_url_;
    std::string ws_url_;
    std::string client_id_;
    
    // Core components
    std::unique_ptr<ITransport> transport_;
    BaseConvexClient base_client_;
    
    // Connection state
    std::string session_id_;
    uint32_t connection_count_ = 0;
    std::string last_close_reason_ = "InitialConnect";
    Backoff backoff_;
    std::atomic<bool> running_{false};
    std::atomic<bool> should_reconnect_{true};
    
    // Callbacks
    std::function<void(ConnectionState)> on_connection_change_;
    
    // Request queue for thread-safe message sending
    std::queue<ClientMessage> outgoing_queue_;
    std::mutex queue_mutex_;
    
    // For blocking operations
    std::mutex sync_mutex_;
    std::condition_variable sync_cv_;
};

} // namespace convex
