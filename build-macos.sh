#!/usr/bin/env bash
#
# Build tg on macOS with Homebrew-provided dependencies.
#
#   ./build-macos.sh              build tg-timer
#   ./build-macos.sh --deps       install the Homebrew dependencies first
#   ./build-macos.sh --app        also produce Tg.app in ./dist
#   ./build-macos.sh --debug      build tg-timer-dbg instead
#   ./build-macos.sh --clean      start from a clean tree
#   ./build-macos.sh --run        launch the program when done
#
set -euo pipefail

cd "$(dirname "$0")"

BREW_PACKAGES=(pkg-config autoconf automake libtool gtk+3 portaudio fftw adwaita-icon-theme)

INSTALL_DEPS=0
MAKE_APP=0
DEBUG_BUILD=0
CLEAN=0
RUN=0
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"

info()  { printf '\033[1;34m==>\033[0m %s\n' "$*"; }
warn()  { printf '\033[1;33m==>\033[0m %s\n' "$*" >&2; }
die()   { printf '\033[1;31m==>\033[0m %s\n' "$*" >&2; exit 1; }

usage() {
	sed -n '3,11p' "$0" | sed 's/^# \{0,1\}//'
	exit 0
}

while [ $# -gt 0 ]; do
	case "$1" in
		--deps)  INSTALL_DEPS=1 ;;
		--app)   MAKE_APP=1 ;;
		--debug) DEBUG_BUILD=1 ;;
		--clean) CLEAN=1 ;;
		--run)   RUN=1 ;;
		-j)      shift; JOBS="${1:?-j needs a number}" ;;
		-h|--help) usage ;;
		*) die "Unknown option: $1 (try --help)" ;;
	esac
	shift
done

[ "$(uname -s)" = "Darwin" ] || die "This script is for macOS; on other systems use ./autogen.sh && ./configure && make"

# --- Toolchain -------------------------------------------------------------

command -v brew >/dev/null 2>&1 || die "Homebrew is required. Install it from https://brew.sh"
BREW_PREFIX="$(brew --prefix)"

if ! xcode-select -p >/dev/null 2>&1; then
	die "Command line tools missing. Run: xcode-select --install"
fi

if [ "$INSTALL_DEPS" = 1 ]; then
	info "Installing dependencies: ${BREW_PACKAGES[*]}"
	brew install "${BREW_PACKAGES[@]}"
fi

# Homebrew keeps some tools out of the default PATH
for keg in gettext libtool; do
	if [ -d "$BREW_PREFIX/opt/$keg/bin" ]; then
		PATH="$BREW_PREFIX/opt/$keg/bin:$PATH"
	fi
done
export PATH

export PKG_CONFIG_PATH="$BREW_PREFIX/lib/pkgconfig:$BREW_PREFIX/share/pkgconfig:${PKG_CONFIG_PATH:-}"
export ACLOCAL_PATH="$BREW_PREFIX/share/aclocal:${ACLOCAL_PATH:-}"

missing=()
for tool in autoconf automake pkg-config; do
	command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
done
for mod in gtk+-3.0 portaudio-2.0 fftw3f; do
	pkg-config --exists "$mod" 2>/dev/null || missing+=("$mod")
done
if [ ${#missing[@]} -gt 0 ]; then
	warn "Missing: ${missing[*]}"
	die "Run './build-macos.sh --deps' to install the Homebrew dependencies."
fi

# --- Build -----------------------------------------------------------------

if [ "$CLEAN" = 1 ]; then
	info "Cleaning the tree"
	[ -f Makefile ] && make distclean >/dev/null 2>&1 || true
	rm -rf autom4te.cache dist aclocal.m4 configure config.status config.log \
	       Makefile.in icons/Makefile.in
fi

if [ ! -f configure ]; then
	info "Generating the build system (autogen.sh)"
	./autogen.sh
fi

if [ ! -f Makefile ] || [ configure -nt Makefile ]; then
	info "Configuring"
	./configure CFLAGS="${CFLAGS:--O2 -g}"
fi

TARGET=tg-timer
[ "$DEBUG_BUILD" = 1 ] && TARGET=tg-timer-dbg

info "Building $TARGET with $JOBS jobs"
make -j"$JOBS" "$TARGET"

info "Built ./$TARGET"

# --- Application bundle ----------------------------------------------------

if [ "$MAKE_APP" = 1 ]; then
	APP="dist/Tg.app"
	VERSION="$(cat version 2>/dev/null | tr -d '[:space:]')"
	info "Creating $APP"

	rm -rf "$APP"
	mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"

	cp "$TARGET" "$APP/Contents/MacOS/tg-timer"

	# Icon, if iconutil can build one from the PNGs shipped in icons/
	if command -v iconutil >/dev/null 2>&1 && command -v sips >/dev/null 2>&1; then
		SRC_ICON="$(ls icons/256x256/apps/tg-timer.png icons/128x128/apps/tg-timer.png 2>/dev/null | head -n 1 || true)"
		if [ -n "$SRC_ICON" ]; then
			ICONSET="$(mktemp -d)/tg.iconset"
			mkdir -p "$ICONSET"
			for size in 16 32 64 128 256 512; do
				sips -z $size $size "$SRC_ICON" \
					--out "$ICONSET/icon_${size}x${size}.png" >/dev/null 2>&1 || true
				sips -z $((size*2)) $((size*2)) "$SRC_ICON" \
					--out "$ICONSET/icon_${size}x${size}@2x.png" >/dev/null 2>&1 || true
			done
			iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/tg.icns" >/dev/null 2>&1 \
				|| warn "Could not build the .icns icon; the bundle will use the default one"
			rm -rf "$(dirname "$ICONSET")"
		fi
	fi

	cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>CFBundleName</key>              <string>Tg</string>
	<key>CFBundleDisplayName</key>       <string>Tg</string>
	<key>CFBundleExecutable</key>        <string>tg-timer</string>
	<key>CFBundleIdentifier</key>        <string>li.ciovil.tg</string>
	<key>CFBundleVersion</key>           <string>${VERSION:-0}</string>
	<key>CFBundleShortVersionString</key><string>${VERSION:-0}</string>
	<key>CFBundlePackageType</key>       <string>APPL</string>
	<key>CFBundleIconFile</key>          <string>tg.icns</string>
	<key>NSHighResolutionCapable</key>   <true/>
	<key>NSMicrophoneUsageDescription</key>
	<string>Tg listens to your watch through the microphone to measure its rate.</string>
	<key>LSMinimumSystemVersion</key>    <string>10.13</string>
	<key>CFBundleDocumentTypes</key>
	<array>
		<dict>
			<key>CFBundleTypeName</key>       <string>Tg snapshot</string>
			<key>CFBundleTypeExtensions</key> <array><string>tgj</string></array>
			<key>CFBundleTypeRole</key>       <string>Editor</string>
		</dict>
	</array>
</dict>
</plist>
PLIST

	# The bundle links against the Homebrew libraries in place: it runs on this
	# machine, but is not self-contained. Ship it elsewhere only after copying
	# the dylibs in (e.g. with dylibbundler).
	codesign --force --deep --sign - "$APP" >/dev/null 2>&1 \
		|| warn "Ad-hoc code signing failed; macOS may refuse to open the bundle"

	info "Created $APP (uses the Homebrew libraries of this machine)"
fi

if [ "$RUN" = 1 ]; then
	info "Starting $TARGET"
	if [ "$MAKE_APP" = 1 ]; then
		open "dist/Tg.app"
	else
		"./$TARGET"
	fi
fi
