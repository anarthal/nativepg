## Why `field_view::data_str()` if you have `field_view::data()`?

When values use the text format (a common choice), they really are strings of text.
Forcing a `reinterpret_cast` for this common use is not ergonomic.
`data_str()` is more explicit than `data()`. By writing it, the user is
acknowledging that their values are expected as text.

## Why not a single trait for serialization and deserialization?

Because some types make sense for serialization, but not for deserialization.
For example, `std::string_view` and `std::span` are suitable as parameters for
serialization because they are used once during the serialization process
and never used again. They are not suitable for deserialization, though:
the response to a command returning many rows may be split across multiple
network packets, and values from earlier packets would become invalid unless
they are copied into an owning type.

## Why can serialization fail?

There are rare cases where the C++ values can't be represented in the
protocol types, and the errors can't be detected at compile time.
For example, the protocol rejects values longer than `INT32_MAX` (2^31 - 1) bytes.
Such lengths are representable in C++, where `std::size_t` is unsigned.

## Why are types associated with a single OID during serialization, but with many during parsing?

PostgreSQL already performs a level of type coercion. Passing Postgres an `int4` where an `int8` is
required works. Accepting several C++ types for a single Postgres type when parsing
(e.g. `int4` being compatible with both `std::int32_t` and `std::int64_t`)
implements similar coercion rules on the C++ side.

## Why does `serializable_ref` deal with `format_code`, but not with the type's OID?

Because both parameter values and parameter format codes are part of the `bind`
message. The serialized parameter value should use the format code advertised
in `bind`. Making `serializable_ref` know about `format_code` makes it impossible
for them to go out of sync.

On the other hand, parameter type OIDs are specified in `parse` messages.
It is common to send `parse` independently of `bind` - this is the case when
preparing a statement and executing it later. For this reason,
it is not viable to embed the type's OID in `serializable_ref`.

## Why not split deserialization into two functions in the traits - one for text and another for binary?

Simplicity. When parsing, the protocol gives you:

- The type OID of what you are parsing - represented as an `std::int32_t`.
- The format code of what you are parsing - represented as a `format_code`.
- The raw bytes of the value, plus a flag indicating whether the value is `NULL` - represented as a `field_view`.

The current traits structure intends to stay as close to the protocol as possible.
It is true that this creates some duplication, especially regarding `NULL` checks.
We find this acceptable because writing traits is a specialized task - most users will never do it.
The alternative would be splitting types into nullable/non-nullable, with
different signatures. We think the complexity is not worth it.

A similar argument applies to serialization.

## Why does serialization represent NULL as a special error code?

This is the simplest and most robust alternative. Another option would be
changing `serializable_ref` to hold a special value when the contained value
is `NULL`. But this:

- Complicates `serializable_ref` - `function_ref` cannot represent an empty
  state, unlike `std::function`.
- Complicates serialization traits. A new function `bool is_null(const T& value)`
  would be required, with almost all types returning `false`.
- Makes the serialization function for nullable types less robust:

```cpp
// Serialization functions for std::optional<T>
static bool is_null(const std::optional<T>& value) { return !value.has_value(); }
static std::error_code serialize(const std::optional<T>& value, std::vector<unsigned char>& to) {
    // Trust that is_null was called correctly
    return field_serialize<T>(*value, to);
}
```

Recall that `std::error_code` values can represent conditions
that are not necessarily fatal errors - e.g. EOF.

## Why do handlers keep receiving messages after they report an error?

Handlers can report errors to make higher-level async operations fail.
For example, `into()` will make `exec()` fail if you passed a type
incompatible with what the database returned, or if the server returns an error.

These are usage errors, not protocol-level errors. After they happen,
the connection should still be usable. To achieve this, all the messages
related to the pipeline associated with the handler need to be read.

Why not just discard them? Handlers may be part of pipelines.
Some parts of the pipeline may be independent of the others.
A handler error in the first part shouldn't affect the rest of the pipeline.
For example:

```cpp
request req;
req.add_query("SELECT * FROM t1")
   .add_query("SELECT * FROM t2");
response res{into(vec1), into(vec2)};
```

The first query might fail, and the second one succeed.
For this to happen, `response` (which is a handler)
must keep receiving messages, even after reporting the first failure.

## Why don't `any_backend_message` and `any_request_message` use `std::variant`?

I don't like variants :) Now, on to the technical arguments:

- All alternatives in both types are trivially copyable and trivially
  destructible, so a `union`-based implementation is straightforward.
- A `std::variant` instantiation can't be forward-declared, but a custom class can.
- Variants with many alternatives (like `any_backend_message`) increase compile times.

The library is one of the biggest consumers of these types.
I initially coded this using `std::variant` and then switched to the custom class.
My code got simpler after the switch.

I've followed `boost::json::value` conventions for accessor names.

## Why do you disable Nagle's algorithm (TCP_NODELAY)?

In the past, I've had bad experiences with the interaction between Nagle's
algorithm and TCP delayed ACK (as explained
[here](https://brooker.co.za/blog/2024/05/09/nagle.html)) - see
[this issue in Boost.MySQL](https://github.com/boostorg/mysql/issues/181).

This library attempts to minimize latency. With Nagle enabled, in a pipelining
scenario where several concurrent requests are issued (e.g. concurrent
`co_connection::exec()` calls), only the first one will be sent to
the server. The rest may be held until the first one is acknowledged.
The library tries to batch writes as much as possible,
neglecting any possible benefit that Nagle's algorithm may bring.

Note that both libpq and the Postgres server disable Nagle's algorithm
unconditionally, too.

## Why not a connection with managed reconnection, like in Boost.Redis?

Managed reconnection can be built on top of an unmanaged primitive, and the
reverse is not true.

Having control over reconnection is vital for `LISTEN`/`NOTIFY`
patterns. Postgres notifications have at-most-once delivery semantics,
meaning that a reconnection implies losing notifications.
For instance, when implementing a cache, a reconnection needs
to invalidate the cache.

Managed reconnection complicates connection pooling implementation.
As [benchmarks](../bench/README.md#does-opening-more-than-one-multiplexed-connection-help-scale) show,
a single multiplexed connection doesn't scale well for Postgres - you need several. If you're dealing with a cluster,
some of these connections may be alive and some may not.
When the user submits a request, we need to assign it to a live connection,
which implies knowing the connection's state.

For us, the most valuable feature of Boost.Redis is automatic
request pipelining. I chose to support this as a built-in in
`co_connection::exec()`.

## Why does `exec()` support automatic pipelining? Why not an exclusive `exec()`?

[Benchmarks](../bench/README.md#are-multiplexed-connections-worth-it) show that pipelining
can improve performance a lot. Automatic pipelining in `exec()` has
[a cost](../bench/README.md#co_connectionexec-supports-multiplexing-how-much-overhead-does-this-add),
but it is small enough to not implement a dedicated function.

Having built-in pipelining in `exec()` also removes a pitfall for the user:
calling `exec()` twice in parallel now just works, and is faster than doing it
serially. I've seen many users shoot themselves in the foot with exclusive
semantics in Boost.MySQL.

## Why does `exec()` perform its own reads and writes, instead of a `run()` task?

It allows saner semantics in the presence of cancellation.
With this approach, when `exec()` is cancelled, its corresponding
read or write is cancelled too. Leftovers are cleaned up,
and `exec()` completes.

This simple scheme allows zero-copy implementations:
the request needs to be kept alive only until `exec()` completes.
If write operations are owned by `run()`, this is no longer true:
you need to either copy the request, or block cancellations in `exec()`
until the writer completes.

This design gives away the write-coalescing that Boost.Redis does.
In Boost.Redis, pending writes are coalesced into a single, big
write, to save syscalls. [Benchmarks](../bench/README.md#is-write-coalescing-worth-it) don't
show much of a difference, attributing most of the performance gain
to pipelining rather than to coalescing.

## What happens if `exec()` is cancelled while a request is being executed?

Nothing. The connection is left usable, and other requests aren't affected.
`exec()` stores internally anything it left over, including partially-written
requests and partially-read responses. Subsequent `exec()`/`receive()` tasks
access this information and discard these leftovers before proceeding.

The bookkeeping information required to keep the connection running is minimal
because we require that all requests end in either a `Sync` or a `Query`.
This way, we can count `ReadyForQuery` messages to perform error recovery.

## Why is `receive()` a member of `co_connection` rather than a dedicated listener type?

A dedicated listener type was designed in some detail before being abandoned. It had
`add_channels()`/`remove_channels()` mutating a desired set, reconciled by the receive
loop, with `channel_subscribed`/`channel_error` events reporting progress.

There are problems with this:

1. It is a managed reconnection pattern. As discussed
   [above](#why-not-a-connection-with-managed-reconnection-like-in-boostredis),
   we're trying to avoid these as primitives.
2. Most patterns involving `LISTEN`/`NOTIFY` also require issuing arbitrary
   `exec()`s.
3. Error handling is problematic. There is no clean way to communicate to the user
   that subscribing to a channel failed.

To elaborate on the second point, there are two features of Postgres'
notification system that force listeners to issue `exec()`s:

1. Notifications have at-most-once delivery semantics.
   A disconnection means lost notifications. For example, when maintaining
   a cache, the client needs to query the rows of interest _before_
   issuing the [`LISTEN`](https://www.postgresql.org/docs/current/sql-listen.html).
2. Notifications can have a payload, but it is small (8KB max by default).
   When dealing with bigger sizes, you need to use notifications as signals
   to re-query the data of interest.

Back-pressure is also easier to implement, see
[the next section](#why-does-receive-drive-the-io-itself-instead-of-a-background-run-task-filling-a-queue).

## Why does `receive()` drive the I/O itself, instead of a background `run()` task filling a queue?

If `receive()` is implemented in terms of an internal queue, we've created
a producer/consumer pair (`run()` being the producer, and `receive()` the consumer).
To make this production-grade, we need to consider what happens when the
producer is faster than the consumer, and implement a back-pressure strategy.

This is a problem we face in Boost.Redis. We mitigate it by placing an upper bound
on the queue size, and stalling all connection reads when the queue fills.
This works but creates non-obvious traps: calling `exec()` and `receive()`
sequentially (a common pattern here) can deadlock, because the responses to `exec()`
may be queued after many notifications. I didn't want this limitation here.

When `receive()` is the one reading the socket, there is no queue and no explicit back-pressure policy.
If nobody calls either `receive()` or `exec()`, nobody reads, the kernel window closes, and the server
blocks. Back-pressure happens at the TCP level.

Notifications read by `exec()` are queued until someone reads
them with `receive()`. There is no upper limit to the queue size here.
This matches what `libpq` does.

This is not as bad as with a background `run()`, though,
because users can choose whether to call `exec()` or not.
Our recommendation is to create a dedicated connection for each listener
pattern that your application needs to implement. This connection
should use `exec()` only when needed by the listener pattern,
and not for unrelated queries. See the [cache example](../example/listen_cache.cpp).

## Why does `receive()` copy notifications to an output buffer, instead of returning a view?

Because notification payloads are always small (8KB max by default).
Zero-copy strategies pay off when dealing with larger sizes.

Zero-copy would mean that `receive()` would return a view pointing
into the connection's read buffer. It would only remain valid until
the next `exec()` or `receive()` is called, since both need to read.
Additionally, notifications read by `exec()` need to be copied anyway.
I believe that zero-copy semantics for this use case would
be more trouble than they are worth.

`notification_vector` is a specialized container to make copying as cheap
as possible. It has a flat memory layout, and achieves amortized zero allocations
in steady state.
