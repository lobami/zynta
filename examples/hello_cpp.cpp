// =============================================================================
// zynta — minimal smoke test: start a server, hit it with curl, exit.
// =============================================================================

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "zynta_http.h"

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? std::atoi(argv[2]) : 8765;

    zynta::Server s;
    s.router().add("GET", "/", [](const zynta::Request&) {
        zynta::ValuePtr v = zynta::Value::make_dict({
            {"hello", zynta::Value::make_string("zynta")},
            {"version", zynta::Value::make_string("0.1.0")},
        });
        return zynta::Response::json(200, "OK", v);
    });
    s.router().add("GET", "/health", [](const zynta::Request&) {
        zynta::ValuePtr v = zynta::Value::make_dict({
            {"status", zynta::Value::make_string("ok")},
        });
        return zynta::Response::json(200, "OK", v);
    });
    s.router().add("POST", "/echo", [](const zynta::Request& req) {
        return zynta::Response::json(200, "OK", req.json_body ? req.json_body : zynta::Value::make_null());
    });
    try {
        s.run(host, port);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zynta: %s\n", e.what());
        return 1;
    }
    return 0;
}
