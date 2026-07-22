#
# Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
#
# Distributed under the Boost Software License, Version 1.0. (See accompanying
# file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
#

# TODO: we should link against the optional Boost libraries in these examples

add_executable(nativepg_example nativepg.cpp)
target_link_libraries(nativepg_example PRIVATE nativepg)

add_executable(nativepg_base_example types/base.cpp)
target_link_libraries(nativepg_base_example PRIVATE nativepg)

add_executable(nativepg_nullable_example types/nullable.cpp)
target_link_libraries(nativepg_nullable_example PRIVATE nativepg)

add_executable(nativepg_numeric_example types/numeric.cpp)
target_link_libraries(nativepg_numeric_example PRIVATE nativepg)

add_executable(nativepg_decimal_example types/decimal.cpp)
target_link_libraries(nativepg_decimal_example PRIVATE nativepg)

add_executable(nativepg_datetime_example types/datetime.cpp)
target_link_libraries(nativepg_datetime_example PRIVATE nativepg)

add_executable(nativepg_json_example types/json.cpp)
target_link_libraries(nativepg_json_example PRIVATE nativepg)

if (NATIVEPG_COROSIO_API)
    add_executable(nativepg_co_connection co_connection.cpp)
    target_link_libraries(nativepg_co_connection PRIVATE nativepg_corosio)

    add_executable(nativepg_co_connection_pool co_connection_pool.cpp)
    target_link_libraries(nativepg_co_connection_pool PRIVATE nativepg_corosio)

    add_executable(nativepg_co_multiplexed_connection co_multiplexed_connection.cpp)
    target_link_libraries(nativepg_co_multiplexed_connection PRIVATE nativepg_corosio)

    add_executable(nativepg_prepared_statements prepared_statements.cpp)
    target_link_libraries(nativepg_prepared_statements PRIVATE nativepg_corosio)

    add_executable(nativepg_listen listen.cpp)
    target_link_libraries(nativepg_listen PRIVATE nativepg_corosio)

    add_executable(nativepg_copy_out copy_out.cpp)
    target_link_libraries(nativepg_copy_out PRIVATE nativepg_corosio)

    add_executable(nativepg_execute_max_rows execute_max_rows.cpp)
    target_link_libraries(nativepg_execute_max_rows PRIVATE nativepg_corosio)

    add_executable(nativepg_update_affected_rows update_affected_rows.cpp)
    target_link_libraries(nativepg_update_affected_rows PRIVATE nativepg_corosio)

    add_executable(nativepg_resultsets resultsets.cpp)
    target_link_libraries(nativepg_resultsets PRIVATE nativepg_corosio)
endif()

