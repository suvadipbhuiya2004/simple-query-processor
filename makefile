.PHONY: all build test clean run help

# Build directory
BUILD_DIR = build
CMAKE = cmake

# Default target
all: build

build:
	@echo "--- Configuring and building the project ---"
	@$(CMAKE) -B $(BUILD_DIR) -S .
	@$(CMAKE) --build $(BUILD_DIR) -j$(shell nproc 2>/dev/null || echo 1)

test: build
	@echo "--- Running unit tests ---"
	@./$(BUILD_DIR)/tests/unit_tests

run: build
	@echo "--- Running the query engine ---"
	@./$(BUILD_DIR)/query_engine

clean:
	@echo "--- Cleaning up build artifacts ---"
	@rm -rf $(BUILD_DIR)
	@# Also clean up any artifacts left in root from previous manual CMake runs
	@rm -rf _deps/
	@rm -rf CMakeFiles/
	@rm -f CMakeCache.txt
	@rm -f cmake_install.cmake
	@rm -f CTestTestfile.cmake
	@rm -rf bin/
	@rm -rf lib/
	@echo "Done."

help:
	@echo "Available targets:"
	@echo "  make        - Build the project (default)"
	@echo "  make test   - Build and run unit tests"
	@echo "  make run    - Build and run the main application"
	@echo "  make clean  - Remove all build artifacts"
	@echo "  make help   - Show this help message"
