# Rust parity and verification

The reference for this port is the local `convex-rs` checkout at `e8083fc`.
Parity is about observable behavior and shared protocol rules, not matching
source line counts between languages.

## Covered by active tests

- `tests/test_integration.cpp` drives the public client through a mock
  transport: shared client copies, builder options, the `/api/sync` endpoint,
  subscriptions and consistent query snapshots, auth, reconnect replay, and
  blocking and callback calls.
- `tests/test_base_client.cpp` checks query-set and identity versions,
  canonical subscription deduplication, cached results, mutation completion
  at a transition, handling invalid responses, and replay across auth changes.
- `tests/test_protocol.cpp` checks Convex message fixtures and value JSON,
  including seeded round trips, float bit patterns, timestamps, malformed
  encodings, null structured error data, and the JSON export format.
- Value, ordering, identifier, path, and mock transport behavior have
  separate unit suites.

## Remaining differences

- The C++ worker is driven with `poll()` or `run()` rather than Tokio. C++
  uses callbacks and blocking calls; subscriptions expose nonblocking
  `next()` instead of Rust async streams. A synchronous C++ auth callback
  has no direct equivalent to dropping a Rust future mid-fetch.
- Rust's test-only export round-trip context and generic sync-message
  serialization properties do not yet have complete C++ counterparts.
  Seeded C++ value tests are not a substitute for every Rust property test.
- Mock tests do not verify live WebSocket, TLS, heartbeat, or hardware
  behavior. The platform transports and their scheduling are not identical
  to Rust's WebSocket manager.

`cargo test --offline --lib` in the reference checkout is blocked by a missing
`sync_types/src/testing.rs` module. The test comparisons here are based on
the checked-in test sources, not a successful run of that Rust suite.
