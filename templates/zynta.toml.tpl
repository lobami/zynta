# {{name}} — zynta project config.
#
# `zynta dev` and `zynta build` read this file to decide which database
# driver to link and on which port to serve.

[project]
name = "{{name}}"
version = "0.1.0"

[server]
host = "127.0.0.1"
port = 8080

[db]
# url = "sqlite://./{{name}}.db"
url = "{{db_url}}"
# pool_size = 4
