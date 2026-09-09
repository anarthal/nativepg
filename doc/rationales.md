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
`co_multiplexed_connection::exec()` calls), only the first one will be sent to
the server. The rest may be held until the first one is acknowledged.
The library tries to batch writes as much as possible,
neglecting any possible benefit that Nagle's algorithm may bring.

Note that both libpq and the Postgres server disable Nagle's algorithm
unconditionally, too.
