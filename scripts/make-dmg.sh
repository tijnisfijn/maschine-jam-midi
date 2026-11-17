#!/bin/bash
# Builds the app and puts it in build/Maschine-Jam-MIDI-<version>.dmg, with an Applications link
# to drag it to. Usage: scripts/make-dmg.sh [version]
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh "$@"
VERSION=$(defaults read "$PWD/build/Maschine Jam MIDI.app/Contents/Info" CFBundleShortVersionString)
STAGE=build/obj/dmg
rm -rf "$STAGE"; mkdir -p "$STAGE"
ditto "build/Maschine Jam MIDI.app" "$STAGE/Maschine Jam MIDI.app"
ln -s /Applications "$STAGE/Applications"
cp README.md MIDI-MAP.md LICENSE NOTICE "$STAGE/"
DMG="build/Maschine-Jam-MIDI-$VERSION.dmg"
rm -f "$DMG"
hdiutil create -volname "Maschine Jam MIDI $VERSION" -srcfolder "$STAGE" -ov -format UDZO "$DMG" > /dev/null
echo "Made $DMG"
