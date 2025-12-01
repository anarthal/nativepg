//
// Created by Henry Roeland on 18/11/2025.
//

#ifndef NATIVEPG_MISC_PARAMS_HPP
#define NATIVEPG_MISC_PARAMS_HPP

#include <algorithm>
#include <bit>
#include <ranges>
#include <regex>
#include <string>
#include <vector>

namespace nativepg {
namespace misc {

    /**
     * Expands environment variables using a regular expression to find all patterns.
     * Looks for patterns like ${VAR}, $VAR, or %VAR%.
     *
     * @param input The string containing potential environment variable placeholders.
     * @return The expanded string.
     */
    inline std::string expand_environment_variables(const std::string& input) {
        // Regex to capture variables in formats like ${VAR}, $VAR, %VAR%
        // This is a comprehensive regex that might need adjustment based on required formats
        // The main capture group is (\\w+)
        std::regex env_regex("\\$\\{(\\w+)\\}|\\$(\\w+)|%(\\w+)%");

        std::string result = input;
        std::smatch match;

        // Use a while loop to find and replace all matches iteratively
        while (std::regex_search(result, match, env_regex)) {
            std::string var_name;

            // Determine which capture group has the variable name
            if (match[1].matched) { var_name = match[1].str(); }
            else if (match[2].matched) { var_name = match[2].str(); }
            else if (match[3].matched) { var_name = match[3].str(); }

            const char* var_value = std::getenv(var_name.c_str());

            if (var_value != nullptr) {
                // Replace the entire match with the environment value
                result.replace(match.position(), match.length(), var_value);
            } else {
                // If variable not found, leave the placeholder as is and advance search past it
                result.replace(match.position(), match.length(), match.str()); // Replace with itself to advance search
            }
        }

        return result;
    }


    using name_value_pair = std::pair<std::string, std::string>;

    inline auto parse_name_value = [](const auto& token) -> name_value_pair {
        const std::string s(std::ranges::begin(token), std::ranges::end(token));
        const size_t pos = s.find('=');
        if (pos == std::string::npos) {
            return {s, ""}; // Return name with empty value if '=' is missing
        }
        return {s.substr(0, pos), s.substr(pos + 1)};
    };

    inline std::vector<name_value_pair> parse_string_to_pairs(
        const std::string& input,
        bool expand_env_vars = true,
        const char delimiter = ';')
    {
        auto semicolon_split = input | std::views::split(delimiter);
        auto pair_view = semicolon_split | std::views::transform(parse_name_value);
        auto result = std::vector<name_value_pair>(std::ranges::begin(pair_view), std::ranges::end(pair_view));
        if (expand_env_vars) {
            for (int i=0;i<(int)result.size();i++) {
                result[i].second = expand_environment_variables(result[i].second);
            }
        }
        return result;
    }

    inline void string_to_lower(std::string& str) {
        std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c){ return std::tolower(c); }
        );
    }

    // Helper function for case-insensitive string comparison
    inline bool is_equal_case_insensitive(const std::string& str1, const std::string& str2) {
        if (str1.length() != str2.length()) {
            return false;
        }
        // C++23 ranges::equal with a custom predicate that ignores case
        return std::ranges::equal(str1, str2, [](char c1, char c2){
            return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
        });
    }

    // Function to find a value case-insensitively in the vector of pairs
    inline std::string find_value_case_insensitive(const std::vector<name_value_pair>& pairs, std::string_view search_name) {
        // Define the predicate for ranges::find_if
        auto predicate = [&](const name_value_pair& p) {
            return is_equal_case_insensitive(p.first, std::string(search_name));
        };

        // Use std::ranges::find_if to locate the element
        auto it = std::ranges::find_if(pairs, predicate);

        if (it != pairs.end()) {
            return it->second; // Return the found value
        } else {
            return "";
        }
    }

}
}

#endif
