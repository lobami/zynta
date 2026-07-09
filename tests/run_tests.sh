#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

# 1. Build everything
make build >/dev/null

# 2. Smoke test the C++ server (hello_cpp) with curl
./hello_cpp 127.0.0.1 8765 &
HTTP_PID=$!
trap "kill $HTTP_PID 2>/dev/null || true" EXIT

# Wait up to 3s for the server to bind
for i in 1 2 3 4 5 6; do
    if curl -s --max-time 1 http://127.0.0.1:8765/health >/dev/null 2>&1; then break; fi
    sleep 0.5
done

# GET /health
got="$(curl -s --max-time 2 http://127.0.0.1:8765/health)"
if [[ "$got" != *'"status":"ok"'* ]]; then
    echo "GET /health returned wrong body: $got" >&2
    exit 1
fi

# GET /
got="$(curl -s --max-time 2 http://127.0.0.1:8765/)"
if [[ "$got" != *'"hello":"zynta"'* ]]; then
    echo "GET / returned wrong body: $got" >&2
    exit 1
fi

# POST /echo with JSON
got="$(curl -s --max-time 2 -X POST -H 'Content-Type: application/json' \
    -d '{"name":"alice","age":30}' http://127.0.0.1:8765/echo)"
if [[ "$got" != *'"name":"alice"'* ]] || [[ "$got" != *'"age":30'* ]]; then
    echo "POST /echo returned wrong body: $got" >&2
    exit 1
fi

# 404
got="$(curl -s --max-time 2 -o /dev/null -w '%{http_code}' http://127.0.0.1:8765/missing)"
if [[ "$got" != "404" ]]; then
    echo "GET /missing expected 404 got $got" >&2
    exit 1
fi

# 3. REST API example: GET /users, POST /users with JSON body
./rest_api 127.0.0.1 8766 &
API_PID=$!
trap "kill $HTTP_PID $API_PID 2>/dev/null || true" EXIT

for i in 1 2 3 4 5 6; do
    if curl -s --max-time 1 http://127.0.0.1:8766/users >/dev/null 2>&1; then break; fi
    sleep 0.5
done

got="$(curl -s --max-time 2 http://127.0.0.1:8766/users)"
if [[ "$got" != *'"id":1'* ]] || [[ "$got" != *'"alice"'* ]]; then
    echo "GET /users returned wrong body: $got" >&2
    exit 1
fi

got="$(curl -s --max-time 2 -X POST -H 'Content-Type: application/json' \
    -d '{"name":"carol","age":42}' http://127.0.0.1:8766/users)"
if [[ "$got" != *'201'* && "$got" != *'"name":"carol"'* ]]; then
    echo "POST /users returned wrong body: $got" >&2
    exit 1
fi

# 4. JSON unit tests (compile + run a small C++ test)
clang++ -std=c++17 -O2 -Iinclude tests/test_json.cpp src/zynta_json.o -o /tmp/zynta_test_json
/tmp/zynta_test_json

# 5. No-vis stdlib smoke: dict literal works as the zynta body type
NOVIS="${NOVIS_DIR:-../novis}/novis"
if [[ -x "$NOVIS" ]]; then
    "$NOVIS" check examples/stdlib_demo.novis >/dev/null
    out="$("$NOVIS" run examples/stdlib_demo.novis)"
    if [[ "$out" != *'name: alice'* ]]; then
        echo "stdlib_demo output mismatch: $out" >&2
        exit 1
    fi
fi

echo "zynta tests: ok"
