# =============================================================================
# zynta — Makefile
# =============================================================================
# `make` builds the C++ hello example, the C++ JSON tests, and the novis
# stdlib module gets shipped alongside. The novis-side examples are run via
# `make run-novis-examples` which uses the novis binary from the parent
# project (../novis/novis).
#
# Layout:
#   include/         public headers (zynta_http.h, zynta_json.h, zynta_value.h)
#   src/             implementation files
#   examples/        C++ and novis example programs
#   stdlib/          novis stdlib modules (zynta.novis)
#   tests/           C++ unit tests + shell-based e2e

CXX      ?= clang++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2 -Iinclude

NOVIS_DIR ?= ../novis
NOVIS     ?= $(NOVIS_DIR)/novis

# Optional database drivers. The Makefile auto-detects what's available
# on the system and compiles the matching source file into the C++ lib
# so the runtime can dispatch based on the URL scheme (sqlite://, etc.).
SQLITE_CFLAGS := $(shell pkg-config --cflags sqlite3 2>/dev/null || echo "")
SQLITE_LIBS   := $(shell pkg-config --libs   sqlite3 2>/dev/null || echo "-lsqlite3")
PGSQL_CFLAGS  := $(shell pkg-config --cflags libpq  2>/dev/null || echo "-I/opt/homebrew/opt/libpq/include")
PGSQL_LIBS    := $(shell pkg-config --libs   libpq  2>/dev/null || echo "-L/opt/homebrew/opt/libpq/lib -lpq")
MYSQL_CFLAGS  := $(shell mysql_config --cflags 2>/dev/null || echo "")
MYSQL_LIBS    := $(shell mysql_config --libs   2>/dev/null || echo "-lmysqlclient")

.PHONY: all build test clean run-cpp-examples run-novis-examples

all: build

# C++ library (archive) so the future novis-bindings code can link to it.
build: libzynta.a hello_cpp rest_api bin/zynta

# Compile a single .cpp into .o
%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Bundle the implementation files into a static library. We use an explicit
# ar invocation because Make's default ar rule has portability issues.
libzynta.a: src/zynta_json.o src/zynta_db.o
	ar rcs $@ $^

# Database helpers — sqlite is the default fallback (no external link
# required if pkg-config finds it). Postgres and MySQL are added
# automatically when their headers are present on the system.
src/zynta_db.o: src/zynta_db.cpp include/zynta_db.h
	$(CXX) $(CXXFLAGS) $(SQLITE_CFLAGS) $(PGSQL_CFLAGS) $(MYSQL_CFLAGS) -c -o $@ $<

hello_cpp: examples/hello_cpp.cpp include/zynta_http.h include/zynta_json.h include/zynta_value.h src/zynta_json.o
	$(CXX) $(CXXFLAGS) -o $@ examples/hello_cpp.cpp src/zynta_json.o

rest_api: examples/rest_api.cpp include/zynta_http.h include/zynta_json.h include/zynta_value.h src/zynta_json.o
	$(CXX) $(CXXFLAGS) -o $@ examples/rest_api.cpp src/zynta_json.o

# The user-facing `zynta` CLI. Built from src/zynta_cli.cpp; does not
# link against libzynta because it's just a templating tool that
# shells out to `novis zynta-serve` for dev mode.
bin/zynta: src/zynta_cli.cpp
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) -o $@ $<

# Tests
test: build
	bash tests/run_tests.sh

clean:
	rm -f *.o *.a hello_cpp rest_api
	rm -f src/*.o
	rm -rf bin/
