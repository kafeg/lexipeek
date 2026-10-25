# Build the core and lexipeek-cli (CMake + vcpkg manifest from the vcpkg submodule).
BUILD ?= build
TYPE ?= Release
GEN := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")

.PHONY: deps build test bench asan clean

deps:
	git submodule update --init vcpkg tests/inkpeek engines/fasttext engines/llama engines/pcre2
	git submodule update --init --recursive engines/bergamot

build:
	cmake -S . -B $(BUILD) $(GEN) -DCMAKE_BUILD_TYPE=$(TYPE) $(CMAKE_ARGS)
	cmake --build $(BUILD) --config $(TYPE)

test: build
	ctest --test-dir $(BUILD) -C $(TYPE) --output-on-failure

bench: build
	$(BUILD)/cli/lexipeek-cli bench

asan:
	$(MAKE) test BUILD=build-asan TYPE=RelWithDebInfo CMAKE_ARGS=-DLEXIPEEK_SANITIZE=ON

clean:
	rm -rf build build-asan
