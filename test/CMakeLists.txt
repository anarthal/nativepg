#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

# Test utils
add_library(nativepg_test_utils STATIC test_utils/src/common_utils.cpp)
target_include_directories(nativepg_test_utils PUBLIC 
    test_utils/include
    ${CMAKE_SOURCE_DIR}/src # access private utilities
)
target_link_libraries(nativepg_test_utils PUBLIC nativepg)

# Corosio-dependent test utilities
if (NATIVEPG_COROSIO_API)
    add_library(nativepg_test_utils_corosio test_utils/src/corosio_utils.cpp)
    target_link_libraries(nativepg_test_utils_corosio PUBLIC
        nativepg_corosio
        nativepg_test_utils
    )
endif()

# Builds and runs a test. Pass libraries to link to as additional arguments
function (nativepg_add_test TEST_DIR TEST_NAME)
    set(TARGET_NAME "nativepg_${TEST_NAME}")
    add_executable(${TARGET_NAME} ${TEST_DIR}/${TEST_NAME}.cpp)
    target_link_libraries(${TARGET_NAME} PRIVATE nativepg_test_utils ${ARGN})
    add_test(NAME ${TARGET_NAME} COMMAND ${TARGET_NAME})
    target_compile_options(${TARGET_NAME} PUBLIC -Wall -Wextra -Wpedantic -Werror)
endfunction()

nativepg_add_test(unit/nativepg_internal test_base64)
nativepg_add_test(unit/protocol          test_scram_sha256_client_first_message)
nativepg_add_test(unit/protocol          test_scram_sha256_server_first_message)
nativepg_add_test(unit/protocol          test_scram_sha256_client_final_message)
nativepg_add_test(unit/protocol          test_scram_sha256_server_final_message)
nativepg_add_test(unit/protocol          test_scram_sha256_crypt)
nativepg_add_test(unit/protocol          test_scram_sha256_fsm)
nativepg_add_test(unit/protocol          test_parse_message)
nativepg_add_test(unit/protocol          test_message_missing_bytes)
nativepg_add_test(unit/protocol          test_startup_fsm)
nativepg_add_test(unit/protocol          test_read_response_fsm)
nativepg_add_test(unit/protocol          test_check_request)
nativepg_add_test(unit/protocol          test_next_power_of_2)
nativepg_add_test(unit/protocol          test_read_buffer)
nativepg_add_test(unit/protocol          test_command_complete_tag)
nativepg_add_test(unit                   test_field_view)
nativepg_add_test(unit                   test_request)
nativepg_add_test(unit                   test_response)
nativepg_add_test(unit                   test_resultset_callback)
nativepg_add_test(unit                   test_diagnostics)
nativepg_add_test(unit                   test_sqlstate)
nativepg_add_test(unit                   test_extended_error_disposition)
nativepg_add_test(unit                   test_extended_error_boost_system)
nativepg_add_test(unit/types             test_base)
nativepg_add_test(unit/types             test_numeric)
nativepg_add_test(unit/types             test_decimal)
nativepg_add_test(unit/types             test_datetime)
nativepg_add_test(unit/types             test_json)

if (NATIVEPG_COROSIO_API)
    nativepg_add_test(integration            test_co_connection  nativepg_test_utils_corosio)
endif()
