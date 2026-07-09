// =============================================================================
// zynta — example: zynta_run.cpp
// =============================================================================
// This is the "novis-style" entrypoint: it loads a Novis source file, runs
// it through the Novis interpreter, and uses the resulting `Request -> Response`
// callbacks to register routes on a real HTTP server.
//
// Architecture:
//   1) The Novis source defines route handlers as named functions of the
//      shape `fn handler(req: dict) -> dict`. They are registered via a
//      builtin `zynta_route(method, path, fn)`, which C++ then wires into
//      the zynta::Server's router.
//   2) When a request arrives, the C++ side parses it, builds a
//      `std::map<std::string, ValuePtr>`, calls the registered novis
//      function, gets a ValuePtr back, and serializes to JSON.
//
// This file demonstrates the integration without requiring a Novis library
// build — it just shows the C++ side. The Novis source lives in
// `examples/rest_api.novis`.
//
// We don't ship a full novis-vm linked into zynta yet (that's Phase 2),
// so this example uses a C++ lambda directly.

#include <cstdio>
#include <cstdlib>

#include "zynta_http.h"

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? std::atoi(argv[2]) : 8080;

    zynta::Server s;

    // GET / -> {"hello": "zynta", "framework": "novis"}
    s.router().add("GET", "/", [](const zynta::Request&) {
        zynta::ValuePtr body = zynta::Value::make_dict({
            {"hello",    zynta::Value::make_string("zynta")},
            {"framework",zynta::Value::make_string("novis")},
        });
        return zynta::Response::json(200, "OK", body);
    });

    // GET /users -> list of users (in-memory store)
    s.router().add("GET", "/users", [](const zynta::Request&) {
        zynta::ValuePtr arr = zynta::Value::make_array({
            zynta::Value::make_dict({
                {"id",   zynta::Value::make_int(1)},
                {"name", zynta::Value::make_string("alice")},
            }),
            zynta::Value::make_dict({
                {"id",   zynta::Value::make_int(2)},
                {"name", zynta::Value::make_string("bob")},
            }),
        });
        return zynta::Response::json(200, "OK", arr);
    });

    // POST /users -> echo the parsed JSON body back
    s.router().add("POST", "/users", [](const zynta::Request& req) {
        if (!req.json_body || req.json_body->kind != zynta::Value::Kind::Dict) {
            zynta::ValuePtr err = zynta::Value::make_dict({
                {"error", zynta::Value::make_string("expected JSON object body")},
            });
            return zynta::Response::json(400, "Bad Request", err);
        }
        return zynta::Response::json(201, "Created", req.json_body);
    });

    try {
        s.run(host, port);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "zynta: %s\n", e.what());
        return 1;
    }
    return 0;
}
