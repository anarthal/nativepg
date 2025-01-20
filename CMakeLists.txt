#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

cmake_minimum_required(VERSION 3.10..3.31)
project(nativepg)

find_package(boost_headers REQUIRED)
find_package(boost_charconv REQUIRED)
find_package(OpenSSL REQUIRED)


add_library(nativepg
    src/error.cpp
    src/messages.cpp
)
target_link_libraries(nativepg PUBLIC Boost::headers Boost::charconv OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(nativepg PUBLIC include)

add_executable(main test/main.cpp)
target_link_libraries(main PRIVATE nativepg)
