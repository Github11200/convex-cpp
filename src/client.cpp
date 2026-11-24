#include "convex/client.hpp"
#include "convex/protocol.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace convex {

// ============================================================================
// Subscription Implementation
// ============================================================================

Subscription::Subscription(ConvexClient* client, SubscriberId id)
    : client_(client), id_(id) {}

Subscription::Subscription(Subscription&& other) noexcept
    : client_(other.client_), id_(other.id_) {
    other.client_ = nullptr;
}

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        client_ = other.client_;
        id_ = other.id_;
        other.client_ = nullptr;
    }
    return *this;
}

Subscription::~Subscription() {
    unsubscribe();
}

std::optional<FunctionResult> Subscription::current() const {
    if (!client_) return std::nullopt;
    return client_->base_client_.get_result(id_);
}

void Subscription::unsubscribe() {
    if (client_) {
        client_->unsubscribe(id_);
        client_ = nullptr;
    }
}

// ============================================================================
// ConvexClient Implementation
// ============================================================================

namespace {

std::string generate_uuid() {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dis;
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << dis(gen) << "-";
    oss << std::setw(4) << (dis(gen) & 0xFFFF) << "-";
    oss << std::setw(4) << ((dis(gen) & 0x0FFF) | 0x4000) << "-";
    oss << std::setw(4) << ((dis(gen) & 0x3FFF) | 0x8000) << "-";
    oss << std::setw(8) << dis(gen);
    oss << std::setw(4) << (dis(gen) & 0xFFFF);
    
    return oss.str();
}

} // anonymous namespace

ConvexClient::ConvexClient(const std::string& deployment_url)
    : ConvexClient(deployment_url, create_transport()) {}

ConvexClient::ConvexClient(
    const std::string& deployment_url,
    std::unique_ptr<ITransport> transport
)
    : deployment_url_(deployment_url)
    , ws_url_(deployment_to_ws_url(deployment_url))
    , client_id_("cpp-convex/0.1.0")
    , transport_(std::move(transport))
    , session_id_(generate_uuid())
{
    // Set up transport callbacks
    transport_->set_callbacks({
        .on_open = [this]() { handle_open(); },
        .on_message = [this](const std::string& msg) { handle_message(msg); },
        .on_close = [this](const std::string& reason) { handle_close(reason); },
        .on_error = [this](const std::string& error) { handle_error(error); }
    });
}

ConvexClient::~ConvexClient() {
    stop();
    disconnect();
}

std::string ConvexClient::deployment_to_ws_url(const std::string& deployment_url) {
    std::string url = deployment_url;
    
    // Remove trailing slash
    while (!url.empty() && url.back() == '/') {
        url.pop_back();
    }
    
    // Convert https:// to wss://
    if (url.find("https://") == 0) {
        url = "wss://" + url.substr(8);
    } else if (url.find("http://") == 0) {
        url = "ws://" + url.substr(7);
    } else if (url.find("wss://") != 0 && url.find("ws://") != 0) {
        url = "wss://" + url;
    }
    
    // Append /sync endpoint
    url += "/sync";
    
    return url;
}

void ConvexClient::connect() {
    transport_->connect(ws_url_, client_id_);
}

void ConvexClient::disconnect() {
    should_reconnect_ = false;
    transport_->close();
}

bool ConvexClient::is_connected() const {
    return transport_->is_connected();
}

void ConvexClient::on_connection_change(std::function<void(ConnectionState)> callback) {
    on_connection_change_ = std::move(callback);
}

void ConvexClient::handle_open() {
    backoff_.reset();
    
    // Send Connect message
    auto msg = base_client_.connect_message(
        session_id_,
        connection_count_,
        last_close_reason_
    );
    send_message(msg);
    
    // Re-subscribe to queries and re-authenticate
    auto reconnect_msgs = base_client_.reconnect_messages();
    for (const auto& m : reconnect_msgs) {
        send_message(m);
    }
    
    connection_count_++;
    
    if (on_connection_change_) {
        on_connection_change_(ConnectionState::Connected);
    }
}

void ConvexClient::handle_message(const std::string& msg) {
    try {
        auto server_msg = parse_server_message(msg);
        base_client_.handle_message(server_msg);
    } catch (const std::exception& e) {
        // Log error but don't disconnect
    }
}

void ConvexClient::handle_close(const std::string& reason) {
    last_close_reason_ = reason.empty() ? "Unknown" : reason;
    
    if (on_connection_change_) {
        on_connection_change_(ConnectionState::Disconnected);
    }
    
    if (should_reconnect_) {
        attempt_reconnect();
    }
}

void ConvexClient::handle_error(const std::string& error) {
    base_client_.handle_failure();
    
    if (should_reconnect_) {
        attempt_reconnect();
    }
}

void ConvexClient::attempt_reconnect() {
    if (!running_) return;
    
    auto delay = backoff_.next();
    
    // Sleep for backoff duration
    std::this_thread::sleep_for(delay);
    
    if (running_ && should_reconnect_) {
        if (on_connection_change_) {
            on_connection_change_(ConnectionState::Connecting);
        }
        connect();
    }
}

void ConvexClient::send_message(const ClientMessage& msg) {
    if (transport_->is_connected()) {
        std::string json = serialize_client_message(msg);
        transport_->send(json);
    } else {
        // Queue for when we reconnect
        std::lock_guard<std::mutex> lock(queue_mutex_);
        outgoing_queue_.push(msg);
    }
}

Subscription ConvexClient::subscribe(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_update
) {
    auto [sub_id, msg] = base_client_.subscribe(
        udf_path,
        args,
        [on_update](const SubscriberId&, const FunctionResult& result) {
            if (on_update) {
                on_update(result);
            }
        }
    );
    
    if (msg) {
        send_message(*msg);
    }
    
    return Subscription(this, sub_id);
}

void ConvexClient::unsubscribe(const SubscriberId& id) {
    auto msg = base_client_.unsubscribe(id);
    if (msg) {
        send_message(*msg);
    }
}

FunctionResult ConvexClient::query(
    const std::string& udf_path,
    const Object& args
) {
    std::optional<FunctionResult> result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    auto sub = subscribe(udf_path, args, [&](const FunctionResult& r) {
        std::lock_guard<std::mutex> lock(mtx);
        result = r;
        done = true;
        cv.notify_one();
    });
    
    // Wait for result
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return *result;
}

void ConvexClient::mutation_async(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_complete
) {
    auto msg = base_client_.mutation(
        udf_path,
        args,
        [on_complete](SessionRequestSeqNumber, const FunctionResult& result) {
            if (on_complete) {
                on_complete(result);
            }
        }
    );
    send_message(msg);
}

FunctionResult ConvexClient::mutation(
    const std::string& udf_path,
    const Object& args
) {
    std::optional<FunctionResult> result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    mutation_async(udf_path, args, [&](const FunctionResult& r) {
        std::lock_guard<std::mutex> lock(mtx);
        result = r;
        done = true;
        cv.notify_one();
    });
    
    // Wait for result
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return *result;
}

void ConvexClient::action_async(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_complete
) {
    auto msg = base_client_.action(
        udf_path,
        args,
        [on_complete](SessionRequestSeqNumber, const FunctionResult& result) {
            if (on_complete) {
                on_complete(result);
            }
        }
    );
    send_message(msg);
}

FunctionResult ConvexClient::action(
    const std::string& udf_path,
    const Object& args
) {
    std::optional<FunctionResult> result;
    std::mutex mtx;
    std::condition_variable cv;
    bool done = false;
    
    action_async(udf_path, args, [&](const FunctionResult& r) {
        std::lock_guard<std::mutex> lock(mtx);
        result = r;
        done = true;
        cv.notify_one();
    });
    
    // Wait for result
    std::unique_lock<std::mutex> lock(mtx);
    cv.wait(lock, [&done]() { return done; });
    
    return *result;
}

void ConvexClient::set_auth(const std::string& token) {
    auto msg = base_client_.authenticate(AuthenticationToken::user(token));
    send_message(msg);
}

void ConvexClient::clear_auth() {
    auto msg = base_client_.clear_auth();
    send_message(msg);
}

void ConvexClient::set_auth_fetcher(std::function<std::string(bool)> fetcher) {
    base_client_.set_auth_fetcher([fetcher](bool force) {
        return AuthenticationToken::user(fetcher(force));
    });
}

void ConvexClient::poll() {
    transport_->poll(0);
    
    // Send any queued messages
    if (transport_->is_connected()) {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        while (!outgoing_queue_.empty()) {
            std::string json = serialize_client_message(outgoing_queue_.front());
            transport_->send(json);
            outgoing_queue_.pop();
        }
    }
}

void ConvexClient::run() {
    running_ = true;
    should_reconnect_ = true;
    
    if (on_connection_change_) {
        on_connection_change_(ConnectionState::Connecting);
    }
    
    connect();
    
    while (running_) {
        transport_->poll(100);  // 100ms poll timeout
        
        // Send any queued messages
        if (transport_->is_connected()) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            while (!outgoing_queue_.empty()) {
                std::string json = serialize_client_message(outgoing_queue_.front());
                transport_->send(json);
                outgoing_queue_.pop();
            }
        }
    }
}

void ConvexClient::stop() {
    running_ = false;
    should_reconnect_ = false;
}

} // namespace convex
