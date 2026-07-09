#pragma once

// =============================================================================
// zynta — Dict type and JSON in/out
// =============================================================================
// Dict is the runtime representation of {"key": value} pairs. JSON parsing
// and stringification both flow through Dict, so a parsed JSON object and a
// user-constructed Dict are interchangeable.
//
// The value type is `zynta::Value`, a tagged union declared in zynta_value.h.
// We don't depend on the rest of the framework here — Dict + JSON are usable
// on their own.

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace zynta {

// Forward declaration; the real definition is in zynta_value.h.
struct Value;
using ValuePtr = std::shared_ptr<Value>;

// Dict: a string-keyed map of values. We use std::map (not unordered) so
// `keys()` returns alphabetically sorted output, which is what people expect
// from JSON.
using Dict = std::map<std::string, ValuePtr>;

// We can't include zynta_value.h yet (mutual forward dependency), so we keep
// the JSON helpers in this header and require Value to be complete by the
// time you call them. The implementation lives in zynta_json.h.

// --- JSON parser -----------------------------------------------------------
//
// Parses a UTF-8 JSON document. Returns a ValuePtr holding the root. The
// parser is recursive-descent, ~250 LOC, and rejects malformed input with a
// `std::runtime_error` whose message includes the byte offset.

ValuePtr json_parse(const std::string& text);
ValuePtr json_parse(const char* data, std::size_t len);

// --- JSON writer -----------------------------------------------------------
//
// Renders any Value (recursively) as a compact or pretty JSON string.

enum class JsonStyle { Compact, Pretty };

std::string json_stringify(const ValuePtr& v, JsonStyle style = JsonStyle::Compact);

// --- JSON parse-or-default helpers (used by zynta HTTP layer) ---------------

inline bool json_is_object(const ValuePtr& v);
inline bool json_is_array(const ValuePtr& v);
inline bool json_is_string(const ValuePtr& v);
inline bool json_is_int(const ValuePtr& v);
inline bool json_is_double(const ValuePtr& v);
inline bool json_is_bool(const ValuePtr& v);
inline bool json_is_null(const ValuePtr& v);

// String coercion: returns "" if v is not a string.
inline std::string json_as_string(const ValuePtr& v);
inline int64_t json_as_int(const ValuePtr& v, int64_t fallback = 0);
inline double json_as_double(const ValuePtr& v, double fallback = 0.0);
inline bool json_as_bool(const ValuePtr& v, bool fallback = false);

// Object helpers
inline const Dict& json_as_object(const ValuePtr& v);
inline const std::vector<ValuePtr>& json_as_array(const ValuePtr& v);

} // namespace zynta

#include "zynta_value.h"

namespace zynta {

// ===== Inline trait checks =================================================
//
// The Value variant has a discriminator; these helpers centralize the type
// checks so the rest of the codebase doesn't need to know the variant layout.

inline bool json_is_object(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Dict;
}
inline bool json_is_array(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Array;
}
inline bool json_is_string(const ValuePtr& v) {
    return v && v->kind == Value::Kind::String;
}
inline bool json_is_int(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Int;
}
inline bool json_is_double(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Double;
}
inline bool json_is_bool(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Bool;
}
inline bool json_is_null(const ValuePtr& v) {
    return v && v->kind == Value::Kind::Null;
}

inline std::string json_as_string(const ValuePtr& v) {
    if (!v) return "";
    if (v->kind == Value::Kind::String) return std::get<std::string>(v->data);
    return "";
}
inline int64_t json_as_int(const ValuePtr& v, int64_t fallback) {
    if (!v) return fallback;
    if (v->kind == Value::Kind::Int)    return std::get<int64_t>(v->data);
    if (v->kind == Value::Kind::Double) return (int64_t)std::get<double>(v->data);
    if (v->kind == Value::Kind::Bool)   return std::get<bool>(v->data) ? 1 : 0;
    return fallback;
}
inline double json_as_double(const ValuePtr& v, double fallback) {
    if (!v) return fallback;
    if (v->kind == Value::Kind::Double) return std::get<double>(v->data);
    if (v->kind == Value::Kind::Int)    return (double)std::get<int64_t>(v->data);
    return fallback;
}
inline bool json_as_bool(const ValuePtr& v, bool fallback) {
    if (!v) return fallback;
    if (v->kind == Value::Kind::Bool) return std::get<bool>(v->data);
    return fallback;
}
inline const Dict& json_as_object(const ValuePtr& v) {
    static const Dict empty{};
    if (!v || v->kind != Value::Kind::Dict) return empty;
    return std::get<Dict>(v->data);
}
inline const std::vector<ValuePtr>& json_as_array(const ValuePtr& v) {
    static const std::vector<ValuePtr> empty{};
    if (!v || v->kind != Value::Kind::Array) return empty;
    return std::get<std::vector<ValuePtr>>(v->data);
}

} // namespace zynta
