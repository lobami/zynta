# zynta

A web framework written in C++17 with bindings for [novis](https://github.com/lobami/novis).
Tiny HTTP server, embedded JSON, struct-validated request models, and
first-class connections to SQLite, PostgreSQL, and MySQL.

## Why

Most "small" web frameworks still pull in 50MB of dependencies. zynta is
the opposite: a BSD-socket HTTP server, a recursive-descent JSON parser,
and a hand-rolled SQL helper, all in C++17, no Boost, no Crow, no
Beast. The full source is small enough to read in one sitting.

## Install

```bash
curl -fsSL https://raw.githubusercontent.com/lobami/zynta/main/install.sh | sh
```

The installer drops a `zynta` binary in `~/.local/bin`. It will also
clone and build [novis](https://github.com/lobami/novis) into
`~/.local/share/zynta/novis/` if no `novis` binary is found on `PATH`.

## Create a new project

```bash
zynta new myapp
cd myapp
zynta dev
```

`zynta new <name>` scaffolds a project with:

- `app.zynta` — the entry point with a `GET /` and a `POST /users` example
- `zynta.toml` — project config (db url, port, etc.)
- `.gitignore`
- `README.md` — your app's readme

## File extensions

Two extensions are in play, one for each language:

- **`.zynta`** — source files for zynta projects (a web app, REST API, etc.)
  that use `zynta_app_new`, `zynta_route`, `zynta_run`, `zynta_db_query`, etc.
- **`.novis`** — pure novis programs that don't use the zynta framework
  (CLI tools, scripts, etc.)

Both extensions are identical under the hood: the novis binary parses the
file regardless of extension. The split is just for organization — a
`myapp/app.zynta` is unambiguously a zynta app, while a `tools/cli.novis`
is clearly a stand-alone novis program.

## Project layout

```
myapp/
├── app.zynta          # the entry point
├── zynta.toml         # project config
├── README.md
└── .gitignore
```

## Example

```novis
# app.zynta
struct User:
    name: str
    age: int
    active: bool = true

fn index(req: dict) -> dict:
    return {"hello": "from zynta", "version": "0.1.0"}

fn list_users(req: dict) -> dict:
    rows: list = zynta_db_query("users", "SELECT id, name FROM users")
    return {"users": rows, "count": 1}

app: int = zynta_app_new()
zynta_route(app, "GET",  "/",       index)
zynta_route(app, "GET",  "/users",  list_users)
zynta_run(app, "127.0.0.1", 8080)
```

## Databases

zynta ships with native bindings for SQLite, PostgreSQL, and MySQL.
The connection is configured in `zynta.toml`:

```toml
[db]
url = "sqlite://./app.db"
# url = "postgresql://user:pass@localhost:5432/mydb"
# url = "mysql://user:pass@localhost:3306/mydb"
```

A query is one call:

```novis
rows: list = zynta_db_query("users", "SELECT id, name FROM users")
```

Behind the scenes, zynta holds one connection per process. The query
returns a list of dicts (one per row) ready to be returned from a route
handler.

## CLI

```
zynta new <name>     # create a new zynta project
zynta dev            # run the local dev server (auto-reload optional)
zynta build          # compile the project to a native binary
zynta help           # show all commands
```

## Status

| Component | Status |
|---|---|
| HTTP/1.1 server (BSD socket + worker pool) | ✅ |
| JSON parser / writer (RFC 8259) | ✅ |
| `novis zynta-serve <file>` integration | ✅ |
| SQLite binding (`sqlite://`) | ✅ |
| PostgreSQL binding (`postgresql://`) | ✅ |
| MySQL binding (`mysql://`) | ✅ |
| TLS / HTTPS | ⏳ Phase 2 |
| HTTP/2 | ⏳ Phase 2 |
| Async route handlers (`async fn`) | ⏳ Phase 2 |

## License

Same as novis.
