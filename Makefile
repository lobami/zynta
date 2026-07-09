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

.PHONY: all build test clean run-cpp-examples run-novis-examples

all: build

# C++ library (archive) so the future novis-bindings code can link to it.
build: libzynta.a hello_cpp rest_api

# Compile a single .cpp into .o
%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# Bundle the implementation files into a static library. We use an explicit
# ar invocation because Make's default ar rule has portability issues.
libzynta.a: src/zynta_json.o
	ar rcs $@ $^

hello_cpp: examples/hello_cpp.cpp include/zynta_http.h include/zynta_json.h include/zynta_value.h src/zynta_json.o
	$(CXX) $(CXXFLAGS) -o $@ examples/hello_cpp.cpp src/zynta_json.o

rest_api: examples/rest_api.cpp include/zynta_http.h include/zynta_json.h include/zynta_value.h src/zynta_json.o
	$(CXX) $(CXXFLAGS) -o $@ examples/rest_api.cpp src/zynta_json.o

# Tests
test: build
	bash tests/run_tests.sh

# C++ examples
run-cpp-examples: hello_cpp
	./hello_cpp 127.0.0.1 8765 &
	HTTP_PID=$$!
	sleep 0.3
	curl -s http://127.0.0.1:8765/ && echo
	curl -s http://127.0.0.1:8765/health && echo
	kill $$HTTP_PID 2>/dev/null || true

# Novis examples (require ../novis/novis to be built)
run-novis-examples:
	@if [ ! -x "$(NOVIS)" ]; then \
		echo "error: $(NOVIS) not found. Build novis first."; exit 1; \
	fi
	@echo "(zynta novis stdlib tests)"

clean:
	rm -f *.o *.a hello_cpp
	rm -f src/*.o
