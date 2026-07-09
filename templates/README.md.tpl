# {{name}}

A zynta web application.

## Run

```bash
zynta dev
# → zynta: serving on http://127.0.0.1:8080
```

## Endpoints

| Method | Path     | Description                |
|--------|----------|----------------------------|
| GET    | `/`      | Service banner + version   |
| GET    | `/users` | List all users from the db |
| POST   | `/users` | Insert a new user          |

## Build a native binary

```bash
zynta build
# → ./build/app
```

## Project layout

```
{{name}}/
├── app.zynta     # the entry point (novis source)
├── zynta.toml    # project config (db url, port, ...)
├── schema.sql    # db schema applied at first run
└── README.md
```

## Edit a handler

Open `app.zynta`, find the function you want to change, and re-run
`zynta dev`. There's no compile step; the novis interpreter reloads
the file on each request.
