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

# Library
add_library(nativepg
    # Internal functions
    src/nativepg_internal/base64.cpp

    # External API
    src/error.cpp
    src/messages.cpp
)
target_link_libraries(nativepg PUBLIC Boost::headers Boost::charconv OpenSSL::SSL OpenSSL::Crypto)
target_include_directories(nativepg PUBLIC include)
target_include_directories(nativepg PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Tests
add_library(nativepg_test_utils STATIC test/test_utils/test_utils.cpp)
target_include_directories(nativepg_test_utils PUBLIC test/test_utils/)
target_link_libraries(nativepg_test_utils PUBLIC Boost::headers)

function (add_unit_test TEST_DIR TEST_NAME)
    set(TARGET_NAME "nativepg_${TEST_NAME}")
    add_executable(${TARGET_NAME} test/unit/${TEST_DIR}/${TEST_NAME}.cpp)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${TARGET_NAME} PRIVATE nativepg nativepg_test_utils)
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    target_compile_options(${TARGET_NAME} PUBLIC -Wall -Wextra -Wpedantic -Werror)
endfunction()

enable_testing()
if (BUILD_TESTING)
    add_unit_test(nativepg_internal test_base64)
    add_unit_test(protocol          test_scram_sha256_client_first_message)
    add_unit_test(protocol          test_scram_sha256_server_first_message)
    add_unit_test(protocol          test_scram_sha256_client_final_message)
    add_unit_test(protocol          test_scram_sha256_server_final_message)
endif()
