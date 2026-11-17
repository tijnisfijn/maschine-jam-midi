// strip-test PASS: writes raw values to the strip LEDs (report 0x82), everything else off.
// Stop jam-midi first. Pass 1/2: strip s = hue 2s (+1 for pass 2), LEDs bottom-up dim,dim,half,half,full,full,pale,pale.
// Pass 3: raw values 72..127 from strip 1 LED 1 upward, then strip 6 LED 2..4 = 1,2,3; strip 8 = 71 (reference white).
import Foundation
import IOKit.hid
let pass = Int(CommandLine.arguments.dropFirst().first ?? "1") ?? 1
var s = [UInt8](repeating: 0, count: 88)
if pass <= 2 {
  for st in 0..<8 { let hue = st * 2 + (pass - 1)
    for j in 0..<8 { s[st * 11 + j] = UInt8(4 + hue * 4 + j / 2) } }
} else {
  for v in 72...127 { s[v - 72] = UInt8(v) }   // 56 values: strips 1-5 full, strip 6 LED 1
  s[5 * 11 + 1] = 1; s[5 * 11 + 2] = 2; s[5 * 11 + 3] = 3
  for j in 0..<11 { s[7 * 11 + j] = 71 }
}
let m = IOHIDManagerCreate(kCFAllocatorDefault, 0)
IOHIDManagerSetDeviceMatching(m, [kIOHIDVendorIDKey: 0x17CC, kIOHIDProductIDKey: 0x1500] as CFDictionary)
IOHIDManagerOpen(m, 0)
guard let d = (IOHIDManagerCopyDevices(m) as? Set<IOHIDDevice>)?.first else { print("no Jam"); exit(1) }
func put(_ id: UInt8, _ b: [UInt8]) { print(String(format: "%02X", id), IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, CFIndex(id), [id] + b, b.count + 1)) }
put(0x80, [UInt8](repeating: 0, count: 53)); put(0x81, [UInt8](repeating: 0, count: 80)); put(0x82, s)
