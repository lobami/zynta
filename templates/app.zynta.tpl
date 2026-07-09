_title: str = "{{name}} (zynta)"

fn index(req: dict) -> dict:
    return {"hello": "from {{name}}", "version": "0.1.0"}

fn init_db(req: dict) -> dict:
    zynta_db_exec("CREATE TABLE IF NOT EXISTS users(id INTEGER PRIMARY KEY, name TEXT, age INTEGER)")
    return {"status": "schema ready"}

fn list_users(req: dict) -> dict:
    rows: list = zynta_db_query("SELECT id, name, age FROM users ORDER BY id")
    return {"users": rows, "count": 1}

fn create_user(req: dict) -> dict:
    zynta_db_exec("INSERT INTO users(name, age) VALUES('alice', 30)")
    return {"created": "alice"}

app: int = zynta_app_new()
zynta_route(app, "GET",  "/",       index)
zynta_route(app, "GET",  "/init",   init_db)
zynta_route(app, "GET",  "/users",  list_users)
zynta_route(app, "GET",  "/create", create_user)
zynta_run(app, "127.0.0.1", 8080)
