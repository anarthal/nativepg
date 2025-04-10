# nativepg: a native Postgres protocol library for C++

This library aims to be the equivalent of Boost.MySQL for the PostgreSQL database.

Scope:

- Serialization functions to serialize and parse protocol messages.
- Sans-IO network algorithms, in the form of state machine classes, to implement the
  distinct protocol functionalities.
- Serialization functions to serialize and parse the distinct data types
  that can appear in a row.
- A connection class that uses the the points above to expose a simple Asio interface for postgres
  (akin to `mysql::any_connection`).
- Functions to securely generate queries dynamically (akin to `mysql::format_sql`).
- A connection pool class (akin to `mysql::connection_pool`).

This is still a work in progress.

Any feedback is welcome. Please feel free to open an issue with your comments.
