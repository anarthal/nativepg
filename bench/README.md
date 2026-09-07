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

TBC
