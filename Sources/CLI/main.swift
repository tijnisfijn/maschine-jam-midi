// jam-midi: the command line version of the driver (Sources/Driver).

import Foundation

setvbuf(stdout, nil, _IOLBF, 0)

let args = CommandLine.arguments
if args.contains("-h") || args.contains("--help") {
    print("""
    jam-midi: the Maschine Jam as an ordinary MIDI device (no Controller Editor)
      -v, --verbose      print every control and every message
      --name NAME        name of the MIDI ports (default "Maschine Jam MIDI")
      --demo             light every LED with a test pattern
      --raw              print raw HID input reports (for working out the protocol)
    """)
    exit(0)
}
verbose = args.contains("-v") || args.contains("--verbose")
rawMode = args.contains("--raw")
var name = "Maschine Jam MIDI"
if let i = args.firstIndex(of: "--name"), i + 1 < args.count { name = args[i + 1] }

startDriver(portName: name)
if args.contains("--demo") { colourTest = true }
log("Ctrl-C to quit")
signal(SIGINT) { _ in ledsOff(); exit(0) }
signal(SIGTERM) { _ in ledsOff(); exit(0) }
CFRunLoopRun()
