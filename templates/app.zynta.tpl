// {{name}} — zynta entry point.
//
// `novis zynta-serve app.zynta` reads this file, registers the routes, and
// starts the C++ HTTP server. Edit the handlers below; the framework does
// the JSON in/out for you.

struct User:
    name: str
    age: int
    active: bool = true

fn index(req: dict) -> dict:
    return {"hello": "from {{name}}", "version": "0.1.0"}

fn list_users(req: dict) -> dict:
    rows: list = zynta_db_query("SELECT id, name, age FROM users ORDER BY id")
    return {"users": rows, "count": 1}

fn create_user(req: dict) -> dict:
    name: str = req.json.name
    age: int = req.json.age
    zynta_db_exec("INSERT INTO users(name, age) VALUES(?, ?)", name, age)
    return {"created": req.json, "status": "ok"}

app: int = zynta_app_new()
zynta_route(app, "GET",  "/",       index)
zynta_route(app, "GET",  "/users",  list_users)
zynta_route(app, "POST", "/users",  create_user)
zynta_run(app, "127.0.0.1", 8080)
