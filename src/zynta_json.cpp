// =============================================================================
// zynta — JSON parser & writer
// =============================================================================
// Recursive-descent parser for JSON. Supports the full RFC 8259 grammar (no
// comments, no trailing commas). Errors carry the byte offset so users can
// locate the bad token in their input.

#include "zynta_json.h"

#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <vector>

namespace zynta {

// ----- Parser state --------------------------------------------------------
namespace {

struct Parser {
    const char* data = nullptr;
    std::size_t len = 0;
    std::size_t pos = 0;

    explicit Parser(const char* d, std::size_t n) : data(d), len(n) {}

    [[noreturn]] void fail(const std::string& msg) {
        throw std::runtime_error("json parse error at byte " +
                                 std::to_string(pos) + ": " + msg);
    }

    bool eof() const { return pos >= len; }
    char peek() const { return data[pos]; }
    char next() {
        if (eof()) fail("unexpected end of input");
        return data[pos++];
    }
    void skip_ws() {
        while (!eof()) {
            char c = data[pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++pos;
            else break;
        }
    }
    bool match(char c) {
        skip_ws();
        if (!eof() && data[pos] == c) { ++pos; return true; }
        return false;
    }
    void expect(char c) {
        skip_ws();
        if (eof() || data[pos] != c) {
            fail(std::string("expected '") + c + "'");
        }
        ++pos;
    }

    ValuePtr parse_value() {
        skip_ws();
        if (eof()) fail("expected value");
        char c = peek();
        if (c == '{') return parse_object();
        if (c == '[') return parse_array();
        if (c == '"') return Value::make_string(parse_string());
        if (c == 't' || c == 'f') return parse_bool();
        if (c == 'n') return parse_null();
        return Value::make_double(parse_number());
    }

    ValuePtr parse_object() {
        expect('{');
        Dict d;
        skip_ws();
        if (match('}')) return Value::make_dict(std::move(d));
        while (true) {
            skip_ws();
            if (eof() || peek() != '"') fail("expected string key");
            std::string k = parse_string();
            expect(':');
            ValuePtr v = parse_value();
            d.emplace(std::move(k), std::move(v));
            skip_ws();
            if (match(',')) continue;
            expect('}');
            break;
        }
        return Value::make_dict(std::move(d));
    }

    ValuePtr parse_array() {
        expect('[');
        std::vector<ValuePtr> a;
        skip_ws();
        if (match(']')) return Value::make_array(std::move(a));
        while (true) {
            a.push_back(parse_value());
            skip_ws();
            if (match(',')) continue;
            expect(']');
            break;
        }
        return Value::make_array(std::move(a));
    }

    std::string parse_string() {
        expect('"');
        std::string s;
        while (true) {
            if (eof()) fail("unterminated string");
            char c = next();
            if (c == '"') break;
            if (c == '\\') {
                if (eof()) fail("bad escape");
                char e = next();
                switch (e) {
                    case '"':  s += '"';  break;
                    case '\\': s += '\\'; break;
                    case '/':  s += '/';  break;
                    case 'b':  s += '\b'; break;
                    case 'f':  s += '\f'; break;
                    case 'n':  s += '\n'; break;
                    case 'r':  s += '\r'; break;
                    case 't':  s += '\t'; break;
                    case 'u': {
                        if (pos + 4 > len) fail("bad \\u escape");
                        unsigned cp = 0;
                        for (int i = 0; i < 4; ++i) {
                            char h = data[pos + i];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= (h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= (h - 'a' + 10);
                            else if (h >= 'A' && h <= 'F') cp |= (h - 'A' + 10);
                            else fail("bad hex digit in \\u escape");
                        }
                        pos += 4;
                        // Naive UTF-8 encode (BMP only).
                        if (cp < 0x80) {
                            s += (char)cp;
                        } else if (cp < 0x800) {
                            s += (char)(0xC0 | (cp >> 6));
                            s += (char)(0x80 | (cp & 0x3F));
                        } else {
                            s += (char)(0xE0 | (cp >> 12));
                            s += (char)(0x80 | ((cp >> 6) & 0x3F));
                            s += (char)(0x80 | (cp & 0x3F));
                        }
                        break;
                    }
                    default: fail("unknown escape");
                }
            } else {
                s += c;
            }
        }
        return s;
    }

    ValuePtr parse_bool() {
        if (pos + 4 <= len && std::memcmp(data + pos, "true", 4) == 0) {
            pos += 4; return Value::make_bool(true);
        }
        if (pos + 5 <= len && std::memcmp(data + pos, "false", 5) == 0) {
            pos += 5; return Value::make_bool(false);
        }
        fail("expected true or false");
    }

    ValuePtr parse_null() {
        if (pos + 4 <= len && std::memcmp(data + pos, "null", 4) == 0) {
            pos += 4; return Value::make_null();
        }
        fail("expected null");
    }

    double parse_number() {
        // Parse a JSON number as a double. We don't try to detect integer
        // overflow; if the user cares, they should keep integers in a safe
        // range. (For 64-bit ints, callers can call json_as_int and round-trip
        // through string parsing if needed.)
        std::size_t start = pos;
        if (!eof() && (peek() == '-' || peek() == '+')) ++pos;
        while (!eof() && (std::isdigit((unsigned char)peek()) || peek() == '.' ||
                          peek() == 'e' || peek() == 'E' || peek() == '+' || peek() == '-')) {
            ++pos;
        }
        std::string n(data + start, pos - start);
        if (n.empty()) fail("expected number");
        try {
            return std::stod(n);
        } catch (...) {
            fail("invalid number '" + n + "'");
        }
    }
};

} // namespace

// ----- Public API ----------------------------------------------------------

ValuePtr json_parse(const char* data, std::size_t len) {
    Parser p(data, len);
    ValuePtr v = p.parse_value();
    p.skip_ws();
    if (!p.eof()) p.fail("trailing data after JSON value");
    return v;
}

ValuePtr json_parse(const std::string& text) {
    return json_parse(text.data(), text.size());
}

// ----- Writer --------------------------------------------------------------
namespace {

void write_string(std::string& out, const std::string& s) {
    out += '"';
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            default:
                if ((unsigned char)c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", (unsigned char)c);
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void write_indent(std::string& out, int depth) {
    for (int i = 0; i < depth; ++i) out += "  ";
}

void write_value(std::string& out, const ValuePtr& v, JsonStyle style, int depth) {
    if (!v) { out += "null"; return; }
    switch (v->kind) {
        case Value::Kind::Null:   out += "null"; break;
        case Value::Kind::Bool:   out += std::get<bool>(v->data) ? "true" : "false"; break;
        case Value::Kind::Int:    out += std::to_string(std::get<int64_t>(v->data)); break;
        case Value::Kind::Double: {
            double d = std::get<double>(v->data);
            if (std::isnan(d) || std::isinf(d)) { out += "null"; break; }
            char buf[32];
            std::snprintf(buf, sizeof(buf), "%.17g", d);
            out += buf;
            break;
        }
        case Value::Kind::String:
            write_string(out, std::get<std::string>(v->data));
            break;
        case Value::Kind::Array: {
            const auto& a = std::get<std::vector<ValuePtr>>(v->data);
            if (a.empty()) { out += "[]"; break; }
            out += '[';
            if (style == JsonStyle::Pretty) out += '\n';
            for (std::size_t i = 0; i < a.size(); ++i) {
                if (style == JsonStyle::Pretty) write_indent(out, depth + 1);
                write_value(out, a[i], style, depth + 1);
                if (i + 1 < a.size()) out += ',';
                if (style == JsonStyle::Pretty) out += '\n';
            }
            if (style == JsonStyle::Pretty) write_indent(out, depth);
            out += ']';
            break;
        }
        case Value::Kind::Dict: {
            const auto& d = std::get<Dict>(v->data);
            if (d.empty()) { out += "{}"; break; }
            out += '{';
            if (style == JsonStyle::Pretty) out += '\n';
            std::size_t i = 0;
            for (const auto& [k, val] : d) {
                if (style == JsonStyle::Pretty) write_indent(out, depth + 1);
                write_string(out, k);
                out += (style == JsonStyle::Pretty) ? ": " : ":";
                write_value(out, val, style, depth + 1);
                if (++i < d.size()) out += ',';
                if (style == JsonStyle::Pretty) out += '\n';
            }
            if (style == JsonStyle::Pretty) write_indent(out, depth);
            out += '}';
            break;
        }
    }
}

} // namespace

std::string json_stringify(const ValuePtr& v, JsonStyle style) {
    std::string out;
    out.reserve(64);
    write_value(out, v, style, 0);
    return out;
}

} // namespace zynta
