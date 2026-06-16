//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/assert.hpp>

#include <array>
#include <string>
#include <system_error>

#include "nativepg/sqlstate.hpp"
#include "nativepg/sqlstate_cond.hpp"

using namespace nativepg;

namespace {

// Inverse of parse_sqlstate_char. Returns \0 if the value is invalid
constexpr char unparse_sqlstate_char(int value)
{
    //   0..9  => '0'..'9'
    //   10..35 => 'A'..'Z'
    if (value < 0)
        return '\0';
    if (value < 10)
        return static_cast<char>('0' + value);
    if (value <= 35)
        return static_cast<char>('A' + value - 10);
    return '\0';
}

// True if value is a code that sqlstate_as_int could have produced: non-negative,
// no bits set above the 30 we use, and every 6-bit group a valid base-36 digit (0..35).
constexpr bool is_valid_sqlstate(int val)
{
    constexpr int six_bits_mask = 0x3f;
    // clang-format off
    return
        val >= 0 &&
        val <= 0x3fffffff &&
        unparse_sqlstate_char(val >> 24) != '\0' &&
        unparse_sqlstate_char((val >> 18) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char((val >> 12) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char((val >> 6) & six_bits_mask) != '\0' &&
        unparse_sqlstate_char(val & six_bits_mask)!= '\0';
    // clang-format on
}

// Inverse of sqlstate_as_int: unpacks the five 6-bit base-36 digits back into a 5-char string.
// Precondition: val should be a valid value
constexpr std::array<char, 5> sqlstate_as_string(int val)
{
    BOOST_ASSERT(is_valid_sqlstate(val));
    constexpr int six_bits_mask = 0x3f;
    return {
        unparse_sqlstate_char(val >> 24),
        unparse_sqlstate_char((val >> 18) & six_bits_mask),
        unparse_sqlstate_char((val >> 12) & six_bits_mask),
        unparse_sqlstate_char((val >> 6) & six_bits_mask),
        unparse_sqlstate_char(val & six_bits_mask),
    };
}

// Deduplicates an SQLSTATE value. Some conditions
// can be represented with different SQLSTATE values, depending
// on where it happens. This maps these duplicates to their canonical value
constexpr int deduplicate_sqlstate_value(int value)
{
    switch (value)
    {
        // 01004 is a warning, mapped to the data exception equivalent
        case "01004"_sqlstate: return static_cast<int>(sqlstate_cond::string_data_right_truncation);
        // 39004 is an External Routine Invocation Exception, mapped to the data exception equivalent
        case "39004"_sqlstate: return static_cast<int>(sqlstate_cond::null_value_not_allowed);
        // 38XXX are External Routine Exceptions, mapped to their SQL routine exception equivalents
        case "38002"_sqlstate: return static_cast<int>(sqlstate_cond::modifying_sql_data_not_permitted);
        case "38003"_sqlstate: return static_cast<int>(sqlstate_cond::prohibited_sql_statement_attempted);
        case "38004"_sqlstate: return static_cast<int>(sqlstate_cond::reading_sql_data_not_permitted);
        // Otherwise, don't touch the value
        default: return value;
    }
}

constexpr const char* sqlstate_to_identifier(int value)
{
    switch (value)
    {
        case "00000"_sqlstate: return "successful_completion";
        case "01000"_sqlstate: return "warning";
        case "0100C"_sqlstate: return "dynamic_result_sets_returned";
        case "01008"_sqlstate: return "implicit_zero_bit_padding";
        case "01003"_sqlstate: return "null_value_eliminated_in_set_function";
        case "01007"_sqlstate: return "privilege_not_granted";
        case "01006"_sqlstate: return "privilege_not_revoked";
        case "01P01"_sqlstate: return "deprecated_feature";
        case "02000"_sqlstate: return "no_data";
        case "02001"_sqlstate: return "no_additional_dynamic_result_sets_returned";
        case "03000"_sqlstate: return "sql_statement_not_yet_complete";
        case "08000"_sqlstate: return "connection_exception";
        case "08003"_sqlstate: return "connection_does_not_exist";
        case "08006"_sqlstate: return "connection_failure";
        case "08001"_sqlstate: return "sqlclient_unable_to_establish_sqlconnection";
        case "08004"_sqlstate: return "sqlserver_rejected_establishment_of_sqlconnection";
        case "08007"_sqlstate: return "transaction_resolution_unknown";
        case "08P01"_sqlstate: return "protocol_violation";
        case "09000"_sqlstate: return "triggered_action_exception";
        case "0A000"_sqlstate: return "feature_not_supported";
        case "0B000"_sqlstate: return "invalid_transaction_initiation";
        case "0F000"_sqlstate: return "locator_exception";
        case "0F001"_sqlstate: return "invalid_locator_specification";
        case "0L000"_sqlstate: return "invalid_grantor";
        case "0LP01"_sqlstate: return "invalid_grant_operation";
        case "0P000"_sqlstate: return "invalid_role_specification";
        case "0Z000"_sqlstate: return "diagnostics_exception";
        case "0Z002"_sqlstate: return "stacked_diagnostics_accessed_without_active_handler";
        case "10608"_sqlstate: return "invalid_argument_for_xquery";
        case "20000"_sqlstate: return "case_not_found";
        case "21000"_sqlstate: return "cardinality_violation";
        case "22000"_sqlstate: return "data_exception";
        case "2202E"_sqlstate: return "array_subscript_error";
        case "22021"_sqlstate: return "character_not_in_repertoire";
        case "22008"_sqlstate: return "datetime_field_overflow";
        case "22012"_sqlstate: return "division_by_zero";
        case "22005"_sqlstate: return "error_in_assignment";
        case "2200B"_sqlstate: return "escape_character_conflict";
        case "22022"_sqlstate: return "indicator_overflow";
        case "22015"_sqlstate: return "interval_field_overflow";
        case "2201E"_sqlstate: return "invalid_argument_for_logarithm";
        case "22014"_sqlstate: return "invalid_argument_for_ntile_function";
        case "22016"_sqlstate: return "invalid_argument_for_nth_value_function";
        case "2201F"_sqlstate: return "invalid_argument_for_power_function";
        case "2201G"_sqlstate: return "invalid_argument_for_width_bucket_function";
        case "22018"_sqlstate: return "invalid_character_value_for_cast";
        case "22007"_sqlstate: return "invalid_datetime_format";
        case "22019"_sqlstate: return "invalid_escape_character";
        case "2200D"_sqlstate: return "invalid_escape_octet";
        case "22025"_sqlstate: return "invalid_escape_sequence";
        case "22P06"_sqlstate: return "nonstandard_use_of_escape_character";
        case "22010"_sqlstate: return "invalid_indicator_parameter_value";
        case "22023"_sqlstate: return "invalid_parameter_value";
        case "22013"_sqlstate: return "invalid_preceding_or_following_size";
        case "2201B"_sqlstate: return "invalid_regular_expression";
        case "2201W"_sqlstate: return "invalid_row_count_in_limit_clause";
        case "2201X"_sqlstate: return "invalid_row_count_in_result_offset_clause";
        case "2202H"_sqlstate: return "invalid_tablesample_argument";
        case "2202G"_sqlstate: return "invalid_tablesample_repeat";
        case "22009"_sqlstate: return "invalid_time_zone_displacement_value";
        case "2200C"_sqlstate: return "invalid_use_of_escape_character";
        case "2200G"_sqlstate: return "most_specific_type_mismatch";
        case "22004"_sqlstate: return "null_value_not_allowed";
        case "22002"_sqlstate: return "null_value_no_indicator_parameter";
        case "22003"_sqlstate: return "numeric_value_out_of_range";
        case "2200H"_sqlstate: return "sequence_generator_limit_exceeded";
        case "22026"_sqlstate: return "string_data_length_mismatch";
        case "22001"_sqlstate: return "string_data_right_truncation";
        case "22011"_sqlstate: return "substring_error";
        case "22027"_sqlstate: return "trim_error";
        case "22024"_sqlstate: return "unterminated_c_string";
        case "2200F"_sqlstate: return "zero_length_character_string";
        case "22P01"_sqlstate: return "floating_point_exception";
        case "22P02"_sqlstate: return "invalid_text_representation";
        case "22P03"_sqlstate: return "invalid_binary_representation";
        case "22P04"_sqlstate: return "bad_copy_file_format";
        case "22P05"_sqlstate: return "untranslatable_character";
        case "2200L"_sqlstate: return "not_an_xml_document";
        case "2200M"_sqlstate: return "invalid_xml_document";
        case "2200N"_sqlstate: return "invalid_xml_content";
        case "2200S"_sqlstate: return "invalid_xml_comment";
        case "2200T"_sqlstate: return "invalid_xml_processing_instruction";
        case "22030"_sqlstate: return "duplicate_json_object_key_value";
        case "22031"_sqlstate: return "invalid_argument_for_sql_json_datetime_function";
        case "22032"_sqlstate: return "invalid_json_text";
        case "22033"_sqlstate: return "invalid_sql_json_subscript";
        case "22034"_sqlstate: return "more_than_one_sql_json_item";
        case "22035"_sqlstate: return "no_sql_json_item";
        case "22036"_sqlstate: return "non_numeric_sql_json_item";
        case "22037"_sqlstate: return "non_unique_keys_in_a_json_object";
        case "22038"_sqlstate: return "singleton_sql_json_item_required";
        case "22039"_sqlstate: return "sql_json_array_not_found";
        case "2203A"_sqlstate: return "sql_json_member_not_found";
        case "2203B"_sqlstate: return "sql_json_number_not_found";
        case "2203C"_sqlstate: return "sql_json_object_not_found";
        case "2203D"_sqlstate: return "too_many_json_array_elements";
        case "2203E"_sqlstate: return "too_many_json_object_members";
        case "2203F"_sqlstate: return "sql_json_scalar_required";
        case "2203G"_sqlstate: return "sql_json_item_cannot_be_cast_to_target_type";
        case "23000"_sqlstate: return "integrity_constraint_violation";
        case "23001"_sqlstate: return "restrict_violation";
        case "23502"_sqlstate: return "not_null_violation";
        case "23503"_sqlstate: return "foreign_key_violation";
        case "23505"_sqlstate: return "unique_violation";
        case "23514"_sqlstate: return "check_violation";
        case "23P01"_sqlstate: return "exclusion_violation";
        case "24000"_sqlstate: return "invalid_cursor_state";
        case "25000"_sqlstate: return "invalid_transaction_state";
        case "25001"_sqlstate: return "active_sql_transaction";
        case "25002"_sqlstate: return "branch_transaction_already_active";
        case "25008"_sqlstate: return "held_cursor_requires_same_isolation_level";
        case "25003"_sqlstate: return "inappropriate_access_mode_for_branch_transaction";
        case "25004"_sqlstate: return "inappropriate_isolation_level_for_branch_transaction";
        case "25005"_sqlstate: return "no_active_sql_transaction_for_branch_transaction";
        case "25006"_sqlstate: return "read_only_sql_transaction";
        case "25007"_sqlstate: return "schema_and_data_statement_mixing_not_supported";
        case "25P01"_sqlstate: return "no_active_sql_transaction";
        case "25P02"_sqlstate: return "in_failed_sql_transaction";
        case "25P03"_sqlstate: return "idle_in_transaction_session_timeout";
        case "25P04"_sqlstate: return "transaction_timeout";
        case "26000"_sqlstate: return "invalid_sql_statement_name";
        case "27000"_sqlstate: return "triggered_data_change_violation";
        case "28000"_sqlstate: return "invalid_authorization_specification";
        case "28P01"_sqlstate: return "invalid_password";
        case "2B000"_sqlstate: return "dependent_privilege_descriptors_still_exist";
        case "2BP01"_sqlstate: return "dependent_objects_still_exist";
        case "2D000"_sqlstate: return "invalid_transaction_termination";
        case "2F000"_sqlstate: return "sql_routine_exception";
        case "2F005"_sqlstate: return "function_executed_no_return_statement";
        case "2F002"_sqlstate: return "modifying_sql_data_not_permitted";
        case "2F003"_sqlstate: return "prohibited_sql_statement_attempted";
        case "2F004"_sqlstate: return "reading_sql_data_not_permitted";
        case "34000"_sqlstate: return "invalid_cursor_name";
        case "38000"_sqlstate: return "external_routine_exception";
        case "38001"_sqlstate: return "containing_sql_not_permitted";
        case "39000"_sqlstate: return "external_routine_invocation_exception";
        case "39001"_sqlstate: return "invalid_sqlstate_returned";
        case "39P01"_sqlstate: return "trigger_protocol_violated";
        case "39P02"_sqlstate: return "srf_protocol_violated";
        case "39P03"_sqlstate: return "event_trigger_protocol_violated";
        case "3B000"_sqlstate: return "savepoint_exception";
        case "3B001"_sqlstate: return "invalid_savepoint_specification";
        case "3D000"_sqlstate: return "invalid_catalog_name";
        case "3F000"_sqlstate: return "invalid_schema_name";
        case "40000"_sqlstate: return "transaction_rollback";
        case "40002"_sqlstate: return "transaction_integrity_constraint_violation";
        case "40001"_sqlstate: return "serialization_failure";
        case "40003"_sqlstate: return "statement_completion_unknown";
        case "40P01"_sqlstate: return "deadlock_detected";
        case "42000"_sqlstate: return "syntax_error_or_access_rule_violation";
        case "42601"_sqlstate: return "syntax_error";
        case "42501"_sqlstate: return "insufficient_privilege";
        case "42846"_sqlstate: return "cannot_coerce";
        case "42803"_sqlstate: return "grouping_error";
        case "42P20"_sqlstate: return "windowing_error";
        case "42P19"_sqlstate: return "invalid_recursion";
        case "42830"_sqlstate: return "invalid_foreign_key";
        case "42602"_sqlstate: return "invalid_name";
        case "42622"_sqlstate: return "name_too_long";
        case "42939"_sqlstate: return "reserved_name";
        case "42804"_sqlstate: return "datatype_mismatch";
        case "42P18"_sqlstate: return "indeterminate_datatype";
        case "42P21"_sqlstate: return "collation_mismatch";
        case "42P22"_sqlstate: return "indeterminate_collation";
        case "42809"_sqlstate: return "wrong_object_type";
        case "428C9"_sqlstate: return "generated_always";
        case "42703"_sqlstate: return "undefined_column";
        case "42883"_sqlstate: return "undefined_function";
        case "42P01"_sqlstate: return "undefined_table";
        case "42P02"_sqlstate: return "undefined_parameter";
        case "42704"_sqlstate: return "undefined_object";
        case "42701"_sqlstate: return "duplicate_column";
        case "42P03"_sqlstate: return "duplicate_cursor";
        case "42P04"_sqlstate: return "duplicate_database";
        case "42723"_sqlstate: return "duplicate_function";
        case "42P05"_sqlstate: return "duplicate_prepared_statement";
        case "42P06"_sqlstate: return "duplicate_schema";
        case "42P07"_sqlstate: return "duplicate_table";
        case "42712"_sqlstate: return "duplicate_alias";
        case "42710"_sqlstate: return "duplicate_object";
        case "42702"_sqlstate: return "ambiguous_column";
        case "42725"_sqlstate: return "ambiguous_function";
        case "42P08"_sqlstate: return "ambiguous_parameter";
        case "42P09"_sqlstate: return "ambiguous_alias";
        case "42P10"_sqlstate: return "invalid_column_reference";
        case "42611"_sqlstate: return "invalid_column_definition";
        case "42P11"_sqlstate: return "invalid_cursor_definition";
        case "42P12"_sqlstate: return "invalid_database_definition";
        case "42P13"_sqlstate: return "invalid_function_definition";
        case "42P14"_sqlstate: return "invalid_prepared_statement_definition";
        case "42P15"_sqlstate: return "invalid_schema_definition";
        case "42P16"_sqlstate: return "invalid_table_definition";
        case "42P17"_sqlstate: return "invalid_object_definition";
        case "44000"_sqlstate: return "with_check_option_violation";
        case "53000"_sqlstate: return "insufficient_resources";
        case "53100"_sqlstate: return "disk_full";
        case "53200"_sqlstate: return "out_of_memory";
        case "53300"_sqlstate: return "too_many_connections";
        case "53400"_sqlstate: return "configuration_limit_exceeded";
        case "54000"_sqlstate: return "program_limit_exceeded";
        case "54001"_sqlstate: return "statement_too_complex";
        case "54011"_sqlstate: return "too_many_columns";
        case "54023"_sqlstate: return "too_many_arguments";
        case "55000"_sqlstate: return "object_not_in_prerequisite_state";
        case "55006"_sqlstate: return "object_in_use";
        case "55P02"_sqlstate: return "cant_change_runtime_param";
        case "55P03"_sqlstate: return "lock_not_available";
        case "55P04"_sqlstate: return "unsafe_new_enum_value_usage";
        case "57000"_sqlstate: return "operator_intervention";
        case "57014"_sqlstate: return "query_canceled";
        case "57P01"_sqlstate: return "admin_shutdown";
        case "57P02"_sqlstate: return "crash_shutdown";
        case "57P03"_sqlstate: return "cannot_connect_now";
        case "57P04"_sqlstate: return "database_dropped";
        case "57P05"_sqlstate: return "idle_session_timeout";
        case "58000"_sqlstate: return "system_error";
        case "58030"_sqlstate: return "io_error";
        case "58P01"_sqlstate: return "undefined_file";
        case "58P02"_sqlstate: return "duplicate_file";
        case "58P03"_sqlstate: return "file_name_too_long";
        case "F0000"_sqlstate: return "config_file_error";
        case "F0001"_sqlstate: return "lock_file_exists";
        case "HV000"_sqlstate: return "fdw_error";
        case "HV005"_sqlstate: return "fdw_column_name_not_found";
        case "HV002"_sqlstate: return "fdw_dynamic_parameter_value_needed";
        case "HV010"_sqlstate: return "fdw_function_sequence_error";
        case "HV021"_sqlstate: return "fdw_inconsistent_descriptor_information";
        case "HV024"_sqlstate: return "fdw_invalid_attribute_value";
        case "HV007"_sqlstate: return "fdw_invalid_column_name";
        case "HV008"_sqlstate: return "fdw_invalid_column_number";
        case "HV004"_sqlstate: return "fdw_invalid_data_type";
        case "HV006"_sqlstate: return "fdw_invalid_data_type_descriptors";
        case "HV091"_sqlstate: return "fdw_invalid_descriptor_field_identifier";
        case "HV00B"_sqlstate: return "fdw_invalid_handle";
        case "HV00C"_sqlstate: return "fdw_invalid_option_index";
        case "HV00D"_sqlstate: return "fdw_invalid_option_name";
        case "HV090"_sqlstate: return "fdw_invalid_string_length_or_buffer_length";
        case "HV00A"_sqlstate: return "fdw_invalid_string_format";
        case "HV009"_sqlstate: return "fdw_invalid_use_of_null_pointer";
        case "HV014"_sqlstate: return "fdw_too_many_handles";
        case "HV001"_sqlstate: return "fdw_out_of_memory";
        case "HV00P"_sqlstate: return "fdw_no_schemas";
        case "HV00J"_sqlstate: return "fdw_option_name_not_found";
        case "HV00K"_sqlstate: return "fdw_reply_handle";
        case "HV00Q"_sqlstate: return "fdw_schema_not_found";
        case "HV00R"_sqlstate: return "fdw_table_not_found";
        case "HV00L"_sqlstate: return "fdw_unable_to_create_execution";
        case "HV00M"_sqlstate: return "fdw_unable_to_create_reply";
        case "HV00N"_sqlstate: return "fdw_unable_to_establish_connection";
        case "P0000"_sqlstate: return "plpgsql_error";
        case "P0001"_sqlstate: return "raise_exception";
        case "P0002"_sqlstate: return "no_data_found";
        case "P0003"_sqlstate: return "too_many_rows";
        case "P0004"_sqlstate: return "assert_failure";
        case "XX000"_sqlstate: return "internal_error";
        case "XX001"_sqlstate: return "data_corrupted";
        case "XX002"_sqlstate: return "index_corrupted";
        case "01004"_sqlstate: return "string_data_right_truncation";
        case "39004"_sqlstate: return "null_value_not_allowed";
        case "38002"_sqlstate: return "modifying_sql_data_not_permitted";
        case "38003"_sqlstate: return "prohibited_sql_statement_attempted";
        case "38004"_sqlstate: return "reading_sql_data_not_permitted";
        default: return nullptr;
    }
}

class sqlstate_category final : public std::error_category
{
public:
    const char* name() const noexcept final override { return "nativepg.sqlstate"; }
    std::string message(int ev) const final override
    {
        if (!is_valid_sqlstate(ev))
            return "bad_sqlstate";

        // 5-letter value
        auto value_as_str = sqlstate_as_string(ev);
        std::string res{value_as_str.begin(), value_as_str.end()};

        // Human-readable identifier
        if (const auto* identifier = sqlstate_to_identifier(ev))
        {
            res += ": ";
            res += identifier;
        }
        return res;
    }
    std::error_condition default_error_condition(int val) const noexcept final override
    {
        if (!is_valid_sqlstate(val))
            return sqlstate_cond::bad_sqlstate;
        return {deduplicate_sqlstate_value(val), *this};
    }
};

sqlstate_category g_sqlstatecat;

}  // namespace

const std::error_category& nativepg::get_sqlstate_category() { return g_sqlstatecat; }
