.PHONY: all build test clean format format-check size

# Default target
all: build test

# Build
build:
	@echo "🔨 Building..."
	@cmake -B build -DCMAKE_BUILD_TYPE=Debug -DV4_BUILD_TESTS=ON
	@cmake --build build -j

# Release build
release:
	@echo "🚀 Building release..."
	@cmake -B build-release -DCMAKE_BUILD_TYPE=Release -DV4_BUILD_TESTS=ON
	@cmake --build build-release -j

# Run tests
test: build
	@echo "🧪 Running tests..."
	@cd build && ctest --output-on-failure

# Clean
clean:
	@echo "🧹 Cleaning..."
	@rm -rf build build-release build-debug build-asan build-ubsan

# Apply formatting
format:
	@echo "✨ Formatting C/C++ code..."
	@find src include tests tools -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) \
		-not -path "*/vendor/*" -exec clang-format -i {} \;
	@echo "✨ Formatting CMake files..."
	@git ls-files -z --cached --others --exclude-standard -- ':(glob)**/CMakeLists.txt' ':(glob)**/*.cmake' | xargs -0 cmake-format -i
	@echo "✅ Formatting complete!"

# Format check
format-check:
	@echo "🔍 Checking C/C++ formatting..."
	@find src include tests tools -type f \( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.c' \) \
		-not -path "*/vendor/*" | xargs clang-format --dry-run --Werror || \
		(echo "❌ C/C++ formatting check failed." && exit 1)
	@echo "🔍 Checking CMake formatting..."
	@git ls-files -z --cached --others --exclude-standard -- ':(glob)**/CMakeLists.txt' ':(glob)**/*.cmake' | xargs -0 cmake-format --check || \
		(echo "❌ CMake formatting check failed." && exit 1)
	@echo "✅ All formatting checks passed!"

# Sanitizer build
asan: clean
	@echo "🛡️  Building with AddressSanitizer..."
	@cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DV4_BUILD_TESTS=ON \
		-DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g"
	@cmake --build build-asan -j
	@echo "🧪 Running tests with AddressSanitizer..."
	@cd build-asan && ctest --output-on-failure

ubsan: clean
	@echo "🛡️  Building with UndefinedBehaviorSanitizer..."
	@cmake -B build-ubsan -DCMAKE_BUILD_TYPE=Debug -DV4_BUILD_TESTS=ON \
		-DCMAKE_CXX_FLAGS="-fsanitize=undefined -fno-omit-frame-pointer -g"
	@cmake --build build-ubsan -j
	@echo "🧪 Running tests with UndefinedBehaviorSanitizer..."
	@cd build-ubsan && ctest --output-on-failure

# Reference linked executable sizes (choose a fresh output directory for each run).
SIZE_OUTPUT ?= build-size-reference
size:
	@python3 tools/size/size_report.py build --output "$(SIZE_OUTPUT)"
