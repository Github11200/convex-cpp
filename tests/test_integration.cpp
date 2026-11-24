#include "doctest.h"
#include "convex/client.hpp"
#include "convex/testing/mock_transport.hpp"
#include "nlohmann_json.hpp"

using namespace convex;
using convex::testing::MockTransport;
using json = nlohmann::json;

namespace {

struct Fixture {
    MockTransport* mock = nullptr;
    ConvexClient client;

    explicit Fixture(const std::string& url = "https://test.convex.cloud")
        : client(url, make_mock()) {}

    std::unique_ptr<MockTransport> make_mock() {
        auto transport = std::make_unique<MockTransport>();
        mock = transport.get();
        return transport;
    }

    json sent() {
        auto frame = mock->pop_sent_message();
        REQUIRE(frame.has_value());
        return json::parse(*frame);
    }

    void transition(int from, int to, json updates) {
        mock->inject_server_message(json{
            {"type", "Transition"},
            {"startVersion", {{"querySet", from}, {"identity", 0}, {"ts", from}}},
            {"endVersion", {{"querySet", to}, {"identity", 0}, {"ts", to}}},
            {"modifications", std::move(updates)}
        }.dump());
    }
};

json updated(int id, const char* value) {
    return {{"type", "QueryUpdated"}, {"queryId", id}, {"value", value}};
}

}

TEST_CASE("Client deployment URL matches the Rust sync endpoint") {
    for (auto scheme : {"http", "https", "ws", "wss"}) {
        Fixture fixture(std::string(scheme) + "://example.test:8080/old/path?test=1");
        fixture.client.connect();
        auto expected = (std::string_view(scheme) == "http" || std::string_view(scheme) == "ws")
            ? "ws://example.test:8080/api/sync?test=1" : "wss://example.test:8080/api/sync?test=1";
        CHECK(fixture.mock->connected_url() == expected);
        auto connect = fixture.sent();
        CHECK(connect["type"] == "Connect");
        CHECK(connect["connectionCount"] == 0);
        CHECK(connect["sessionId"].get<std::string>().size() == 36);
    }
    CHECK_THROWS_AS(Fixture("ftp://example.test"), std::invalid_argument);
    CHECK_THROWS_AS(Fixture("https:///path"), std::invalid_argument);
}

TEST_CASE("Client publishes a consistent view of separate and shared queries") {
    Fixture fixture;
    fixture.client.connect();
    fixture.sent();
    auto watch = fixture.client.watch_all();
    auto alpha = fixture.client.subscribe("messages:list", {});
    auto shared = fixture.client.subscribe("messages:list", {});
    auto beta = fixture.client.subscribe("messages:list", {{"other", Value(true)}});
    CHECK(alpha.id().query_id == shared.id().query_id);
    CHECK(alpha.id() != shared.id());
    CHECK(alpha.id().query_id != beta.id().query_id);
    CHECK_FALSE(watch.next());
    CHECK_FALSE(alpha.next());

    fixture.transition(0, 2, {updated(alpha.id().query_id, "a"), updated(beta.id().query_id, "b")});
    fixture.client.poll();
    CHECK(fixture.sent()["modifications"][0]["type"] == "Add");
    CHECK(fixture.sent()["baseVersion"] == 1);
    CHECK(fixture.mock->sent_message_count() == 0);
    auto snapshot = watch.next();
    REQUIRE(snapshot);
    CHECK(snapshot->size() == 3);
    CHECK(snapshot->get(alpha.id())->value.as_string() == "a");
    CHECK(snapshot->get(shared.id())->value.as_string() == "a");
    CHECK(snapshot->get(beta.id())->value.as_string() == "b");
    CHECK(alpha.next()->value.as_string() == "a");
    CHECK_FALSE(alpha.next());

    auto late = fixture.client.subscribe("messages:list", {});
    CHECK(late.next()->value.as_string() == "a");
    auto old_id = alpha.id().query_id;
    alpha.unsubscribe();
    late.unsubscribe();
    fixture.client.poll();
    CHECK(fixture.mock->sent_message_count() == 0);
    shared.unsubscribe();
    fixture.client.poll();
    CHECK(fixture.sent()["modifications"][0]["type"] == "Remove");

    auto reopened = fixture.client.subscribe("messages:list", {});
    CHECK(reopened.id().query_id != old_id);
    CHECK_FALSE(reopened.current());
    fixture.transition(2, 4, {updated(old_id, "stale"), updated(reopened.id().query_id, "new")});
    fixture.client.poll();
    CHECK(reopened.next()->value.as_string() == "new");
    CHECK(snapshot->size() == 3); // Old snapshots are immutable.
    auto latest = watch.next();
    REQUIRE(latest);
    CHECK(latest->size() == 2);
}

TEST_CASE("Client mutation waits for a transition; actions and plain errors do not") {
    Fixture fixture;
    fixture.client.connect();
    fixture.sent();
    std::optional<FunctionResult> mutation, action, error;
    fixture.client.mutation_async("messages:send", {}, [&](const auto& result) { mutation = result; });
    fixture.client.action_async("messages:notify", {}, [&](const auto& result) { action = result; });
    fixture.client.mutation_async("messages:fail", {}, [&](const auto& result) { error = result; });
    fixture.mock->inject_mutation_response(0, Value("committed"));
    fixture.mock->inject_action_response(1, Value("sent"));
    fixture.mock->inject_server_message(
        R"({"type":"MutationResponse","requestId":2,"success":false,"result":"failed"})");
    fixture.client.poll();
    CHECK(fixture.sent()["type"] == "Mutation");
    CHECK(fixture.sent()["type"] == "Action");
    CHECK(fixture.sent()["requestId"] == 2);
    CHECK_FALSE(mutation);
    REQUIRE(action);
    CHECK(action->value.as_string() == "sent");
    REQUIRE(error);
    CHECK(error->error_message == "failed");
    fixture.transition(0, 1, json::array());
    fixture.client.poll();
    REQUIRE(mutation);
    CHECK(mutation->value.as_string() == "committed");
}

TEST_CASE("Client auth API sends user, admin impersonation and sign-out messages") {
    Fixture fixture;
    fixture.client.connect();
    fixture.sent();
    fixture.client.set_auth("jwt");
    fixture.client.set_admin_auth("deploy-key", Object{{"subject", Value("test-user")}});
    fixture.client.set_auth_callback([](bool refresh) {
        CHECK_FALSE(refresh);
        return AuthenticationToken::none();
    });
    fixture.client.set_auth(std::nullopt);
    fixture.client.poll();
    auto user = fixture.sent();
    CHECK(user["tokenType"] == "User");
    CHECK(user["value"] == "jwt");
    CHECK(user["baseVersion"] == 0);
    auto admin = fixture.sent();
    CHECK(admin["tokenType"] == "Admin");
    CHECK(admin["acting_as"]["subject"] == "test-user");
    CHECK(admin["baseVersion"] == 1);
    CHECK(fixture.sent()["tokenType"] == "None");
    auto signout = fixture.sent();
    CHECK(signout["tokenType"] == "None");
    CHECK(signout["baseVersion"] == 3);
}

TEST_CASE("Client rebuilds versioned state and replays requests after disconnect") {
    Fixture fixture;
    fixture.client.connect();
    auto connect = fixture.sent();
    auto alpha = fixture.client.subscribe("messages:list", {});
    auto beta = fixture.client.subscribe("messages:other", {});
    fixture.client.mutation_async("messages:send", {}, {});
    fixture.client.poll();
    fixture.sent();
    fixture.sent();
    auto mutation = fixture.sent();
    fixture.transition(0, 2, {updated(alpha.id().query_id, "before")});
    fixture.client.poll();
    fixture.mock->simulate_close("network lost");
    beta.unsubscribe();
    auto pending = fixture.client.subscribe("messages:new", {});
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fixture.mock->sent_message_count() < 3 && std::chrono::steady_clock::now() < deadline) {
        fixture.client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    auto reconnected = fixture.sent();
    CHECK(reconnected["sessionId"] == connect["sessionId"]);
    CHECK(reconnected["connectionCount"] == 1);
    CHECK(reconnected["lastCloseReason"] == "network lost");
    CHECK(reconnected["maxObservedTimestamp"] == "AgAAAAAAAAA=");
    auto replay = fixture.sent();
    CHECK(replay["baseVersion"] == 0);
    CHECK(replay["newVersion"] == 1);
    REQUIRE(replay["modifications"].size() == 2);
    CHECK(fixture.sent() == mutation);
    CHECK(fixture.mock->sent_message_count() == 0);
    CHECK(alpha.current()->value.as_string() == "before");
    CHECK_FALSE(pending.current());
}

TEST_CASE("A blocking action drives the client without a separate event loop") {
    Fixture fixture;
    fixture.mock->on_message_sent([&](const std::string& message) {
        if (json::parse(message)["type"] == "Action") {
            fixture.mock->inject_action_response(0, Value("done"));
        }
    });
    CHECK(fixture.client.action("messages:notify", {}).value.as_string() == "done");
}

TEST_CASE("Dropping the client invalidates outstanding subscription handles") {
    Subscription subscription;
    QuerySetSubscription watch;
    {
        Fixture fixture;
        subscription = fixture.client.subscribe("messages:list", {});
        watch = fixture.client.watch_all();
        CHECK(subscription.is_active());
    }
    CHECK_FALSE(subscription.is_active());
    CHECK_FALSE(subscription.current());
    CHECK_FALSE(subscription.next());
    CHECK_FALSE(watch.next());
    CHECK_NOTHROW(subscription.unsubscribe());
}

TEST_CASE("Builder options apply to one worker shared by copied clients") {
    auto transport = std::make_unique<MockTransport>();
    auto* mock = transport.get();
    std::vector<ConnectionState> states;
    auto client = ConvexClientBuilder("https://test.convex.cloud")
        .with_client_id("test-sdk")
        .with_on_state_change([&](auto state) { states.push_back(state); })
        .build(std::move(transport));
    Subscription subscription;
    {
        auto copy = client;
        copy.connect();
        subscription = copy.subscribe("messages:list", {});
    }
    client.connect(); // Does not open a second connection.
    CHECK(mock->connected_client_id() == "test-sdk");
    REQUIRE(states.size() == 2);
    CHECK(states[0] == ConnectionState::Connecting);
    CHECK(states[1] == ConnectionState::Connected);
    CHECK(subscription.is_active());
    client.poll();
    CHECK(mock->sent_message_count() == 2); // Connect and Add
}

TEST_CASE("A query failure is returned as a function error and unsubscribes") {
    Fixture fixture;
    fixture.mock->on_message_sent([&](const std::string& message) {
        auto frame = json::parse(message);
        if (frame["type"] == "ModifyQuerySet" && frame["modifications"][0]["type"] == "Add") {
            fixture.transition(0, 1, {{{"type", "QueryFailed"}, {"queryId", 0}, {"errorMessage", "not found"}}});
        }
    });
    CHECK(fixture.client.query("messages:missing", {}).error_message == "not found");
    fixture.client.poll();
    fixture.sent();
    fixture.sent();
    CHECK(fixture.sent()["modifications"][0]["type"] == "Remove");
}

TEST_CASE("Polling retries a failed initial connection and sends queued work once") {
    Fixture fixture;
    fixture.mock->fail_next_connect("network unavailable");
    auto sub = fixture.client.subscribe("messages:list", {});
    fixture.client.poll();
    CHECK_FALSE(fixture.client.is_connected());
    CHECK(fixture.mock->sent_message_count() == 0);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fixture.mock->sent_message_count() < 2 && std::chrono::steady_clock::now() < deadline) {
        fixture.client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(fixture.sent()["lastCloseReason"] == "network unavailable");
    CHECK(fixture.sent()["modifications"][0]["queryId"] == sub.id().query_id);
    CHECK(fixture.mock->sent_message_count() == 0);
}

TEST_CASE("Protocol errors trigger a reconnect and a forced auth refresh") {
    Fixture fixture;
    std::vector<bool> refreshes;
    fixture.client.set_auth_callback([&](bool force) {
        refreshes.push_back(force);
        return AuthenticationToken::user(force ? "refreshed" : "initial");
    });
    fixture.client.poll();
    fixture.sent();
    CHECK(fixture.sent()["value"] == "initial");
    fixture.mock->inject_server_message(R"({"type":"AuthError","errorMessage":"expired","baseVersion":0})");
    fixture.client.poll();
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (fixture.mock->sent_message_count() < 3 && std::chrono::steady_clock::now() < deadline) {
        fixture.client.poll();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(fixture.sent()["lastCloseReason"] == "AuthError: expired");
    CHECK(fixture.sent()["value"] == "refreshed");
    CHECK(fixture.sent()["newVersion"] == 1);
    REQUIRE(refreshes.size() == 2);
    CHECK_FALSE(refreshes[0]);
    CHECK(refreshes[1]);
}
