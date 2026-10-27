#pragma once
// Minimal flat-JSON readers, shared by HCS (R9) and KML (R1.1) import.
// One parser, two consumers: a second copy of the scanning logic would drift
// on quoting/array edge cases, and both front ends promise the same rejection
// behaviour (complete violation lists, R9.2/R1.2).
//
// Scope limitation, deliberately: values are scalars, strings and arrays of
// flat objects/numbers — no braces or brackets inside string values, no
// nesting deeper than one level. That covers our two schemas; it is not a
// general JSON reader.
#include <optional>
#include <string>
#include <vector>

namespace unikern::detail {

std::string trim(const std::string& s);

// True when the key appears anywhere in the text (used to tell "absent" from
// "present but empty", which reject differently).
bool json_has(const std::string& j, const std::string& key);

// Raw value text for "key" as a string or scalar; nullopt when absent.
std::optional<std::string> json_field(const std::string& j, const std::string& key);

// Bodies of each top-level {...} inside "key": [ {...}, {...} ].
std::vector<std::string> json_objects(const std::string& j, const std::string& key);

// Tokens of "key": [ 1, 2, 3 ] as trimmed strings.
std::vector<std::string> json_numbers(const std::string& j, const std::string& key);

// Keys of the object at depth 1 (the outermost object only). Used to reject
// unknown or misspelled fields instead of silently ignoring them.
std::vector<std::string> json_keys(const std::string& j);

}  // namespace unikern::detail
