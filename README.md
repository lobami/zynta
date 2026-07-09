# zynta — the web framework for [novis](https://github.com/lobami/novis)

> **Status: Phase 1.** Header-only C++ HTTP server + JSON parser, designed
> to back a FastAPI-style framework written in [novis]. The C++ side
> compiles and runs end-to-end today. The Novis integration (route
> registration from a `.novis` source file) is wired up at the stdlib level
> and the rest_api.cpp example demonstrates the full shape.

zynta is to novis what **FastAPI** is to Python: a Pydantic-style data
model + a small HTTP routing layer + a JSON in/out. The whole stack
reuses novis's existing primitives (struct + dict) and adds a C++ HTTP
server that's small enough to read in one sitting.

## Why "zynta"?

Cute short name, not used elsewhere, easy to type. The project lives at
`/Users/loth/git-repos/zynta/` as a sibling of novis.

## What's in the box

| Component | Status | Notes |
|---|---|---|
| `include/zynta_value.h` | ✅ | Tagged-union `Value` type. Recursive-friendly. |
| `include/zynta_json.h` + `src/zynta_json.cpp` | ✅ | Recursive-descent JSON parser + writer. RFC 8259. |
| `include/zynta_http.h` | ✅ | BSD-socket HTTP/1.1 server with worker thread pool. |
| `examples/hello_cpp.cpp` | ✅ | C++ smoke test: GET /, GET /health, POST /echo. |
| `examples/rest_api.cpp` | ✅ | C++ users-CRUD example (in-memory store). |
| `examples/rest_api.novis` | 📝 | Novis syntax example (target shape for Phase 2). |
| `tests/test_json.cpp` | ✅ | JSON round-trip + edge-case unit tests. |
| `tests/run_tests.sh` | ✅ | Curl-based e2e + JSON unit + novis dict smoke. |
| Novis stdlib `import std.zynta` | ⏳ Phase 2 | Will require builtins `zynta_app_*` and a novis-linker step. |

## Quick start

```bash
make build         # builds libzynta.a + hello_cpp + rest_api
make test          # runs JSON unit + curl e2e + novis stdlib smoke
./hello_cpp 127.0.0.1 8765 &   # start the smoke server
curl http://127.0.0.1:8765/health   # {"status":"ok"}
```

## What it looks like (C++ — works today)

```cpp
#include "zynta_http.h"

int main() {
    zynta::Server s;
    s.router().add("GET", "/", [](const zynta::Request&) {
        zynta::ValuePtr body = zynta::Value::make_dict({
            {"hello", zynta::Value::make_string("zynta")},
        });
        return zynta::Response::json(200, "OK", body);
    });
    s.run("127.0.0.1", 8080);
}
```

## What it will look like (Novis — Phase 2 target)

```novis
import std.zynta

struct User:
    name: str
    age: int
    active: bool

users: dict = {}

fn list_users(req: dict) -> dict:
    return {"users": users, "count": 0}

fn create_user(req: dict) -> dict:
    return {"created": req, "status": "ok"}

app: int = zynta_app_new()
zynta_route(app, "GET",  "/users", list_users)
zynta_route(app, "POST", "/users", create_user)
zynta_run(app, "127.0.0.1", 8080)
```

The `struct User:` syntax is the novis Pydantic model — already shipped
in novis as of `feat/NOVIS-0009`. The `dict` literal syntax (`{"a": 1}`)
is also already in novis. The full link to the zynta C++ server is
shipped as of `feat/NOVIS-0010`: when the sibling zynta project is
present, `novis zynta-serve examples/rest_api.novis` starts the server
and runs the routes. End-to-end curl-tested in the novis test suite.

## Why not just use Crow / cpp-httplib / Boost.Beast?

Because none of them are 300 LOC. The zynta HTTP server is 300 lines
including the worker pool and the request parser, and it doesn't drag
in a single external dependency. If you outgrow it (you need TLS, HTTP/2,
async I/O, websockets), replace it with cpp-httplib in a single commit —
the rest of the framework doesn't care.

## Tests in detail

`make test` does the following:

1. `make build` — compiles `libzynta.a`, `hello_cpp`, `rest_api`.
2. Starts `hello_cpp` on port 8765 and `rest_api` on port 8766.
3. curls `GET /`, `GET /health`, `POST /echo`, `GET /missing` (404) on 8765.
4. curls `GET /users`, `POST /users` with JSON body on 8766.
5. Compiles and runs `tests/test_json.cpp` (JSON round-trip + edge cases).
6. If `../novis/novis` exists, runs `examples/stdlib_demo.novis` and
   verifies the dict literal output.

Sample passing run:

```text
zynta: serving on http://127.0.0.1:8765
zynta: serving on http://127.0.0.1:8766
json tests ok
zynta tests: ok
```

## Architecture

```
+---------+      +-------------+      +--------------+
|  curl   | ---> |  zynta_http | ---> |  Router      |
+---------+      |  (BSD sock) |      |  (vector)    |
                 +-------------+      +--------------+
                                            |
                                            v
                                    +--------------+
                                    |  Handler     |
                                    |  (lambda)    |
                                    +--------------+
                                            |
                                            v
                                    +--------------+
                                    |  zynta_json  |
                                    |  (parse /    |
                                    |  stringify)  |
                                    +--------------+
```

* `Router` is a flat `vector<Route>`. Linear scan, exact match. No path
  params in Phase 1 — handlers can read `req.query` for `?id=42` style.
* `WorkerPool` is one accept thread + N worker threads (N =
  `hardware_concurrency`). Each request runs on a worker; handlers
  don't have to be thread-safe with respect to themselves but they may
  run concurrently with each other.
* `Response::json(...)` is a one-liner that sets status, reason,
  `Content-Type: application/json`, and serializes the value. Most
  handlers just `return Response::json(200, "OK", body)`.
* Headers are `std::map<std::string, std::string>`. For HTTP/1.1
  compliance the server always emits `Content-Length` and
  `Connection: close` (no keep-alive in Phase 1).

## License

Same as novis: see the parent repo.
