//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef NATIVEPG_SQLSTATE_HPP
#define NATIVEPG_SQLSTATE_HPP

#include <boost/system/detail/error_code.hpp>

#include <cstddef>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>

#include "nativepg/client_errc.hpp"

namespace nativepg {

namespace detail {

constexpr int parse_sqlstate_char(char value)
{
    // '0' => 0
    // '1' => 1
    // ...
    // '9' => 9
    // 'A' => 10
    // 'B' => 11
    // ...
    // 'Z' => 35
    // invalid => -1
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'A' && value <= 'Z')
        return value - 'A' + 10;
    return -1;
}

constexpr int parse_sqlstate(std::span<const char, 5> chars)
{
    int c0 = parse_sqlstate_char(chars[0]);
    int c1 = parse_sqlstate_char(chars[1]);
    int c2 = parse_sqlstate_char(chars[2]);
    int c3 = parse_sqlstate_char(chars[3]);
    int c4 = parse_sqlstate_char(chars[4]);
    if (c0 == -1 || c1 == -1 || c2 == -1 || c3 == -1 || c4 == -1)
        return -1;
    return c0 << (6 * 4) | c1 << (6 * 3) | c2 << (6 * 2) | c3 << (6 * 1) | c4;
}

}  // namespace detail

// Runtime parsing
inline std::error_code parse_sqlstate(std::string_view from)
{
    if (from.size() != 5u)
        return boost::system::error_code(client_errc::bad_sqlstate);
    int v = detail::parse_sqlstate(std::span<const char, 5>{from.data(), 5});
    if (v == -1)
        return boost::system::error_code(client_errc::bad_sqlstate);
    return {};  // TODO
}

// Compile-time parsing
consteval int operator""_sqlstate(const char* value, std::size_t len)
{
    if (len != 5u)
        throw std::invalid_argument("Bad SQLSTATE literal");
    int res = detail::parse_sqlstate(std::span<const char, 5>{value, 5});
    if (res == -1)
        throw std::invalid_argument("SQLSTATE literals can only contain numbers and uppercase letters");
    return res;
}

enum class sqlstate_cond
{
    successful_completion,
    warning,
    dynamic_result_sets_returned,
    implicit_zero_bit_padding,
    null_value_eliminated_in_set_function,
    privilege_not_granted,
    privilege_not_revoked,
    string_data_right_truncation,
    deprecated_feature,
    no_data,
    no_additional_dynamic_result_sets_returned,
    sql_statement_not_yet_complete,
    connection_exception,
    connection_does_not_exist,
    connection_failure,
    sqlclient_unable_to_establish_sqlconnection,
    sqlserver_rejected_establishment_of_sqlconnection,
    transaction_resolution_unknown,
    protocol_violation,
    triggered_action_exception,
    feature_not_supported,
    invalid_transaction_initiation,
    locator_exception,
    invalid_locator_specification,
    invalid_grantor,
    invalid_grant_operation,
    invalid_role_specification,
    diagnostics_exception,
    stacked_diagnostics_accessed_without_active_handler,
    invalid_argument_for_xquery,
    case_not_found,
    cardinality_violation,
    data_exception,
    array_subscript_error,
    character_not_in_repertoire,
    datetime_field_overflow,
    division_by_zero,
    error_in_assignment,
    escape_character_conflict,
    indicator_overflow,
    interval_field_overflow,
    invalid_argument_for_logarithm,
    invalid_argument_for_ntile_function,
    invalid_argument_for_nth_value_function,
    invalid_argument_for_power_function,
    invalid_argument_for_width_bucket_function,
    invalid_character_value_for_cast,
    invalid_datetime_format,
    invalid_escape_character,
    invalid_escape_octet,
    invalid_escape_sequence,
    nonstandard_use_of_escape_character,
    invalid_indicator_parameter_value,
    invalid_parameter_value,
    invalid_preceding_or_following_size,
    invalid_regular_expression,
    invalid_row_count_in_limit_clause,
    invalid_row_count_in_result_offset_clause,
    invalid_tablesample_argument,
    invalid_tablesample_repeat,
    invalid_time_zone_displacement_value,
    invalid_use_of_escape_character,
    most_specific_type_mismatch,
    null_value_not_allowed,
    null_value_no_indicator_parameter,
    numeric_value_out_of_range,
    sequence_generator_limit_exceeded,
    string_data_length_mismatch,
    substring_error,
    trim_error,
    unterminated_c_string,
    zero_length_character_string,
    floating_point_exception,
    invalid_text_representation,
    invalid_binary_representation,
    bad_copy_file_format,
    untranslatable_character,
    not_an_xml_document,
    invalid_xml_document,
    invalid_xml_content,
    invalid_xml_comment,
    invalid_xml_processing_instruction,
    duplicate_json_object_key_value,
    invalid_argument_for_sql_json_datetime_function,
    invalid_json_text,
    invalid_sql_json_subscript,
    more_than_one_sql_json_item,
    no_sql_json_item,
    non_numeric_sql_json_item,
    non_unique_keys_in_a_json_object,
    singleton_sql_json_item_required,
    sql_json_array_not_found,
    sql_json_member_not_found,
    sql_json_number_not_found,
    sql_json_object_not_found,
    too_many_json_array_elements,
    too_many_json_object_members,
    sql_json_scalar_required,
    sql_json_item_cannot_be_cast_to_target_type,
    integrity_constraint_violation,
    restrict_violation,
    not_null_violation,
    foreign_key_violation,
    unique_violation,
    check_violation,
    exclusion_violation,
    invalid_cursor_state,
    invalid_transaction_state,
    active_sql_transaction,
    branch_transaction_already_active,
    held_cursor_requires_same_isolation_level,
    inappropriate_access_mode_for_branch_transaction,
    inappropriate_isolation_level_for_branch_transaction,
    no_active_sql_transaction_for_branch_transaction,
    read_only_sql_transaction,
    schema_and_data_statement_mixing_not_supported,
    no_active_sql_transaction,
    in_failed_sql_transaction,
    idle_in_transaction_session_timeout,
    transaction_timeout,
    invalid_sql_statement_name,
    triggered_data_change_violation,
    invalid_authorization_specification,
    invalid_password,
    dependent_privilege_descriptors_still_exist,
    dependent_objects_still_exist,
    invalid_transaction_termination,
    sql_routine_exception,
    function_executed_no_return_statement,
    modifying_sql_data_not_permitted,
    prohibited_sql_statement_attempted,
    reading_sql_data_not_permitted,
    invalid_cursor_name,
    external_routine_exception,
    containing_sql_not_permitted,
    external_routine_invocation_exception,
    invalid_sqlstate_returned,
    trigger_protocol_violated,
    srf_protocol_violated,
    event_trigger_protocol_violated,
    savepoint_exception,
    invalid_savepoint_specification,
    invalid_catalog_name,
    invalid_schema_name,
    transaction_rollback,
    transaction_integrity_constraint_violation,
    serialization_failure,
    statement_completion_unknown,
    deadlock_detected,
    syntax_error_or_access_rule_violation,
    syntax_error,
    insufficient_privilege,
    cannot_coerce,
    grouping_error,
    windowing_error,
    invalid_recursion,
    invalid_foreign_key,
    invalid_name,
    name_too_long,
    reserved_name,
    datatype_mismatch,
    indeterminate_datatype,
    collation_mismatch,
    indeterminate_collation,
    wrong_object_type,
    generated_always,
    undefined_column,
    undefined_function,
    undefined_table,
    undefined_parameter,
    undefined_object,
    duplicate_column,
    duplicate_cursor,
    duplicate_database,
    duplicate_function,
    duplicate_prepared_statement,
    duplicate_schema,
    duplicate_table,
    duplicate_alias,
    duplicate_object,
    ambiguous_column,
    ambiguous_function,
    ambiguous_parameter,
    ambiguous_alias,
    invalid_column_reference,
    invalid_column_definition,
    invalid_cursor_definition,
    invalid_database_definition,
    invalid_function_definition,
    invalid_prepared_statement_definition,
    invalid_schema_definition,
    invalid_table_definition,
    invalid_object_definition,
    with_check_option_violation,
    insufficient_resources,
    disk_full,
    out_of_memory,
    too_many_connections,
    configuration_limit_exceeded,
    program_limit_exceeded,
    statement_too_complex,
    too_many_columns,
    too_many_arguments,
    object_not_in_prerequisite_state,
    object_in_use,
    cant_change_runtime_param,
    lock_not_available,
    unsafe_new_enum_value_usage,
    operator_intervention,
    query_canceled,
    admin_shutdown,
    crash_shutdown,
    cannot_connect_now,
    database_dropped,
    idle_session_timeout,
    system_error,
    io_error,
    undefined_file,
    duplicate_file,
    file_name_too_long,
    config_file_error,
    lock_file_exists,
    fdw_error,
    fdw_column_name_not_found,
    fdw_dynamic_parameter_value_needed,
    fdw_function_sequence_error,
    fdw_inconsistent_descriptor_information,
    fdw_invalid_attribute_value,
    fdw_invalid_column_name,
    fdw_invalid_column_number,
    fdw_invalid_data_type,
    fdw_invalid_data_type_descriptors,
    fdw_invalid_descriptor_field_identifier,
    fdw_invalid_handle,
    fdw_invalid_option_index,
    fdw_invalid_option_name,
    fdw_invalid_string_length_or_buffer_length,
    fdw_invalid_string_format,
    fdw_invalid_use_of_null_pointer,
    fdw_too_many_handles,
    fdw_out_of_memory,
    fdw_no_schemas,
    fdw_option_name_not_found,
    fdw_reply_handle,
    fdw_schema_not_found,
    fdw_table_not_found,
    fdw_unable_to_create_execution,
    fdw_unable_to_create_reply,
    fdw_unable_to_establish_connection,
    plpgsql_error,
    raise_exception,
    no_data_found,
    too_many_rows,
    assert_failure,
    internal_error,
    data_corrupted,
    index_corrupted,
};

}  // namespace nativepg

#endif
