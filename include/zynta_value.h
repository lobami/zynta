#pragma once

// =============================================================================
// zynta — Value type
// =============================================================================
// A small tagged union used everywhere in the framework: HTTP request/response
// bodies, JSON values, struct fields, validation errors. It's a strict subset
// of what most languages call "dynamic" — no operator overloading, no
// arithmetic on variants, just typed access via the helpers in zynta_json.h.

#include <cstdint>
#include <memory>
#include <string>
#include <variant>
#include <vector>

namespace zynta {

// Forward decl so Dict can reference it before its full definition.
struct Value;

// Dict lives in zynta_json.h. We forward-declare it here so the Value
// variant can hold a Dict without dragging JSON in.
struct DictStorage;
using Dict = std::map<std::string, std::shared_ptr<Value>>;

struct Value {
    enum class Kind {
        Null,
        Bool,
        Int,
        Double,
        String,
        Array,   // std::vector<ValuePtr>
        Dict,    // zynta::Dict
    };

    Kind kind = Kind::Null;
    using Storage = std::variant<
        std::monostate,
        bool,
        int64_t,
        double,
        std::string,
        std::vector<std::shared_ptr<Value>>,
        Dict
    >;
    Storage data{};

    // Convenience ctors — keep construction ergonomic.
    Value() = default;
    static std::shared_ptr<Value> make_null() {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Null;
        return v;
    }
    static std::shared_ptr<Value> make_bool(bool b) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Bool;
        v->data = b;
        return v;
    }
    static std::shared_ptr<Value> make_int(int64_t i) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Int;
        v->data = i;
        return v;
    }
    static std::shared_ptr<Value> make_double(double d) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Double;
        v->data = d;
        return v;
    }
    static std::shared_ptr<Value> make_string(std::string s) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::String;
        v->data = std::move(s);
        return v;
    }
    static std::shared_ptr<Value> make_array(std::vector<std::shared_ptr<Value>> a) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Array;
        v->data = std::move(a);
        return v;
    }
    static std::shared_ptr<Value> make_dict(Dict d) {
        auto v = std::make_shared<Value>();
        v->kind = Kind::Dict;
        v->data = std::move(d);
        return v;
    }
};

using ValuePtr = std::shared_ptr<Value>;

} // namespace zynta
