// JSON round-trip unit test.
#include <cassert>
#include <cstdio>
#include <string>

#include "zynta_json.h"

int main() {
    using namespace zynta;

    // parse
    auto v = json_parse(R"({"name":"alice","age":30,"tags":["a","b"],"nested":{"k":1}})");
    assert(v && v->kind == Value::Kind::Dict);

    auto& d = std::get<Dict>(v->data);
    assert(d.at("name")->kind == Value::Kind::String);
    assert(json_as_string(d.at("name")) == "alice");
    assert(json_as_int(d.at("age")) == 30);

    auto& tags = json_as_array(d.at("tags"));
    assert(tags.size() == 2);
    assert(json_as_string(tags[0]) == "a");
    assert(json_as_string(tags[1]) == "b");

    auto& nested = json_as_object(d.at("nested"));
    assert(json_as_int(nested.find("k")->second) == 1);

    // round-trip
    auto s = json_stringify(v, JsonStyle::Compact);
    auto v2 = json_parse(s);
    assert(json_as_string(json_as_object(v2).at("name")) == "alice");

    // pretty
    auto pretty = json_stringify(v, JsonStyle::Pretty);
    assert(pretty.find('\n') != std::string::npos);

    // bad input
    bool threw = false;
    try { json_parse("{not json"); } catch (...) { threw = true; }
    assert(threw);

    // null
    auto n = json_parse("null");
    assert(json_is_null(n));

    // array of ints
    auto a = json_parse("[1, 2, 3]");
    assert(json_as_array(a).size() == 3);
    assert(json_as_int(json_as_array(a)[2]) == 3);

    std::printf("json tests ok\n");
    return 0;
}
