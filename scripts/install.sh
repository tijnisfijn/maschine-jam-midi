#!/bin/bash
# Builds Maschine Jam MIDI.app and installs it in /Applications, then starts it.
# The first start adds it to the login items (switch it off in its menu).
set -euo pipefail
cd "$(dirname "$0")/.."
scripts/build.sh "$@"
pkill -x jam-midi 2> /dev/null || true          # the command line version would make a second port
osascript -e 'quit app "Maschine Jam MIDI"' 2> /dev/null || true
sleep 1
rm -rf "/Applications/Maschine Jam MIDI.app"
ditto "build/Maschine Jam MIDI.app" "/Applications/Maschine Jam MIDI.app"
open "/Applications/Maschine Jam MIDI.app"
echo "Installed: /Applications/Maschine Jam MIDI.app (look for the grid icon in the menu bar)"
