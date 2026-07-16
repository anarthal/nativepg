#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

cmake_minimum_required(VERSION 3.10..3.31)
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
enable_testing()
add_library(nativepg_test_utils STATIC test/test_utils/test_utils.cpp)
target_include_directories(nativepg_test_utils PUBLIC test/test_utils/)
target_link_libraries(nativepg_test_utils PUBLIC nativepg)

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
    add_unit_test(protocol          test_scram_sha256_crypt)
    add_unit_test(protocol          test_scram_sha256_fsm)
    add_unit_test(protocol          test_parse_message)
    add_unit_test(protocol          test_message_missing_bytes)
    add_unit_test(protocol          test_startup_fsm)
    add_unit_test(protocol          test_read_response_fsm)
    add_unit_test(protocol          test_check_request)
    add_unit_test(protocol          test_next_power_of_2)
    add_unit_test(protocol          test_read_buffer)
    add_unit_test(protocol          test_command_complete_tag)
    add_unit_test(""                test_field_view)
    add_unit_test(""                test_request)
    add_unit_test(""                test_response)
    add_unit_test(""                test_resultset_callback)
    add_unit_test(""                test_diagnostics)
    add_unit_test(""                test_sqlstate)
    add_unit_test(""                test_extended_error_disposition)
    add_unit_test(""                test_extended_error_boost_system)
    add_unit_test(types             test_base)
    add_unit_test(types             test_numeric)
    add_unit_test(types             test_decimal)
    add_unit_test(types             test_datetime)
endif()

# Examples
add_executable(nativepg_example example/nativepg.cpp)
target_link_libraries(nativepg_example PRIVATE nativepg)

add_executable(nativepg_base_example example/types/base.cpp)
target_link_libraries(nativepg_base_example PRIVATE nativepg)

add_executable(nativepg_numeric_example example/types/numeric.cpp)
target_link_libraries(nativepg_numeric_example PRIVATE nativepg)

add_executable(nativepg_decimal_example example/types/decimal.cpp)
target_link_libraries(nativepg_decimal_example PRIVATE nativepg)

add_executable(nativepg_datetime_example example/types/datetime.cpp)
target_link_libraries(nativepg_datetime_example PRIVATE nativepg)

add_executable(nativepg_asio_dml_example example/asio_dml.cpp)
target_link_libraries(nativepg_asio_dml_example PRIVATE nativepg)

if (NATIVEPG_COROSIO_API)
    add_executable(nativepg_co_connection example/co_connection.cpp)
    target_link_libraries(nativepg_co_connection PRIVATE nativepg_corosio)

    add_executable(nativepg_co_connection_pool example/co_connection_pool.cpp)
    target_link_libraries(nativepg_co_connection_pool PRIVATE nativepg_corosio)

    add_executable(nativepg_co_multiplexed_connection example/co_multiplexed_connection.cpp)
    target_link_libraries(nativepg_co_multiplexed_connection PRIVATE nativepg_corosio)

    add_executable(nativepg_prepared_statements example/prepared_statements.cpp)
    target_link_libraries(nativepg_prepared_statements PRIVATE nativepg_corosio)

    add_executable(nativepg_listen example/listen.cpp)
    target_link_libraries(nativepg_listen PRIVATE nativepg_corosio)

    add_executable(nativepg_copy_out example/copy_out.cpp)
    target_link_libraries(nativepg_copy_out PRIVATE nativepg_corosio)

    add_executable(nativepg_co_dml_example example/co_dml.cpp)
    target_link_libraries(nativepg_co_dml_example PRIVATE nativepg_corosio)

    add_executable(nativepg_dynamic_resultset example/dynamic_resultset.cpp)
    target_link_libraries(nativepg_dynamic_resultset PRIVATE nativepg_corosio)

    add_executable(nativepg_execute_max_rows example/execute_max_rows.cpp)
    target_link_libraries(nativepg_execute_max_rows PRIVATE nativepg_corosio)

    add_executable(nativepg_update_affected_rows example/update_affected_rows.cpp)
    target_link_libraries(nativepg_update_affected_rows PRIVATE nativepg_corosio)

    add_executable(nativepg_resultsets example/resultsets.cpp)
    target_link_libraries(nativepg_resultsets PRIVATE nativepg_corosio)
endif()
