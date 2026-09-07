#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

if (NATIVEPG_COROSIO_API)
    add_executable(nativepg_bench_multiplexed_vs_dedicated multiplexed_vs_dedicated.cpp)
    target_link_libraries(nativepg_bench_multiplexed_vs_dedicated PRIVATE nativepg_corosio)
endif()

