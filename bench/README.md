# Benchmarks

This directory contains the library's benchmarks.
This is a work in progress.

Frequently, I've found benchmarks that output a bunch of numbers
that look cool but end up translating into little useful information.
For nativepg, I'm trying an **A/B approach**. Every benchmark
should try to answer a particular question. It should contain
two cases, and potentially reach to a conclusion.

The conclusions here have been extracted by running the benchmarks
on a i7-10510U 1.80GHz CPU with 8 cores, under Ubuntu 24.04
and clang-20 built with CMake 4.2.1 using the Release configuration.

The server runs in localhost, using Docker (`postgres:17.4`).
Ideally, both pieces of code should run in independent machines
to reduce mutual influence, but that's a task for the future.

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

**Conclusions**: multiplexed is faster, but not as faster as I thought
it would be (around 20% faster). Wireshark reveals no write multiplexing
most of the time, probably due to the speculative write architecture
that Corosio implements. Speed likely comes from being full-duplex,
rather than write multiplexing.

- We need to open more than one multiplexed connection to scale effectively.
- We may be able to drop write multiplexing altogether.
