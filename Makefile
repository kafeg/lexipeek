# Build the core and lexipeek-cli (CMake + vcpkg manifest from the vcpkg submodule); the macOS app in mac/ (XcodeGen).
BUILD ?= build
TYPE ?= Release
GEN := $(shell command -v ninja >/dev/null 2>&1 && echo "-G Ninja")

.PHONY: deps build test bench asan clean app app-test install snapshots
# The app's signing identity. macOS ties the Accessibility grant (and the Keychain's AI keys) to it: an ad-hoc build is
# a new app each time. With the local certificate (scripts/create-dev-certificate.sh, once) builds keep the grant;
# without it (CI) ad-hoc. SIGN=<identity> picks another.
DEV_IDENTITY ?= LexiPeek Dev
SIGN ?= $(shell security find-identity -v -p codesigning 2>/dev/null | grep -q "$(DEV_IDENTITY)" && echo "$(DEV_IDENTITY)" || echo "-")
APP := mac/build/Build/Products/Release/LexiPeek.app
LSREGISTER := /System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister
# The version as InkPeek's: <major.minor>.<commits>, the build number the commits.
VERSION := 0.1
COMMITS := $(shell git rev-list --count HEAD 2>/dev/null || echo 0)
XCODE := xcodebuild -project mac/LexiPeek.xcodeproj -scheme LexiPeek -derivedDataPath mac/build CODE_SIGN_IDENTITY="$(SIGN)" \
	MARKETING_VERSION=$(VERSION).$(COMMITS) CURRENT_PROJECT_VERSION=$(COMMITS)

deps:
	git submodule update --init vcpkg tests/inkpeek engines/fasttext engines/llama engines/pcre2
	git submodule update --init --recursive engines/bergamot engines/qwentts

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

# The app's own PNGs of a fixed set of cards and the settings panes, light and dark: build/snapshots/index.html.
snapshots: app
	scripts/snapshots.sh

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
