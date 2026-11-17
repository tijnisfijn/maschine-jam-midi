// jam-send: send MIDI to a destination by name. Usage: jam-send "Maschine Jam MIDI" B0 4C 7F [90 00 06 ...]
import CoreMIDI
import Foundation
let a = CommandLine.arguments
guard a.count >= 3 else { print("usage: jam-send DEST HEXBYTES..."); exit(1) }
var client = MIDIClientRef(), port = MIDIPortRef()
MIDIClientCreate("jam-send" as CFString, nil, nil, &client)
MIDIOutputPortCreate(client, "out" as CFString, &port)
var dest: MIDIEndpointRef = 0
for i in 0..<MIDIGetNumberOfDestinations() {
  let d = MIDIGetDestination(i); var n: Unmanaged<CFString>?
  MIDIObjectGetStringProperty(d, kMIDIPropertyName, &n)
  if (n?.takeRetainedValue() as String?) == a[1] { dest = d }
}
guard dest != 0 else { print("no destination named \(a[1])"); exit(1) }
let bytes = a.dropFirst(2).compactMap { UInt8($0, radix: 16) }
var list = MIDIPacketList(); let p = MIDIPacketListInit(&list)
_ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, p, 0, bytes.count, bytes)
MIDISend(port, dest, &list); usleep(50_000)
