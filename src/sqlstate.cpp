//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <string>
#include <system_error>

#include "nativepg/sqlstate.hpp"

using namespace nativepg;

namespace {

// Deduplicates an SQLSTATE value. Some conditions
// can be represented with different SQLSTATE values, depending
// on where it happens. This maps these duplicates to their canonical value
constexpr int to_error_condition_value(int value)
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

std::string sqlstate_int_to_str(int val)
{
    // TODO
}

constexpr const char* map_known_sqlstate(int value)
{
    switch (value)
    {
        case "00000"_sqlstate: return "00000: successful_completion";
        case "01000"_sqlstate: return "01000: warning";
        case "0100C"_sqlstate: return "0100C: dynamic_result_sets_returned";
        case "01008"_sqlstate: return "01008: implicit_zero_bit_padding";
        case "01003"_sqlstate: return "01003: null_value_eliminated_in_set_function";
        case "01007"_sqlstate: return "01007: privilege_not_granted";
        case "01006"_sqlstate: return "01006: privilege_not_revoked";
        case "01P01"_sqlstate: return "01P01: deprecated_feature";
        case "02000"_sqlstate: return "02000: no_data";
        case "02001"_sqlstate: return "02001: no_additional_dynamic_result_sets_returned";
        case "03000"_sqlstate: return "03000: sql_statement_not_yet_complete";
        case "08000"_sqlstate: return "08000: connection_exception";
        case "08003"_sqlstate: return "08003: connection_does_not_exist";
        case "08006"_sqlstate: return "08006: connection_failure";
        case "08001"_sqlstate: return "08001: sqlclient_unable_to_establish_sqlconnection";
        case "08004"_sqlstate: return "08004: sqlserver_rejected_establishment_of_sqlconnection";
        case "08007"_sqlstate: return "08007: transaction_resolution_unknown";
        case "08P01"_sqlstate: return "08P01: protocol_violation";
        case "09000"_sqlstate: return "09000: triggered_action_exception";
        case "0A000"_sqlstate: return "0A000: feature_not_supported";
        case "0B000"_sqlstate: return "0B000: invalid_transaction_initiation";
        case "0F000"_sqlstate: return "0F000: locator_exception";
        case "0F001"_sqlstate: return "0F001: invalid_locator_specification";
        case "0L000"_sqlstate: return "0L000: invalid_grantor";
        case "0LP01"_sqlstate: return "0LP01: invalid_grant_operation";
        case "0P000"_sqlstate: return "0P000: invalid_role_specification";
        case "0Z000"_sqlstate: return "0Z000: diagnostics_exception";
        case "0Z002"_sqlstate: return "0Z002: stacked_diagnostics_accessed_without_active_handler";
        case "10608"_sqlstate: return "10608: invalid_argument_for_xquery";
        case "20000"_sqlstate: return "20000: case_not_found";
        case "21000"_sqlstate: return "21000: cardinality_violation";
        case "22000"_sqlstate: return "22000: data_exception";
        case "2202E"_sqlstate: return "2202E: array_subscript_error";
        case "22021"_sqlstate: return "22021: character_not_in_repertoire";
        case "22008"_sqlstate: return "22008: datetime_field_overflow";
        case "22012"_sqlstate: return "22012: division_by_zero";
        case "22005"_sqlstate: return "22005: error_in_assignment";
        case "2200B"_sqlstate: return "2200B: escape_character_conflict";
        case "22022"_sqlstate: return "22022: indicator_overflow";
        case "22015"_sqlstate: return "22015: interval_field_overflow";
        case "2201E"_sqlstate: return "2201E: invalid_argument_for_logarithm";
        case "22014"_sqlstate: return "22014: invalid_argument_for_ntile_function";
        case "22016"_sqlstate: return "22016: invalid_argument_for_nth_value_function";
        case "2201F"_sqlstate: return "2201F: invalid_argument_for_power_function";
        case "2201G"_sqlstate: return "2201G: invalid_argument_for_width_bucket_function";
        case "22018"_sqlstate: return "22018: invalid_character_value_for_cast";
        case "22007"_sqlstate: return "22007: invalid_datetime_format";
        case "22019"_sqlstate: return "22019: invalid_escape_character";
        case "2200D"_sqlstate: return "2200D: invalid_escape_octet";
        case "22025"_sqlstate: return "22025: invalid_escape_sequence";
        case "22P06"_sqlstate: return "22P06: nonstandard_use_of_escape_character";
        case "22010"_sqlstate: return "22010: invalid_indicator_parameter_value";
        case "22023"_sqlstate: return "22023: invalid_parameter_value";
        case "22013"_sqlstate: return "22013: invalid_preceding_or_following_size";
        case "2201B"_sqlstate: return "2201B: invalid_regular_expression";
        case "2201W"_sqlstate: return "2201W: invalid_row_count_in_limit_clause";
        case "2201X"_sqlstate: return "2201X: invalid_row_count_in_result_offset_clause";
        case "2202H"_sqlstate: return "2202H: invalid_tablesample_argument";
        case "2202G"_sqlstate: return "2202G: invalid_tablesample_repeat";
        case "22009"_sqlstate: return "22009: invalid_time_zone_displacement_value";
        case "2200C"_sqlstate: return "2200C: invalid_use_of_escape_character";
        case "2200G"_sqlstate: return "2200G: most_specific_type_mismatch";
        case "22004"_sqlstate: return "22004: null_value_not_allowed";
        case "22002"_sqlstate: return "22002: null_value_no_indicator_parameter";
        case "22003"_sqlstate: return "22003: numeric_value_out_of_range";
        case "2200H"_sqlstate: return "2200H: sequence_generator_limit_exceeded";
        case "22026"_sqlstate: return "22026: string_data_length_mismatch";
        case "22001"_sqlstate: return "22001: string_data_right_truncation";
        case "22011"_sqlstate: return "22011: substring_error";
        case "22027"_sqlstate: return "22027: trim_error";
        case "22024"_sqlstate: return "22024: unterminated_c_string";
        case "2200F"_sqlstate: return "2200F: zero_length_character_string";
        case "22P01"_sqlstate: return "22P01: floating_point_exception";
        case "22P02"_sqlstate: return "22P02: invalid_text_representation";
        case "22P03"_sqlstate: return "22P03: invalid_binary_representation";
        case "22P04"_sqlstate: return "22P04: bad_copy_file_format";
        case "22P05"_sqlstate: return "22P05: untranslatable_character";
        case "2200L"_sqlstate: return "2200L: not_an_xml_document";
        case "2200M"_sqlstate: return "2200M: invalid_xml_document";
        case "2200N"_sqlstate: return "2200N: invalid_xml_content";
        case "2200S"_sqlstate: return "2200S: invalid_xml_comment";
        case "2200T"_sqlstate: return "2200T: invalid_xml_processing_instruction";
        case "22030"_sqlstate: return "22030: duplicate_json_object_key_value";
        case "22031"_sqlstate: return "22031: invalid_argument_for_sql_json_datetime_function";
        case "22032"_sqlstate: return "22032: invalid_json_text";
        case "22033"_sqlstate: return "22033: invalid_sql_json_subscript";
        case "22034"_sqlstate: return "22034: more_than_one_sql_json_item";
        case "22035"_sqlstate: return "22035: no_sql_json_item";
        case "22036"_sqlstate: return "22036: non_numeric_sql_json_item";
        case "22037"_sqlstate: return "22037: non_unique_keys_in_a_json_object";
        case "22038"_sqlstate: return "22038: singleton_sql_json_item_required";
        case "22039"_sqlstate: return "22039: sql_json_array_not_found";
        case "2203A"_sqlstate: return "2203A: sql_json_member_not_found";
        case "2203B"_sqlstate: return "2203B: sql_json_number_not_found";
        case "2203C"_sqlstate: return "2203C: sql_json_object_not_found";
        case "2203D"_sqlstate: return "2203D: too_many_json_array_elements";
        case "2203E"_sqlstate: return "2203E: too_many_json_object_members";
        case "2203F"_sqlstate: return "2203F: sql_json_scalar_required";
        case "2203G"_sqlstate: return "2203G: sql_json_item_cannot_be_cast_to_target_type";
        case "23000"_sqlstate: return "23000: integrity_constraint_violation";
        case "23001"_sqlstate: return "23001: restrict_violation";
        case "23502"_sqlstate: return "23502: not_null_violation";
        case "23503"_sqlstate: return "23503: foreign_key_violation";
        case "23505"_sqlstate: return "23505: unique_violation";
        case "23514"_sqlstate: return "23514: check_violation";
        case "23P01"_sqlstate: return "23P01: exclusion_violation";
        case "24000"_sqlstate: return "24000: invalid_cursor_state";
        case "25000"_sqlstate: return "25000: invalid_transaction_state";
        case "25001"_sqlstate: return "25001: active_sql_transaction";
        case "25002"_sqlstate: return "25002: branch_transaction_already_active";
        case "25008"_sqlstate: return "25008: held_cursor_requires_same_isolation_level";
        case "25003"_sqlstate: return "25003: inappropriate_access_mode_for_branch_transaction";
        case "25004"_sqlstate: return "25004: inappropriate_isolation_level_for_branch_transaction";
        case "25005"_sqlstate: return "25005: no_active_sql_transaction_for_branch_transaction";
        case "25006"_sqlstate: return "25006: read_only_sql_transaction";
        case "25007"_sqlstate: return "25007: schema_and_data_statement_mixing_not_supported";
        case "25P01"_sqlstate: return "25P01: no_active_sql_transaction";
        case "25P02"_sqlstate: return "25P02: in_failed_sql_transaction";
        case "25P03"_sqlstate: return "25P03: idle_in_transaction_session_timeout";
        case "25P04"_sqlstate: return "25P04: transaction_timeout";
        case "26000"_sqlstate: return "26000: invalid_sql_statement_name";
        case "27000"_sqlstate: return "27000: triggered_data_change_violation";
        case "28000"_sqlstate: return "28000: invalid_authorization_specification";
        case "28P01"_sqlstate: return "28P01: invalid_password";
        case "2B000"_sqlstate: return "2B000: dependent_privilege_descriptors_still_exist";
        case "2BP01"_sqlstate: return "2BP01: dependent_objects_still_exist";
        case "2D000"_sqlstate: return "2D000: invalid_transaction_termination";
        case "2F000"_sqlstate: return "2F000: sql_routine_exception";
        case "2F005"_sqlstate: return "2F005: function_executed_no_return_statement";
        case "2F002"_sqlstate: return "2F002: modifying_sql_data_not_permitted";
        case "2F003"_sqlstate: return "2F003: prohibited_sql_statement_attempted";
        case "2F004"_sqlstate: return "2F004: reading_sql_data_not_permitted";
        case "34000"_sqlstate: return "34000: invalid_cursor_name";
        case "38000"_sqlstate: return "38000: external_routine_exception";
        case "38001"_sqlstate: return "38001: containing_sql_not_permitted";
        case "39000"_sqlstate: return "39000: external_routine_invocation_exception";
        case "39001"_sqlstate: return "39001: invalid_sqlstate_returned";
        case "39P01"_sqlstate: return "39P01: trigger_protocol_violated";
        case "39P02"_sqlstate: return "39P02: srf_protocol_violated";
        case "39P03"_sqlstate: return "39P03: event_trigger_protocol_violated";
        case "3B000"_sqlstate: return "3B000: savepoint_exception";
        case "3B001"_sqlstate: return "3B001: invalid_savepoint_specification";
        case "3D000"_sqlstate: return "3D000: invalid_catalog_name";
        case "3F000"_sqlstate: return "3F000: invalid_schema_name";
        case "40000"_sqlstate: return "40000: transaction_rollback";
        case "40002"_sqlstate: return "40002: transaction_integrity_constraint_violation";
        case "40001"_sqlstate: return "40001: serialization_failure";
        case "40003"_sqlstate: return "40003: statement_completion_unknown";
        case "40P01"_sqlstate: return "40P01: deadlock_detected";
        case "42000"_sqlstate: return "42000: syntax_error_or_access_rule_violation";
        case "42601"_sqlstate: return "42601: syntax_error";
        case "42501"_sqlstate: return "42501: insufficient_privilege";
        case "42846"_sqlstate: return "42846: cannot_coerce";
        case "42803"_sqlstate: return "42803: grouping_error";
        case "42P20"_sqlstate: return "42P20: windowing_error";
        case "42P19"_sqlstate: return "42P19: invalid_recursion";
        case "42830"_sqlstate: return "42830: invalid_foreign_key";
        case "42602"_sqlstate: return "42602: invalid_name";
        case "42622"_sqlstate: return "42622: name_too_long";
        case "42939"_sqlstate: return "42939: reserved_name";
        case "42804"_sqlstate: return "42804: datatype_mismatch";
        case "42P18"_sqlstate: return "42P18: indeterminate_datatype";
        case "42P21"_sqlstate: return "42P21: collation_mismatch";
        case "42P22"_sqlstate: return "42P22: indeterminate_collation";
        case "42809"_sqlstate: return "42809: wrong_object_type";
        case "428C9"_sqlstate: return "428C9: generated_always";
        case "42703"_sqlstate: return "42703: undefined_column";
        case "42883"_sqlstate: return "42883: undefined_function";
        case "42P01"_sqlstate: return "42P01: undefined_table";
        case "42P02"_sqlstate: return "42P02: undefined_parameter";
        case "42704"_sqlstate: return "42704: undefined_object";
        case "42701"_sqlstate: return "42701: duplicate_column";
        case "42P03"_sqlstate: return "42P03: duplicate_cursor";
        case "42P04"_sqlstate: return "42P04: duplicate_database";
        case "42723"_sqlstate: return "42723: duplicate_function";
        case "42P05"_sqlstate: return "42P05: duplicate_prepared_statement";
        case "42P06"_sqlstate: return "42P06: duplicate_schema";
        case "42P07"_sqlstate: return "42P07: duplicate_table";
        case "42712"_sqlstate: return "42712: duplicate_alias";
        case "42710"_sqlstate: return "42710: duplicate_object";
        case "42702"_sqlstate: return "42702: ambiguous_column";
        case "42725"_sqlstate: return "42725: ambiguous_function";
        case "42P08"_sqlstate: return "42P08: ambiguous_parameter";
        case "42P09"_sqlstate: return "42P09: ambiguous_alias";
        case "42P10"_sqlstate: return "42P10: invalid_column_reference";
        case "42611"_sqlstate: return "42611: invalid_column_definition";
        case "42P11"_sqlstate: return "42P11: invalid_cursor_definition";
        case "42P12"_sqlstate: return "42P12: invalid_database_definition";
        case "42P13"_sqlstate: return "42P13: invalid_function_definition";
        case "42P14"_sqlstate: return "42P14: invalid_prepared_statement_definition";
        case "42P15"_sqlstate: return "42P15: invalid_schema_definition";
        case "42P16"_sqlstate: return "42P16: invalid_table_definition";
        case "42P17"_sqlstate: return "42P17: invalid_object_definition";
        case "44000"_sqlstate: return "44000: with_check_option_violation";
        case "53000"_sqlstate: return "53000: insufficient_resources";
        case "53100"_sqlstate: return "53100: disk_full";
        case "53200"_sqlstate: return "53200: out_of_memory";
        case "53300"_sqlstate: return "53300: too_many_connections";
        case "53400"_sqlstate: return "53400: configuration_limit_exceeded";
        case "54000"_sqlstate: return "54000: program_limit_exceeded";
        case "54001"_sqlstate: return "54001: statement_too_complex";
        case "54011"_sqlstate: return "54011: too_many_columns";
        case "54023"_sqlstate: return "54023: too_many_arguments";
        case "55000"_sqlstate: return "55000: object_not_in_prerequisite_state";
        case "55006"_sqlstate: return "55006: object_in_use";
        case "55P02"_sqlstate: return "55P02: cant_change_runtime_param";
        case "55P03"_sqlstate: return "55P03: lock_not_available";
        case "55P04"_sqlstate: return "55P04: unsafe_new_enum_value_usage";
        case "57000"_sqlstate: return "57000: operator_intervention";
        case "57014"_sqlstate: return "57014: query_canceled";
        case "57P01"_sqlstate: return "57P01: admin_shutdown";
        case "57P02"_sqlstate: return "57P02: crash_shutdown";
        case "57P03"_sqlstate: return "57P03: cannot_connect_now";
        case "57P04"_sqlstate: return "57P04: database_dropped";
        case "57P05"_sqlstate: return "57P05: idle_session_timeout";
        case "58000"_sqlstate: return "58000: system_error";
        case "58030"_sqlstate: return "58030: io_error";
        case "58P01"_sqlstate: return "58P01: undefined_file";
        case "58P02"_sqlstate: return "58P02: duplicate_file";
        case "58P03"_sqlstate: return "58P03: file_name_too_long";
        case "F0000"_sqlstate: return "F0000: config_file_error";
        case "F0001"_sqlstate: return "F0001: lock_file_exists";
        case "HV000"_sqlstate: return "HV000: fdw_error";
        case "HV005"_sqlstate: return "HV005: fdw_column_name_not_found";
        case "HV002"_sqlstate: return "HV002: fdw_dynamic_parameter_value_needed";
        case "HV010"_sqlstate: return "HV010: fdw_function_sequence_error";
        case "HV021"_sqlstate: return "HV021: fdw_inconsistent_descriptor_information";
        case "HV024"_sqlstate: return "HV024: fdw_invalid_attribute_value";
        case "HV007"_sqlstate: return "HV007: fdw_invalid_column_name";
        case "HV008"_sqlstate: return "HV008: fdw_invalid_column_number";
        case "HV004"_sqlstate: return "HV004: fdw_invalid_data_type";
        case "HV006"_sqlstate: return "HV006: fdw_invalid_data_type_descriptors";
        case "HV091"_sqlstate: return "HV091: fdw_invalid_descriptor_field_identifier";
        case "HV00B"_sqlstate: return "HV00B: fdw_invalid_handle";
        case "HV00C"_sqlstate: return "HV00C: fdw_invalid_option_index";
        case "HV00D"_sqlstate: return "HV00D: fdw_invalid_option_name";
        case "HV090"_sqlstate: return "HV090: fdw_invalid_string_length_or_buffer_length";
        case "HV00A"_sqlstate: return "HV00A: fdw_invalid_string_format";
        case "HV009"_sqlstate: return "HV009: fdw_invalid_use_of_null_pointer";
        case "HV014"_sqlstate: return "HV014: fdw_too_many_handles";
        case "HV001"_sqlstate: return "HV001: fdw_out_of_memory";
        case "HV00P"_sqlstate: return "HV00P: fdw_no_schemas";
        case "HV00J"_sqlstate: return "HV00J: fdw_option_name_not_found";
        case "HV00K"_sqlstate: return "HV00K: fdw_reply_handle";
        case "HV00Q"_sqlstate: return "HV00Q: fdw_schema_not_found";
        case "HV00R"_sqlstate: return "HV00R: fdw_table_not_found";
        case "HV00L"_sqlstate: return "HV00L: fdw_unable_to_create_execution";
        case "HV00M"_sqlstate: return "HV00M: fdw_unable_to_create_reply";
        case "HV00N"_sqlstate: return "HV00N: fdw_unable_to_establish_connection";
        case "P0000"_sqlstate: return "P0000: plpgsql_error";
        case "P0001"_sqlstate: return "P0001: raise_exception";
        case "P0002"_sqlstate: return "P0002: no_data_found";
        case "P0003"_sqlstate: return "P0003: too_many_rows";
        case "P0004"_sqlstate: return "P0004: assert_failure";
        case "XX000"_sqlstate: return "XX000: internal_error";
        case "XX001"_sqlstate: return "XX001: data_corrupted";
        case "XX002"_sqlstate: return "XX002: index_corrupted";
        case "01004"_sqlstate: return "01004: string_data_right_truncation";
        case "39004"_sqlstate: return "39004: null_value_not_allowed";
        case "38002"_sqlstate: return "38002: modifying_sql_data_not_permitted";
        case "38003"_sqlstate: return "38003: prohibited_sql_statement_attempted";
        case "38004"_sqlstate: return "38004: reading_sql_data_not_permitted";
        default: return nullptr;
    }
}

class sqlstate_category final : public std::error_category
{
public:
    const char* name() const noexcept final override { return "nativepg.sqlstate"; }
    std::string message(int ev) const final override
    {
        if (ev == -1)
            return "invalid_sqlstate";
        if (const auto* msg = map_known_sqlstate(ev))
            return msg;
        return sqlstate_int_to_str(ev);
    }
    std::error_condition default_error_condition(int val) const noexcept final override
    {
        return {to_error_condition_value(val), *this};
    }
};

static sqlstate_category g_sqlstatecat;

}  // namespace

const std::error_category& nativepg::get_sqlstate_category() { return g_sqlstatecat; }
