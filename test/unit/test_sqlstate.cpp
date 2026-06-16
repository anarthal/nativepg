//
// Copyright (c) 2025 Ruben Perez Hidalgo (rubenperez038 at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <boost/core/lightweight_test.hpp>

#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

#include "nativepg/sqlstate.hpp"
#include "nativepg/sqlstate_cond.hpp"
#include "nativepg_internal/sqlstate_as_string.hpp"

using namespace nativepg;

namespace {

//
// String literal
//
static_assert("00015"_sqlstate == 69);
static_assert("00000"_sqlstate == 0);
static_assert("ZZZZZ"_sqlstate == 596523235);

//
// Runtime parsing
//
void test_parse_sqlstate_success()
{
    const auto& cat = get_sqlstate_category();

    // Zero
    BOOST_TEST_EQ(parse_sqlstate("00000"), std::error_code(0, cat));

    // All digits
    BOOST_TEST_EQ(parse_sqlstate("23505"), std::error_code(34361349, cat));

    // Digits and a letter
    BOOST_TEST_EQ(parse_sqlstate("0A000"), std::error_code(2621440, cat));

    // Mixed
    BOOST_TEST_EQ(parse_sqlstate("42P01"), std::error_code(67735553, cat));

    // High letters
    BOOST_TEST_EQ(parse_sqlstate("HV00B"), std::error_code(293339147, cat));
}

// On error, unknown_sqlstate (error_code -1) is returned
void test_parse_sqlstate_error()
{
    const std::error_code invalid(-1, get_sqlstate_category());

    // empty string
    BOOST_TEST_EQ(parse_sqlstate(""), invalid);

    // string has < 5 chars
    BOOST_TEST_EQ(parse_sqlstate("0000"), invalid);

    // string has > 5 chars
    BOOST_TEST_EQ(parse_sqlstate("000000"), invalid);

    // string has lowercase chars
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);

    // string has punctuation
    BOOST_TEST_EQ(parse_sqlstate("0000-"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("42:01"), invalid);

    // string has space/newline/control
    BOOST_TEST_EQ(parse_sqlstate("00 00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\n00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00\t00"), invalid);

    // string has NULL characters
    const char with_null[] = {'2', '3', '\0', '0', '5'};
    BOOST_TEST_EQ(parse_sqlstate(std::string_view(with_null, 5)), invalid);

    // string has UTF-8 surrogates
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("val\xc3\xb1", 5)), invalid);

    // string has 0xff chars
    BOOST_TEST_EQ(parse_sqlstate(std::string_view("0000\xff", 5)), invalid);

    // Coverage: we detect errors in all the characters
    BOOST_TEST_EQ(parse_sqlstate("a0000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0a000"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("00a00"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("000a0"), invalid);
    BOOST_TEST_EQ(parse_sqlstate("0000a"), invalid);
}

//
// Error category
//
void test_sqlstate_cond()
{
    const auto& cat = get_sqlstate_category();

    // A std::error_condition can be implicitly created from a sqlstate_cond value
    {
        std::error_condition cond = sqlstate_cond::unique_violation;
        BOOST_TEST_EQ(cond.value(), static_cast<int>(sqlstate_cond::unique_violation));
        BOOST_TEST(cond.category() == cat);
    }

    // Casting a std::error_condition containing success (00000) to bool returns false
    {
        std::error_condition cond("00000"_sqlstate, cat);
        BOOST_TEST_NOT(cond);
    }

    // Casting a std::error_condition containing another sqlstate to bool returns true
    {
        std::error_condition cond = sqlstate_cond::unique_violation;
        BOOST_TEST(cond);
    }

    // Comparing a sqlstate code to an unrelated sqlstate condition returns false
    BOOST_TEST(parse_sqlstate("23505") != sqlstate_cond::syntax_error);

    // Comparing a sqlstate code to the same sqlstate condition returns true
    BOOST_TEST(parse_sqlstate("23505") == sqlstate_cond::unique_violation);

    // Comparing the conditions deduplicated in deduplicate_sqlstate_value returns true.
    // These raw SQLSTATEs map to a canonical condition via default_error_condition.
    BOOST_TEST(parse_sqlstate("01004") == sqlstate_cond::string_data_right_truncation);
    BOOST_TEST(parse_sqlstate("39004") == sqlstate_cond::null_value_not_allowed);
    BOOST_TEST(parse_sqlstate("38002") == sqlstate_cond::modifying_sql_data_not_permitted);
    BOOST_TEST(parse_sqlstate("38003") == sqlstate_cond::prohibited_sql_statement_attempted);
    BOOST_TEST(parse_sqlstate("38004") == sqlstate_cond::reading_sql_data_not_permitted);

    // Comparing codes with invalid int values to sqlstate_cond::bad_sqlstate succeeds
    BOOST_TEST(std::error_code(-1, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(-500, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(0x7fffffff, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 6, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 12, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 18, cat) == sqlstate_cond::bad_sqlstate);
    BOOST_TEST(std::error_code(36 << 24, cat) == sqlstate_cond::bad_sqlstate);

    // Comparing other codes to bad_sqlstate returns false
    BOOST_TEST(parse_sqlstate("38004") != sqlstate_cond::bad_sqlstate);
}

void test_sqlstate_cond_message()
{
    constexpr struct
    {
        int code;
        std::string_view message;
    } test_cases[] = {
        // Codes with human-readable identifiers
        {"00000"_sqlstate, "00000: successful_completion"                               },
        {"01000"_sqlstate, "01000: warning"                                             },
        {"0100C"_sqlstate, "0100C: dynamic_result_sets_returned"                        },
        {"01008"_sqlstate, "01008: implicit_zero_bit_padding"                           },
        {"01003"_sqlstate, "01003: null_value_eliminated_in_set_function"               },
        {"01007"_sqlstate, "01007: privilege_not_granted"                               },
        {"01006"_sqlstate, "01006: privilege_not_revoked"                               },
        {"01004"_sqlstate, "01004: string_data_right_truncation"                        },
        {"01P01"_sqlstate, "01P01: deprecated_feature"                                  },
        {"02000"_sqlstate, "02000: no_data"                                             },
        {"02001"_sqlstate, "02001: no_additional_dynamic_result_sets_returned"          },
        {"03000"_sqlstate, "03000: sql_statement_not_yet_complete"                      },
        {"08000"_sqlstate, "08000: connection_exception"                                },
        {"08003"_sqlstate, "08003: connection_does_not_exist"                           },
        {"08006"_sqlstate, "08006: connection_failure"                                  },
        {"08001"_sqlstate, "08001: sqlclient_unable_to_establish_sqlconnection"         },
        {"08004"_sqlstate, "08004: sqlserver_rejected_establishment_of_sqlconnection"   },
        {"08007"_sqlstate, "08007: transaction_resolution_unknown"                      },
        {"08P01"_sqlstate, "08P01: protocol_violation"                                  },
        {"09000"_sqlstate, "09000: triggered_action_exception"                          },
        {"0A000"_sqlstate, "0A000: feature_not_supported"                               },
        {"0B000"_sqlstate, "0B000: invalid_transaction_initiation"                      },
        {"0F000"_sqlstate, "0F000: locator_exception"                                   },
        {"0F001"_sqlstate, "0F001: invalid_locator_specification"                       },
        {"0L000"_sqlstate, "0L000: invalid_grantor"                                     },
        {"0LP01"_sqlstate, "0LP01: invalid_grant_operation"                             },
        {"0P000"_sqlstate, "0P000: invalid_role_specification"                          },
        {"0Z000"_sqlstate, "0Z000: diagnostics_exception"                               },
        {"0Z002"_sqlstate, "0Z002: stacked_diagnostics_accessed_without_active_handler" },
        {"10608"_sqlstate, "10608: invalid_argument_for_xquery"                         },
        {"20000"_sqlstate, "20000: case_not_found"                                      },
        {"21000"_sqlstate, "21000: cardinality_violation"                               },
        {"22000"_sqlstate, "22000: data_exception"                                      },
        {"2202E"_sqlstate, "2202E: array_subscript_error"                               },
        {"22021"_sqlstate, "22021: character_not_in_repertoire"                         },
        {"22008"_sqlstate, "22008: datetime_field_overflow"                             },
        {"22012"_sqlstate, "22012: division_by_zero"                                    },
        {"22005"_sqlstate, "22005: error_in_assignment"                                 },
        {"2200B"_sqlstate, "2200B: escape_character_conflict"                           },
        {"22022"_sqlstate, "22022: indicator_overflow"                                  },
        {"22015"_sqlstate, "22015: interval_field_overflow"                             },
        {"2201E"_sqlstate, "2201E: invalid_argument_for_logarithm"                      },
        {"22014"_sqlstate, "22014: invalid_argument_for_ntile_function"                 },
        {"22016"_sqlstate, "22016: invalid_argument_for_nth_value_function"             },
        {"2201F"_sqlstate, "2201F: invalid_argument_for_power_function"                 },
        {"2201G"_sqlstate, "2201G: invalid_argument_for_width_bucket_function"          },
        {"22018"_sqlstate, "22018: invalid_character_value_for_cast"                    },
        {"22007"_sqlstate, "22007: invalid_datetime_format"                             },
        {"22019"_sqlstate, "22019: invalid_escape_character"                            },
        {"2200D"_sqlstate, "2200D: invalid_escape_octet"                                },
        {"22025"_sqlstate, "22025: invalid_escape_sequence"                             },
        {"22P06"_sqlstate, "22P06: nonstandard_use_of_escape_character"                 },
        {"22010"_sqlstate, "22010: invalid_indicator_parameter_value"                   },
        {"22023"_sqlstate, "22023: invalid_parameter_value"                             },
        {"22013"_sqlstate, "22013: invalid_preceding_or_following_size"                 },
        {"2201B"_sqlstate, "2201B: invalid_regular_expression"                          },
        {"2201W"_sqlstate, "2201W: invalid_row_count_in_limit_clause"                   },
        {"2201X"_sqlstate, "2201X: invalid_row_count_in_result_offset_clause"           },
        {"2202H"_sqlstate, "2202H: invalid_tablesample_argument"                        },
        {"2202G"_sqlstate, "2202G: invalid_tablesample_repeat"                          },
        {"22009"_sqlstate, "22009: invalid_time_zone_displacement_value"                },
        {"2200C"_sqlstate, "2200C: invalid_use_of_escape_character"                     },
        {"2200G"_sqlstate, "2200G: most_specific_type_mismatch"                         },
        {"22004"_sqlstate, "22004: null_value_not_allowed"                              },
        {"22002"_sqlstate, "22002: null_value_no_indicator_parameter"                   },
        {"22003"_sqlstate, "22003: numeric_value_out_of_range"                          },
        {"2200H"_sqlstate, "2200H: sequence_generator_limit_exceeded"                   },
        {"22026"_sqlstate, "22026: string_data_length_mismatch"                         },
        {"22001"_sqlstate, "22001: string_data_right_truncation"                        },
        {"22011"_sqlstate, "22011: substring_error"                                     },
        {"22027"_sqlstate, "22027: trim_error"                                          },
        {"22024"_sqlstate, "22024: unterminated_c_string"                               },
        {"2200F"_sqlstate, "2200F: zero_length_character_string"                        },
        {"22P01"_sqlstate, "22P01: floating_point_exception"                            },
        {"22P02"_sqlstate, "22P02: invalid_text_representation"                         },
        {"22P03"_sqlstate, "22P03: invalid_binary_representation"                       },
        {"22P04"_sqlstate, "22P04: bad_copy_file_format"                                },
        {"22P05"_sqlstate, "22P05: untranslatable_character"                            },
        {"2200L"_sqlstate, "2200L: not_an_xml_document"                                 },
        {"2200M"_sqlstate, "2200M: invalid_xml_document"                                },
        {"2200N"_sqlstate, "2200N: invalid_xml_content"                                 },
        {"2200S"_sqlstate, "2200S: invalid_xml_comment"                                 },
        {"2200T"_sqlstate, "2200T: invalid_xml_processing_instruction"                  },
        {"22030"_sqlstate, "22030: duplicate_json_object_key_value"                     },
        {"22031"_sqlstate, "22031: invalid_argument_for_sql_json_datetime_function"     },
        {"22032"_sqlstate, "22032: invalid_json_text"                                   },
        {"22033"_sqlstate, "22033: invalid_sql_json_subscript"                          },
        {"22034"_sqlstate, "22034: more_than_one_sql_json_item"                         },
        {"22035"_sqlstate, "22035: no_sql_json_item"                                    },
        {"22036"_sqlstate, "22036: non_numeric_sql_json_item"                           },
        {"22037"_sqlstate, "22037: non_unique_keys_in_a_json_object"                    },
        {"22038"_sqlstate, "22038: singleton_sql_json_item_required"                    },
        {"22039"_sqlstate, "22039: sql_json_array_not_found"                            },
        {"2203A"_sqlstate, "2203A: sql_json_member_not_found"                           },
        {"2203B"_sqlstate, "2203B: sql_json_number_not_found"                           },
        {"2203C"_sqlstate, "2203C: sql_json_object_not_found"                           },
        {"2203D"_sqlstate, "2203D: too_many_json_array_elements"                        },
        {"2203E"_sqlstate, "2203E: too_many_json_object_members"                        },
        {"2203F"_sqlstate, "2203F: sql_json_scalar_required"                            },
        {"2203G"_sqlstate, "2203G: sql_json_item_cannot_be_cast_to_target_type"         },
        {"23000"_sqlstate, "23000: integrity_constraint_violation"                      },
        {"23001"_sqlstate, "23001: restrict_violation"                                  },
        {"23502"_sqlstate, "23502: not_null_violation"                                  },
        {"23503"_sqlstate, "23503: foreign_key_violation"                               },
        {"23505"_sqlstate, "23505: unique_violation"                                    },
        {"23514"_sqlstate, "23514: check_violation"                                     },
        {"23P01"_sqlstate, "23P01: exclusion_violation"                                 },
        {"24000"_sqlstate, "24000: invalid_cursor_state"                                },
        {"25000"_sqlstate, "25000: invalid_transaction_state"                           },
        {"25001"_sqlstate, "25001: active_sql_transaction"                              },
        {"25002"_sqlstate, "25002: branch_transaction_already_active"                   },
        {"25008"_sqlstate, "25008: held_cursor_requires_same_isolation_level"           },
        {"25003"_sqlstate, "25003: inappropriate_access_mode_for_branch_transaction"    },
        {"25004"_sqlstate, "25004: inappropriate_isolation_level_for_branch_transaction"},
        {"25005"_sqlstate, "25005: no_active_sql_transaction_for_branch_transaction"    },
        {"25006"_sqlstate, "25006: read_only_sql_transaction"                           },
        {"25007"_sqlstate, "25007: schema_and_data_statement_mixing_not_supported"      },
        {"25P01"_sqlstate, "25P01: no_active_sql_transaction"                           },
        {"25P02"_sqlstate, "25P02: in_failed_sql_transaction"                           },
        {"25P03"_sqlstate, "25P03: idle_in_transaction_session_timeout"                 },
        {"25P04"_sqlstate, "25P04: transaction_timeout"                                 },
        {"26000"_sqlstate, "26000: invalid_sql_statement_name"                          },
        {"27000"_sqlstate, "27000: triggered_data_change_violation"                     },
        {"28000"_sqlstate, "28000: invalid_authorization_specification"                 },
        {"28P01"_sqlstate, "28P01: invalid_password"                                    },
        {"2B000"_sqlstate, "2B000: dependent_privilege_descriptors_still_exist"         },
        {"2BP01"_sqlstate, "2BP01: dependent_objects_still_exist"                       },
        {"2D000"_sqlstate, "2D000: invalid_transaction_termination"                     },
        {"2F000"_sqlstate, "2F000: sql_routine_exception"                               },
        {"2F005"_sqlstate, "2F005: function_executed_no_return_statement"               },
        {"2F002"_sqlstate, "2F002: modifying_sql_data_not_permitted"                    },
        {"2F003"_sqlstate, "2F003: prohibited_sql_statement_attempted"                  },
        {"2F004"_sqlstate, "2F004: reading_sql_data_not_permitted"                      },
        {"34000"_sqlstate, "34000: invalid_cursor_name"                                 },
        {"38000"_sqlstate, "38000: external_routine_exception"                          },
        {"38001"_sqlstate, "38001: containing_sql_not_permitted"                        },
        {"38002"_sqlstate, "38002: modifying_sql_data_not_permitted"                    },
        {"38003"_sqlstate, "38003: prohibited_sql_statement_attempted"                  },
        {"38004"_sqlstate, "38004: reading_sql_data_not_permitted"                      },
        {"39000"_sqlstate, "39000: external_routine_invocation_exception"               },
        {"39001"_sqlstate, "39001: invalid_sqlstate_returned"                           },
        {"39004"_sqlstate, "39004: null_value_not_allowed"                              },
        {"39P01"_sqlstate, "39P01: trigger_protocol_violated"                           },
        {"39P02"_sqlstate, "39P02: srf_protocol_violated"                               },
        {"39P03"_sqlstate, "39P03: event_trigger_protocol_violated"                     },
        {"3B000"_sqlstate, "3B000: savepoint_exception"                                 },
        {"3B001"_sqlstate, "3B001: invalid_savepoint_specification"                     },
        {"3D000"_sqlstate, "3D000: invalid_catalog_name"                                },
        {"3F000"_sqlstate, "3F000: invalid_schema_name"                                 },
        {"40000"_sqlstate, "40000: transaction_rollback"                                },
        {"40002"_sqlstate, "40002: transaction_integrity_constraint_violation"          },
        {"40001"_sqlstate, "40001: serialization_failure"                               },
        {"40003"_sqlstate, "40003: statement_completion_unknown"                        },
        {"40P01"_sqlstate, "40P01: deadlock_detected"                                   },
        {"42000"_sqlstate, "42000: syntax_error_or_access_rule_violation"               },
        {"42601"_sqlstate, "42601: syntax_error"                                        },
        {"42501"_sqlstate, "42501: insufficient_privilege"                              },
        {"42846"_sqlstate, "42846: cannot_coerce"                                       },
        {"42803"_sqlstate, "42803: grouping_error"                                      },
        {"42P20"_sqlstate, "42P20: windowing_error"                                     },
        {"42P19"_sqlstate, "42P19: invalid_recursion"                                   },
        {"42830"_sqlstate, "42830: invalid_foreign_key"                                 },
        {"42602"_sqlstate, "42602: invalid_name"                                        },
        {"42622"_sqlstate, "42622: name_too_long"                                       },
        {"42939"_sqlstate, "42939: reserved_name"                                       },
        {"42804"_sqlstate, "42804: datatype_mismatch"                                   },
        {"42P18"_sqlstate, "42P18: indeterminate_datatype"                              },
        {"42P21"_sqlstate, "42P21: collation_mismatch"                                  },
        {"42P22"_sqlstate, "42P22: indeterminate_collation"                             },
        {"42809"_sqlstate, "42809: wrong_object_type"                                   },
        {"428C9"_sqlstate, "428C9: generated_always"                                    },
        {"42703"_sqlstate, "42703: undefined_column"                                    },
        {"42883"_sqlstate, "42883: undefined_function"                                  },
        {"42P01"_sqlstate, "42P01: undefined_table"                                     },
        {"42P02"_sqlstate, "42P02: undefined_parameter"                                 },
        {"42704"_sqlstate, "42704: undefined_object"                                    },
        {"42701"_sqlstate, "42701: duplicate_column"                                    },
        {"42P03"_sqlstate, "42P03: duplicate_cursor"                                    },
        {"42P04"_sqlstate, "42P04: duplicate_database"                                  },
        {"42723"_sqlstate, "42723: duplicate_function"                                  },
        {"42P05"_sqlstate, "42P05: duplicate_prepared_statement"                        },
        {"42P06"_sqlstate, "42P06: duplicate_schema"                                    },
        {"42P07"_sqlstate, "42P07: duplicate_table"                                     },
        {"42712"_sqlstate, "42712: duplicate_alias"                                     },
        {"42710"_sqlstate, "42710: duplicate_object"                                    },
        {"42702"_sqlstate, "42702: ambiguous_column"                                    },
        {"42725"_sqlstate, "42725: ambiguous_function"                                  },
        {"42P08"_sqlstate, "42P08: ambiguous_parameter"                                 },
        {"42P09"_sqlstate, "42P09: ambiguous_alias"                                     },
        {"42P10"_sqlstate, "42P10: invalid_column_reference"                            },
        {"42611"_sqlstate, "42611: invalid_column_definition"                           },
        {"42P11"_sqlstate, "42P11: invalid_cursor_definition"                           },
        {"42P12"_sqlstate, "42P12: invalid_database_definition"                         },
        {"42P13"_sqlstate, "42P13: invalid_function_definition"                         },
        {"42P14"_sqlstate, "42P14: invalid_prepared_statement_definition"               },
        {"42P15"_sqlstate, "42P15: invalid_schema_definition"                           },
        {"42P16"_sqlstate, "42P16: invalid_table_definition"                            },
        {"42P17"_sqlstate, "42P17: invalid_object_definition"                           },
        {"44000"_sqlstate, "44000: with_check_option_violation"                         },
        {"53000"_sqlstate, "53000: insufficient_resources"                              },
        {"53100"_sqlstate, "53100: disk_full"                                           },
        {"53200"_sqlstate, "53200: out_of_memory"                                       },
        {"53300"_sqlstate, "53300: too_many_connections"                                },
        {"53400"_sqlstate, "53400: configuration_limit_exceeded"                        },
        {"54000"_sqlstate, "54000: program_limit_exceeded"                              },
        {"54001"_sqlstate, "54001: statement_too_complex"                               },
        {"54011"_sqlstate, "54011: too_many_columns"                                    },
        {"54023"_sqlstate, "54023: too_many_arguments"                                  },
        {"55000"_sqlstate, "55000: object_not_in_prerequisite_state"                    },
        {"55006"_sqlstate, "55006: object_in_use"                                       },
        {"55P02"_sqlstate, "55P02: cant_change_runtime_param"                           },
        {"55P03"_sqlstate, "55P03: lock_not_available"                                  },
        {"55P04"_sqlstate, "55P04: unsafe_new_enum_value_usage"                         },
        {"57000"_sqlstate, "57000: operator_intervention"                               },
        {"57014"_sqlstate, "57014: query_canceled"                                      },
        {"57P01"_sqlstate, "57P01: admin_shutdown"                                      },
        {"57P02"_sqlstate, "57P02: crash_shutdown"                                      },
        {"57P03"_sqlstate, "57P03: cannot_connect_now"                                  },
        {"57P04"_sqlstate, "57P04: database_dropped"                                    },
        {"57P05"_sqlstate, "57P05: idle_session_timeout"                                },
        {"58000"_sqlstate, "58000: system_error"                                        },
        {"58030"_sqlstate, "58030: io_error"                                            },
        {"58P01"_sqlstate, "58P01: undefined_file"                                      },
        {"58P02"_sqlstate, "58P02: duplicate_file"                                      },
        {"58P03"_sqlstate, "58P03: file_name_too_long"                                  },
        {"F0000"_sqlstate, "F0000: config_file_error"                                   },
        {"F0001"_sqlstate, "F0001: lock_file_exists"                                    },
        {"HV000"_sqlstate, "HV000: fdw_error"                                           },
        {"HV005"_sqlstate, "HV005: fdw_column_name_not_found"                           },
        {"HV002"_sqlstate, "HV002: fdw_dynamic_parameter_value_needed"                  },
        {"HV010"_sqlstate, "HV010: fdw_function_sequence_error"                         },
        {"HV021"_sqlstate, "HV021: fdw_inconsistent_descriptor_information"             },
        {"HV024"_sqlstate, "HV024: fdw_invalid_attribute_value"                         },
        {"HV007"_sqlstate, "HV007: fdw_invalid_column_name"                             },
        {"HV008"_sqlstate, "HV008: fdw_invalid_column_number"                           },
        {"HV004"_sqlstate, "HV004: fdw_invalid_data_type"                               },
        {"HV006"_sqlstate, "HV006: fdw_invalid_data_type_descriptors"                   },
        {"HV091"_sqlstate, "HV091: fdw_invalid_descriptor_field_identifier"             },
        {"HV00B"_sqlstate, "HV00B: fdw_invalid_handle"                                  },
        {"HV00C"_sqlstate, "HV00C: fdw_invalid_option_index"                            },
        {"HV00D"_sqlstate, "HV00D: fdw_invalid_option_name"                             },
        {"HV090"_sqlstate, "HV090: fdw_invalid_string_length_or_buffer_length"          },
        {"HV00A"_sqlstate, "HV00A: fdw_invalid_string_format"                           },
        {"HV009"_sqlstate, "HV009: fdw_invalid_use_of_null_pointer"                     },
        {"HV014"_sqlstate, "HV014: fdw_too_many_handles"                                },
        {"HV001"_sqlstate, "HV001: fdw_out_of_memory"                                   },
        {"HV00P"_sqlstate, "HV00P: fdw_no_schemas"                                      },
        {"HV00J"_sqlstate, "HV00J: fdw_option_name_not_found"                           },
        {"HV00K"_sqlstate, "HV00K: fdw_reply_handle"                                    },
        {"HV00Q"_sqlstate, "HV00Q: fdw_schema_not_found"                                },
        {"HV00R"_sqlstate, "HV00R: fdw_table_not_found"                                 },
        {"HV00L"_sqlstate, "HV00L: fdw_unable_to_create_execution"                      },
        {"HV00M"_sqlstate, "HV00M: fdw_unable_to_create_reply"                          },
        {"HV00N"_sqlstate, "HV00N: fdw_unable_to_establish_connection"                  },
        {"P0000"_sqlstate, "P0000: plpgsql_error"                                       },
        {"P0001"_sqlstate, "P0001: raise_exception"                                     },
        {"P0002"_sqlstate, "P0002: no_data_found"                                       },
        {"P0003"_sqlstate, "P0003: too_many_rows"                                       },
        {"P0004"_sqlstate, "P0004: assert_failure"                                      },
        {"XX000"_sqlstate, "XX000: internal_error"                                      },
        {"XX001"_sqlstate, "XX001: data_corrupted"                                      },
        {"XX002"_sqlstate, "XX002: index_corrupted"                                     },

        // Valid codes with no human-readable identifiers
        {"ABC12"_sqlstate, "ABC12"                                                      },
        {"ZZZZZ"_sqlstate, "ZZZZZ"                                                      },
        {"12345"_sqlstate, "12345"                                                      },

        // Invalid values, not representing any SQLSTATE
        {-1,               "bad_sqlstate"                                               },
        {-500,             "bad_sqlstate"                                               },
        {0x7fffffff,       "bad_sqlstate"                                               },
        {36,               "bad_sqlstate"                                               },
        {36 << 6,          "bad_sqlstate"                                               },
        {36 << 12,         "bad_sqlstate"                                               },
        {36 << 18,         "bad_sqlstate"                                               },
        {36 << 24,         "bad_sqlstate"                                               },
    };

    const auto& cat = get_sqlstate_category();

    for (const auto& tc : test_cases)
    {
        if (!BOOST_TEST_EQ(std::error_code(tc.code, cat).message(), tc.message))
            std::cerr << "With code=" << tc.code << "\n";
    }
}

// Same, but specifying int values instead of using the _sqlstate literal
void test_sqlstate_cond_message_int_values()
{
    auto str_code = [](int val) { return get_sqlstate_category().message(val).substr(0, 5); };

    // Zero
    BOOST_TEST_EQ(str_code(0), "00000");

    // All digits
    BOOST_TEST_EQ(str_code(34361349), "23505");

    // Digits and a letter
    BOOST_TEST_EQ(str_code(2621440), "0A000");

    // Mixed
    BOOST_TEST_EQ(str_code(67735553), "42P01");

    // High letters
    BOOST_TEST_EQ(str_code(293339147), "HV00B");
}

}  // namespace

int main()
{
    test_parse_sqlstate_success();
    test_parse_sqlstate_error();
    test_sqlstate_cond();
    test_sqlstate_cond_message();
    test_sqlstate_cond_message_int_values();

    return boost::report_errors();
}