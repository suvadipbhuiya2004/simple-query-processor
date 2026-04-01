.PHONY: all build unit-tests test test-verbose run rebuild clean help

# Configuration
BUILD_DIR  ?= build
BUILD_TYPE ?= Release
CMAKE      ?= cmake
JOBS       ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 1)

UNIT_TEST_BIN ?= $(BUILD_DIR)/unit_tests
GTEST_ARGS    ?=

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
	@./$(BUILD_DIR)/query_engine

rebuild: clean build

clean:
	@$(CMAKE) -E rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  build         Configure + build all targets"
	@echo "  unit-tests    Build only the unit test binary"
	@echo "  test          Run tests (brief mode)"
	@echo "  test-verbose  Run tests (full output)"
	@echo "  run           Run query_engine using queries.sql"
	@echo "  rebuild       Clean then build"
	@echo "  clean         Remove $(BUILD_DIR)/" 

ci: clean build test-verbose
	@echo "CI pipeline steps completed successfully."