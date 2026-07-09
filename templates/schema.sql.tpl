-- {{name}} — initial schema.
-- zynta runs this on first `zynta dev` (when the db file is created).

CREATE TABLE IF NOT EXISTS users (
    id     INTEGER PRIMARY KEY AUTOINCREMENT,
    name   TEXT NOT NULL,
    age    INTEGER NOT NULL DEFAULT 0,
    active INTEGER NOT NULL DEFAULT 1
);

-- Add more tables below as your app grows.
