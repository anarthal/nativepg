#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

# Test utils
add_library(nativepg_test_utils STATIC test_utils/test_utils.cpp)
target_include_directories(nativepg_test_utils PUBLIC test_utils)
target_link_libraries(nativepg_test_utils PUBLIC nativepg)

# Corosio-dependent test utilities
if (NATIVEPG_COROSIO_API)
    add_library(nativepg_test_utils_corosio test_utils/corosio_utils.cpp)
    target_link_libraries(nativepg_test_utils_corosio PUBLIC
        nativepg_corosio
        nativepg_test_utils
    )
endif()

# TODO: rename
function (add_unit_test TEST_DIR TEST_NAME)
    set(TARGET_NAME "nativepg_${TEST_NAME}")
    add_executable(${TARGET_NAME} ${TEST_DIR}/${TEST_NAME}.cpp)
    target_include_directories(${TARGET_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(${TARGET_NAME} PRIVATE nativepg nativepg_test_utils ${ARGN})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    target_compile_options(${TARGET_NAME} PUBLIC -Wall -Wextra -Wpedantic -Werror)
endfunction()

add_unit_test(unit/nativepg_internal test_base64)
add_unit_test(unit/protocol          test_scram_sha256_client_first_message)
add_unit_test(unit/protocol          test_scram_sha256_server_first_message)
add_unit_test(unit/protocol          test_scram_sha256_client_final_message)
add_unit_test(unit/protocol          test_scram_sha256_server_final_message)
add_unit_test(unit/protocol          test_scram_sha256_crypt)
add_unit_test(unit/protocol          test_scram_sha256_fsm)
add_unit_test(unit/protocol          test_parse_message)
add_unit_test(unit/protocol          test_message_missing_bytes)
add_unit_test(unit/protocol          test_startup_fsm)
add_unit_test(unit/protocol          test_read_response_fsm)
add_unit_test(unit/protocol          test_check_request)
add_unit_test(unit/protocol          test_next_power_of_2)
add_unit_test(unit/protocol          test_read_buffer)
add_unit_test(unit/protocol          test_command_complete_tag)
add_unit_test(unit                   test_field_view)
add_unit_test(unit                   test_request)
add_unit_test(unit                   test_response)
add_unit_test(unit                   test_resultset_callback)
add_unit_test(unit                   test_diagnostics)
add_unit_test(unit                   test_sqlstate)
add_unit_test(unit                   test_extended_error_disposition)
add_unit_test(unit                   test_extended_error_boost_system)
add_unit_test(unit/types             test_base)
add_unit_test(unit/types             test_numeric)
add_unit_test(unit/types             test_decimal)
add_unit_test(unit/types             test_datetime)

if (NATIVEPG_COROSIO_API)
    add_unit_test(integration            test_co_connection  nativepg_test_utils_corosio)
endif()
