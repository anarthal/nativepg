#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

cmake_minimum_required(VERSION 3.10..4.3)
project(nativepg LANGUAGES CXX)

option(NATIVEPG_COROSIO_API "Whether to build the Corosio API" OFF)

find_package(boost_headers REQUIRED)
find_package(OpenSSL REQUIRED)

if (NATIVEPG_COROSIO_API)
    find_package(boost_corosio REQUIRED)
endif()

# Library
add_library(nativepg
    # Internal functions
    src/nativepg_internal/base64.cpp
    src/nativepg_internal/openssl_error.cpp

    # External API
    src/error.cpp
    src/messages.cpp
    src/startup_fsm.cpp
    src/scram_sha256_fsm.cpp
    src/read_response_fsm.cpp
    src/exec_fsm.cpp
    src/connect_fsm.cpp
    src/request.cpp
    src/response.cpp
    src/sqlstate.cpp
)
target_link_libraries(nativepg PUBLIC Boost::headers OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(nativepg PUBLIC include)
target_include_directories(nativepg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Corosio API
if (NATIVEPG_COROSIO_API)
    add_library(nativepg_corosio
        src/co_connection.cpp
        src/co_connection_pool.cpp
        src/co_multiplexed_connection.cpp
    )
    target_link_libraries(nativepg_corosio PUBLIC
        nativepg
        Boost::corosio
    )
    target_include_directories(nativepg_corosio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# Tests
include(CTest)
if (BUILD_TESTING)
    add_subdirectory(test)
    add_subdirectory(example) # Build the examples to prevent code rotting
endif()
