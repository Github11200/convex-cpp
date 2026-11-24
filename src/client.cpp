#include "convex/client.hpp"
#include "convex/protocol.hpp"
#include "client_worker.hpp"
#include <random>
#include <sstream>
#include <iomanip>
#include <chrono>

namespace convex {

using detail::ClientWorker;

// ============================================================================
// Subscription Implementation
// ============================================================================

QuerySetSubscription::QuerySetSubscription(const std::shared_ptr<detail::QueryBroadcast>& broadcast)
    : broadcast_(broadcast) {
    std::lock_guard<std::mutex> lock(broadcast->mutex);
    sequence_ = broadcast->sequence;
}

std::optional<QueryResults> QuerySetSubscription::next() {
    auto broadcast = broadcast_.lock();
    if (!broadcast) return std::nullopt;
    std::lock_guard<std::mutex> lock(broadcast->mutex);
    if (sequence_ == broadcast->sequence) return std::nullopt;
    sequence_ = broadcast->sequence;
    return broadcast->latest;
}

Subscription::Subscription(std::weak_ptr<ClientWorker> worker, SubscriberId id,
                           QuerySetSubscription watch, std::optional<FunctionResult> initial)
    : worker_(std::move(worker)), id_(id), watch_(std::move(watch)), initial_(std::move(initial)) {}

Subscription::Subscription(Subscription&& other) noexcept = default;

Subscription& Subscription::operator=(Subscription&& other) noexcept {
    if (this != &other) {
        unsubscribe();
        worker_ = std::move(other.worker_);
        id_ = other.id_;
        watch_ = std::move(other.watch_);
        initial_ = std::move(other.initial_);
    }
    return *this;
}

Subscription::~Subscription() {
    unsubscribe();
}

std::optional<FunctionResult> Subscription::current() const {
    if (auto worker = worker_.lock()) return worker->current(id_);
    return std::nullopt;
}

std::optional<FunctionResult> Subscription::next() {
    if (!is_active()) return std::nullopt;
    if (initial_) return std::exchange(initial_, std::nullopt);
    auto snapshot = watch_.next();
    return snapshot ? snapshot->get(id_) : std::nullopt;
}

void Subscription::unsubscribe() {
    auto worker = worker_.lock();
    worker_.reset();
    if (worker) worker->unsubscribe(id_);
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
) : ConvexClient(deployment_url, std::move(transport), "cpp-convex/0.1.0", {}) {}

ConvexClient::ConvexClient(const std::string& deployment_url, std::unique_ptr<ITransport> transport,
                           std::string client_id, std::function<void(ConnectionState)> state_change)
    : worker_(std::make_shared<ClientWorker>(deployment_url, std::move(transport),
                                            std::move(client_id), std::move(state_change))) {}

ClientWorker::ClientWorker(const std::string& deployment_url, std::unique_ptr<ITransport> transport,
                           std::string client_id, StateCallback state_change)
    : ws_url_(deployment_to_ws_url(deployment_url))
    , client_id_(std::move(client_id))
    , transport_(std::move(transport))
    , session_id_(generate_uuid())
    , on_connection_change_(std::move(state_change))
{
    if (!transport_) throw std::invalid_argument("Transport must not be null");
    // Set up transport callbacks
    transport_->set_callbacks({
        .on_open = [this]() { enqueue_event(EventType::Open); },
        .on_message = [this](const std::string& msg) { enqueue_event(EventType::Message, msg); },
        .on_close = [this](const std::string& reason) { enqueue_event(EventType::Close, reason); },
        .on_error = [this](const std::string& error) { enqueue_event(EventType::Error, error); }
    });
}

ClientWorker::~ClientWorker() {
    broadcast_.reset();
    stop();
    disconnect();
}

std::string ClientWorker::deployment_to_ws_url(const std::string& deployment_url) {
    auto separator = deployment_url.find("://");
    if (separator == std::string::npos) throw std::invalid_argument("Deployment URL must include a scheme");
    auto scheme = deployment_url.substr(0, separator);
    std::string ws_scheme;
    if (scheme == "http" || scheme == "ws") ws_scheme = "ws";
    else if (scheme == "https" || scheme == "wss") ws_scheme = "wss";
    else throw std::invalid_argument("Unknown scheme " + scheme + ". Expected http or https.");
    auto authority_start = separator + 3;
    auto authority_end = deployment_url.find_first_of("/?#", authority_start);
    auto authority = deployment_url.substr(authority_start, authority_end - authority_start);
    if (authority.empty()) throw std::invalid_argument("Deployment URL must include a host");
    auto suffix = deployment_url.find_first_of("?#", authority_start);
    return ws_scheme + "://" + authority + "/api/sync" +
           (suffix == std::string::npos ? "" : deployment_url.substr(suffix));
}

void ClientWorker::connect() {
    if (transport_->state() == ConnectionState::Connected ||
        transport_->state() == ConnectionState::Connecting) return;
    should_reconnect_ = true;
    if (on_connection_change_) on_connection_change_(ConnectionState::Connecting);
    auto started = transport_->connect(ws_url_, client_id_);
    drain_transport_events();
    if (!started) attempt_reconnect();
}

void ClientWorker::disconnect() {
    should_reconnect_ = false;
    reconnect_at_.reset();
    transport_->close();
    std::lock_guard<std::mutex> lock(event_mutex_);
    transport_events_ = {};
}

bool ClientWorker::is_connected() const {
    return transport_->is_connected();
}

void ClientWorker::on_connection_change(StateCallback callback) {
    on_connection_change_ = std::move(callback);
}

void ClientWorker::handle_open() {
    backoff_.reset();
    reconnect_at_.reset();
    auto msg = base_client_.connect_message(
        session_id_,
        connection_count_,
        last_close_reason_
    );
    if (!transport_->send(serialize_client_message(msg))) {
        handle_error("Failed to send Connect");
        return;
    }
    if (connection_count_ != 0) base_client_.reconnect_messages();
    
    connection_count_++;
    
    if (on_connection_change_) {
        on_connection_change_(ConnectionState::Connected);
    }
}

void ClientWorker::handle_message(const std::string& msg) {
    try {
        auto server_msg = parse_server_message(msg);
        auto snapshot = base_client_.handle_message(server_msg);
        if (snapshot) {
            std::lock_guard<std::mutex> lock(broadcast_->mutex);
            broadcast_->latest = std::move(*snapshot);
            ++broadcast_->sequence;
        }
    } catch (const std::exception& e) {
        handle_error(e.what());
    }
}

void ClientWorker::handle_close(const std::string& reason) {
    if (on_connection_change_) {
        on_connection_change_(ConnectionState::Disconnected);
    }
    
    handle_error(reason);
}

void ClientWorker::handle_error(const std::string& error) {
    last_close_reason_ = error.empty() ? "Unknown" : error;
    attempt_reconnect();
}

void ClientWorker::attempt_reconnect() {
    if (should_reconnect_ && !reconnect_at_) {
        reconnect_at_ = std::chrono::steady_clock::now() + backoff_.next();
    }
}

void ClientWorker::enqueue_event(EventType type, std::string data) {
    std::lock_guard<std::mutex> lock(event_mutex_);
    transport_events_.emplace(type, std::move(data));
}

void ClientWorker::drain_transport_events() {
    std::queue<std::pair<EventType, std::string>> events;
    {
        std::lock_guard<std::mutex> lock(event_mutex_);
        events.swap(transport_events_);
    }
    while (!events.empty()) {
        auto& [type, data] = events.front();
        switch (type) {
            case EventType::Open: handle_open(); break;
            case EventType::Message: handle_message(data); break;
            case EventType::Close: handle_close(data); break;
            case EventType::Error: handle_error(data); break;
        }
        events.pop();
    }
}

Subscription ClientWorker::subscribe(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_update
) {
    auto watch = watch_all();
    auto sub_id = base_client_.subscribe(
        udf_path,
        args,
        [on_update](const SubscriberId&, const FunctionResult& result) {
            if (on_update) {
                on_update(result);
            }
        }
    ).first;
    
    return Subscription(weak_from_this(), sub_id, std::move(watch), current(sub_id));
}

QuerySetSubscription ClientWorker::watch_all() const {
    return QuerySetSubscription(broadcast_);
}

void ClientWorker::unsubscribe(const SubscriberId& id) {
    base_client_.unsubscribe(id);
}

std::optional<FunctionResult> ClientWorker::current(const SubscriberId& id) const {
    return base_client_.get_result(id);
}

FunctionResult ConvexClient::query(
    const std::string& udf_path,
    const Object& args
) {
    Subscription subscription;
    auto worker = worker_;
    return worker->wait_for_result([&](auto complete) {
        subscription = worker->subscribe(udf_path, args, std::move(complete));
    });
}

void ClientWorker::mutation_async(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_complete
) {
    base_client_.mutation(
        udf_path,
        args,
        [on_complete](SessionRequestSeqNumber, const FunctionResult& result) {
            if (on_complete) {
                on_complete(result);
            }
        }
    );
}

FunctionResult ConvexClient::mutation(
    const std::string& udf_path,
    const Object& args
) {
    auto worker = worker_;
    return worker->wait_for_result([&](auto complete) {
        worker->mutation_async(udf_path, args, std::move(complete));
    });
}

void ClientWorker::action_async(
    const std::string& udf_path,
    const Object& args,
    std::function<void(const FunctionResult&)> on_complete
) {
    base_client_.action(
        udf_path,
        args,
        [on_complete](SessionRequestSeqNumber, const FunctionResult& result) {
            if (on_complete) {
                on_complete(result);
            }
        }
    );
}

FunctionResult ConvexClient::action(
    const std::string& udf_path,
    const Object& args
) {
    auto worker = worker_;
    return worker->wait_for_result([&](auto complete) {
        worker->action_async(udf_path, args, std::move(complete));
    });
}

FunctionResult ClientWorker::wait_for_result(
    const std::function<void(std::function<void(const FunctionResult&)>)>& start) {
    struct Waiter {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<FunctionResult> result;
    };
    auto waiter = std::make_shared<Waiter>();
    start([waiter](const FunctionResult& result) {
        std::lock_guard<std::mutex> lock(waiter->mutex);
        if (!waiter->result) waiter->result = result;
        waiter->ready.notify_one();
    });
    if (!running_ && !is_connected()) connect();
    for (;;) {
        std::unique_lock<std::mutex> lock(waiter->mutex);
        if (waiter->result) return std::move(*waiter->result);
        if (running_) {
            waiter->ready.wait_for(lock, std::chrono::milliseconds(100));
        } else {
            lock.unlock();
            process(10);
            if (!is_connected()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
}

void ConvexClient::set_auth(std::optional<std::string> token) {
    if (!token) clear_auth();
    else set_auth_callback([token = std::move(*token)](bool) { return AuthenticationToken::user(token); });
}

void ConvexClient::set_admin_auth(std::string deploy_key, std::optional<Object> acting_as) {
    set_auth_callback([token = AuthenticationToken{AuthenticationToken::Type::Admin,
                        std::move(deploy_key), std::move(acting_as)}](bool) { return token; });
}

void ConvexClient::clear_auth() {
    set_auth_callback({});
}

void ConvexClient::set_auth_fetcher(std::function<std::string(bool)> fetcher) {
    if (!fetcher) {
        set_auth_callback({});
        return;
    }
    set_auth_callback([fetcher = std::move(fetcher)](bool force) {
        return AuthenticationToken::user(fetcher(force));
    });
}

void ClientWorker::set_auth_callback(AuthCallback fetcher) {
    base_client_.set_auth_fetcher(std::move(fetcher));
}

void ClientWorker::poll() {
    process(0);
}

void ClientWorker::process(int timeout_ms) {
    drain_transport_events();
    if (should_reconnect_ && reconnect_at_) {
        if (std::chrono::steady_clock::now() < *reconnect_at_) {
            if (timeout_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return;
        }
        reconnect_at_.reset();
        transport_->close();
        {
            std::lock_guard<std::mutex> lock(event_mutex_);
            transport_events_ = {};
        }
        connect();
    } else if (should_reconnect_ && transport_->state() == ConnectionState::Disconnected) {
        connect();
    }
    while (transport_->is_connected()) {
        auto message = base_client_.pop_next_message();
        if (!message) break;
        if (!transport_->send(serialize_client_message(*message))) {
            handle_error("Failed to send message");
            return;
        }
    }
    transport_->poll(timeout_ms);
    drain_transport_events();
}

void ClientWorker::run() {
    if (running_.exchange(true)) throw std::logic_error("Client already has a running event loop");
    struct Reset {
        std::atomic<bool>& running;
        ~Reset() { running = false; }
    } reset{running_};
    should_reconnect_ = true;
    
    if (!is_connected()) connect();
    
    while (running_) {
        process(100);
        if (!is_connected()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void ClientWorker::stop() {
    running_ = false;
    should_reconnect_ = false;
}

void ConvexClient::connect() { auto worker = worker_; worker->connect(); }
void ConvexClient::disconnect() { auto worker = worker_; worker->disconnect(); }
bool ConvexClient::is_connected() const { return worker_->is_connected(); }
void ConvexClient::on_connection_change(std::function<void(ConnectionState)> callback) {
    worker_->on_connection_change(std::move(callback));
}
Subscription ConvexClient::subscribe(const std::string& path, const Object& args,
                                     std::function<void(const FunctionResult&)> callback) {
    auto worker = worker_;
    return worker->subscribe(path, args, std::move(callback));
}
QuerySetSubscription ConvexClient::watch_all() const { return worker_->watch_all(); }
void ConvexClient::mutation_async(const std::string& path, const Object& args,
                                  std::function<void(const FunctionResult&)> callback) {
    worker_->mutation_async(path, args, std::move(callback));
}
void ConvexClient::action_async(const std::string& path, const Object& args,
                                std::function<void(const FunctionResult&)> callback) {
    worker_->action_async(path, args, std::move(callback));
}
void ConvexClient::set_auth_callback(AuthCallback fetcher) {
    auto worker = worker_;
    worker->set_auth_callback(std::move(fetcher));
}
void ConvexClient::poll() { auto worker = worker_; worker->poll(); }
void ConvexClient::run() { auto worker = worker_; worker->run(); }
void ConvexClient::stop() { worker_->stop(); }

ConvexClient ConvexClientBuilder::build() const {
    return build(create_transport());
}

ConvexClient ConvexClientBuilder::build(std::unique_ptr<ITransport> transport) const {
    return ConvexClient(deployment_url_, std::move(transport), client_id_, state_change_);
}

} // namespace convex
