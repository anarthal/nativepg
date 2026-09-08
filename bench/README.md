# Benchmarks

This directory contains the library's benchmarks.
This is a work in progress.

Frequently, I've found benchmarks that output a bunch of numbers
that look cool but end up translating into little useful information.
For nativepg, I'm trying an **A/B approach**. Every benchmark
should try to answer a particular question. It should contain
two cases, and potentially reach to a conclusion.

The conclusions here have been extracted by running the benchmarks
on a i7-10510U 1.80GHz CPU with 4 cores/8 threads, under Ubuntu 24.04
and clang-20 built with CMake 4.2.1 using the Release configuration.
All benchmarks use plaintext TCP.

For the server, I've got two setups:

- One running in localhost, using Docker (`postgres:17.4` image).
  This setup represents use cases where the network latency is small
  (e.g. where both client and server run in the same cloud/machine).
- One running in AWS, on a `t3.micro` server with a Ubuntu 26.04 image.
  It uses the system's Postgres installation (v18.6).
  This setup represents use cases where network latency is large.
  A full network round-trip here takes around 100ms.

## Are multiplexed connections worth it?

Tries to answer the question: are multiplexed connections faster
than dedicated connections?

Source: [`multiplexed_vs_dedicated.cpp`](multiplexed_vs_dedicated.cpp).

The workload is composed of simple SELECT queries (suitable for being
multiplexed), issued by a number of independent sessions running in
parallel. Both cases are given a single connection, shared by all
sessions:

- **Multiplexed**: sessions share a single `co_multiplexed_connection`.
- **Dedicated**: sessions share a single `co_connection`. Access is
  arbitrated using a `capy::async_mutex`.

Doing this measures connection utilization (i.e. given a fixed number
of connections, which case uses them more effectively?). This is
important because Postgres is one process per connection, so the max
number of connections is limited (usually `max_connections=100`).
Intuitively, multiplexed connections should be faster because they are
full-duplex, while dedicated connections are half-duplex.

We measure latency, as seen by an individual session, and throughput,
as queries completed per unit of time.

**Conclusions**: multiplexed is faster. The bigger the network latency,
the more significant the improvements are.

- For the localhost server, the multiplexed connection is around 20% faster.
  Wireshark reveals almost no coalescing of write packets.
  Speed likely comes from being full-duplex, rather than write packet coalescing.
- For the AWS server, the multiplexed version is x80 times faster.
  We can see much more write packet coalescing here.

Future lines:

- We need to open more than one multiplexed connection to scale effectively,
  especially if the network latency is small. Boost.Redis' single
  multiplexed connection per application recommendation
  does not work for us.
- We should measure whether coalescing writes is really worth the complexity.
