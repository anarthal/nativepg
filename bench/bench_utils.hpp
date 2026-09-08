//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_BENCH_BENCH_UTILS_HPP
#define NATIVEPG_BENCH_BENCH_UTILS_HPP

// Facilities shared by all benchmarks in this directory.

#include <boost/assert/source_location.hpp>

#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <system_error>

namespace nativepg::bench {

// Benchmarks have nothing meaningful to report if any operation fails,
// so bail out as soon as one does.
[[noreturn]] inline void die(
    const char* prefix,
    std::error_code ec,
    boost::source_location loc = BOOST_CURRENT_LOCATION
)
{
    std::cerr << prefix << ": " << ec << ": " << ec.message() << "\n"
              << "  Called from " << loc << std::endl;
    std::exit(1);
}

// Latency accumulator, in microseconds. Samples are folded in as they are
// produced (Welford's online algorithm), so we never store them.
class stats
{
    std::size_t count_{};
    double mean_{};
    double m2_{};  // sum of squared deviations from the running mean

public:
    void add(double sample)
    {
        ++count_;
        const double delta = sample - mean_;
        mean_ += delta / static_cast<double>(count_);
        m2_ += delta * (sample - mean_);
    }

    std::size_t count() const { return count_; }
    double mean() const { return mean_; }

    // Sample standard deviation
    double stddev() const { return count_ < 2u ? 0.0 : std::sqrt(m2_ / static_cast<double>(count_ - 1u)); }
};

// Reports the figures of a single case. elapsed_secs is the wall time of the
// run, and latency holds one sample per query, as an individual session saw it.
inline void print_results(
    const char* name,
    std::size_t nconns,
    std::size_t nsessions,
    std::size_t nqueries,
    double elapsed_secs,
    const stats& latency
)
{
    const auto total_queries = static_cast<double>(latency.count());

    std::cout << std::fixed << std::setprecision(2)                                        //
              << "\n=== " << name << " ===\n"                                              //
              << "Connections:         " << nconns << '\n'                                 //
              << "Sessions:            " << nsessions << '\n'                              //
              << "Queries per session: " << nqueries << '\n'                               //
              << "Queries run:         " << latency.count() << '\n'                        //
              << "\nThroughput\n"                                                          //
              << "  total time:        " << elapsed_secs * 1e3 << " ms\n"                  //
              << "  time per query:    " << elapsed_secs * 1e6 / total_queries << " us\n"  //
              << "  queries/second:    " << total_queries / elapsed_secs << '\n'           //
              << "\nQuery latency (us)\n"                                                  //
              << "  mean:              " << latency.mean() << '\n'                         //
              << "  stddev:            " << latency.stddev() << '\n';
}

}  // namespace nativepg::bench

#endif
