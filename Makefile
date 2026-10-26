# Build the core and lexipeek-cli (CMake + vcpkg manifest from the vcpkg submodule); the macOS app in mac/ (XcodeGen).
BUILD ?= build
TYPE ?= Release
GEN := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")

.PHONY: deps build test bench asan clean app app-test install
# The app's signing identity: ad-hoc by default (Accessibility must then be granted again after each build).
SIGN ?= -
APP := mac/build/Build/Products/Release/LexiPeek.app
LSREGISTER := /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
XCODE := xcodebuild -project mac/LexiPeek.xcodeproj -scheme LexiPeek -derivedDataPath mac/build CODE_SIGN_IDENTITY="$(SIGN)"

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

app: build
	cd mac && xcodegen generate --quiet
	$(XCODE) -configuration Release build -quiet

app-test:
	cd mac && xcodegen generate --quiet
	$(XCODE) -configuration Debug test -quiet

# In place: deleting the bundle first would drop its Accessibility entry; the build's copy stops claiming lexipeek://.
install: app
	-pkill -x LexiPeek
	mkdir -p /Applications/LexiPeek.app
	rsync -a --delete $(APP)/ /Applications/LexiPeek.app/
	$(LSREGISTER) -u $(APP)
	/System/Library/CoreServices/pbs -update
	open /Applications/LexiPeek.app

clean:
	rm -rf build build-asan mac/build mac/LexiPeek.xcodeproj
