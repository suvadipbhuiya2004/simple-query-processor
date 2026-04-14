.PHONY: all build unit-tests test test-verbose run start-cli web-install web-run rebuild clean help

# Configuration
BUILD_DIR  ?= build
BUILD_TYPE ?= Release
CMAKE      ?= cmake
JOBS       ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 1)

UNIT_TEST_BIN ?= $(BUILD_DIR)/unit_tests
GTEST_ARGS    ?=
ARGS          ?=
VENV_DIR      ?= .venv
VENV_PY       ?= $(VENV_DIR)/bin/python3

# Internal helpers
_configure = $(CMAKE) -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)
_build     = $(CMAKE) --build $(BUILD_DIR) -j$(JOBS)

# Targets
all: build

$(BUILD_DIR)/CMakeCache.txt:
	@$(_configure)

build: $(BUILD_DIR)/CMakeCache.txt
	@$(_build)

unit-tests: $(BUILD_DIR)/CMakeCache.txt
	@$(_build) --target unit_tests

test: unit-tests
	@GTEST_COLOR=1 $(UNIT_TEST_BIN) --gtest_color=yes --gtest_brief=1 $(GTEST_ARGS)

test-verbose: unit-tests
	@GTEST_COLOR=1 $(UNIT_TEST_BIN) --gtest_color=yes $(GTEST_ARGS)

run: build
	@./$(BUILD_DIR)/query_engine $(ARGS)

start-cli: build
	@./$(BUILD_DIR)/query_engine --repl

rebuild: clean build

clean:
	@$(CMAKE) -E rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  build         Configure + build all targets"
	@echo "  unit-tests    Build only the unit test binary"
	@echo "  test          Run tests (brief mode)"
	@echo "  test-verbose  Run tests (full output)"
	@echo "  run           Run query_engine using queries.sql or ARGS=..."
	@echo "  start-cli     Start interactive SQL terminal (psql-like input mode)"
	@echo "               Examples:"
	@echo "                 ARGS='--repl'"
	@echo "                 ARGS='--query \"SELECT * FROM users;\"'"
	@echo "                 ARGS='--file queries.sql'"
	@echo "               REPL meta commands: .help .tables .schema <table> .run <file.sql> .clear .exit"
	@echo "  web-install   Install Python dependencies for web terminal"
	@echo "  web-run       Build engine and launch the web terminal at http://127.0.0.1:5000"
	@echo "               Uses virtual environment at $(VENV_DIR)/"
	@echo "  rebuild       Clean then build"
	@echo "  clean         Remove $(BUILD_DIR)/" 

ci: clean build test-verbose
	@echo "CI pipeline steps completed successfully."