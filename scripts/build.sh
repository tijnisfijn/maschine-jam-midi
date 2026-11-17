#!/bin/bash
# Builds everything into build/: the app (build/Maschine Jam MIDI.app) and the command line tools
# (build/jam-midi, build/jam-send, build/strip-test). Universal: Apple Silicon and Intel.
# Needs only Apple's command line tools. Usage: scripts/build.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
VERSION="${1:-$(git describe --tags --abbrev=0 2>/dev/null | sed 's/^v//' || echo 0.0.0)}"
[ -n "$VERSION" ] || VERSION=0.0.0
mkdir -p build/obj
universal() { # out min-macos sources...
    local out=$1 min=$2; shift 2
    for a in arm64 x86_64; do
        swiftc -O -swift-version 5 -target "$a-apple-macos$min" "$@" -o "build/obj/$(basename "$out")-$a"
    done
    lipo -create "build/obj/$(basename "$out")-arm64" "build/obj/$(basename "$out")-x86_64" -output "$out"
}
universal build/jam-midi 11 Sources/Driver/Driver.swift Sources/CLI/main.swift
universal build/jam-send 11 Tools/send.swift
universal build/strip-test 11 Tools/strip-test.swift

APP="build/Maschine Jam MIDI.app"
rm -rf "$APP"
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
universal "$APP/Contents/MacOS/Maschine Jam MIDI" 13 Sources/Driver/Driver.swift Sources/App/main.swift

ICONSET=build/obj/AppIcon.iconset
rm -rf "$ICONSET"; mkdir -p "$ICONSET"
for s in 16 32 128 256 512; do
    sips -z $s $s assets/icon.png --out "$ICONSET/icon_${s}x${s}.png" > /dev/null
    sips -z $((s*2)) $((s*2)) assets/icon.png --out "$ICONSET/icon_${s}x${s}@2x.png" > /dev/null
done
iconutil -c icns "$ICONSET" -o "$APP/Contents/Resources/AppIcon.icns"

cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key><string>Maschine Jam MIDI</string>
    <key>CFBundleDisplayName</key><string>Maschine Jam MIDI</string>
    <key>CFBundleIdentifier</key><string>io.github.tijnisfijn.maschine-jam-midi</string>
    <key>CFBundleExecutable</key><string>Maschine Jam MIDI</string>
    <key>CFBundleIconFile</key><string>AppIcon</string>
    <key>CFBundlePackageType</key><string>APPL</string>
    <key>CFBundleShortVersionString</key><string>$VERSION</string>
    <key>CFBundleVersion</key><string>$VERSION</string>
    <key>LSMinimumSystemVersion</key><string>13.0</string>
    <key>LSUIElement</key><true/>
    <key>LSMultipleInstancesProhibited</key><true/>
    <key>NSHumanReadableCopyright</key><string>MIT licence. Not made by or connected to Native Instruments.</string>
</dict>
</plist>
PLIST
codesign --force --deep --sign - "$APP"
for t in build/jam-midi build/jam-send build/strip-test; do codesign --force --sign - "$t"; done
echo "Built $APP ($VERSION) and build/jam-midi"
