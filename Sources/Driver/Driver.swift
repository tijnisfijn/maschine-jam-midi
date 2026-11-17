// Maschine Jam MIDI: a user-space driver that turns the Native Instruments Maschine Jam into an
// ordinary MIDI device, without Controller Editor. It reads the Jam over USB HID, sends MIDI out of a
// virtual source, and sets every LED from MIDI received on a virtual destination.
//
// This file is the driver itself, shared by the command line tool (Sources/CLI) and the menu bar
// app (Sources/App). Everything runs on the main run loop.
//
// The HID layout (report 0x01 buttons, 0x02 strips, LED reports 0x80 / 0x81 / 0x82, the colour
// palette) follows the MIT-licensed cabl library: Copyright (C) 2015 Vincenzo Pacella,
// https://github.com/shaduzlabs/cabl, MIT licence (http://opensource.org/licenses/MIT).
// The MIDI numbers follow page 1 / page A of NI's default Jam template; see MIDI-MAP.md.

import CoreMIDI
import Foundation
import IOKit.hid

// MARK: - Options (set by the CLI or the app before start())

var verbose = false
var rawMode = false
/// Called on the main thread when the Jam is connected (true) or disconnected (false).
var onConnectionChange: ((Bool) -> Void)?
private(set) var jamConnected = false

func log(_ s: String) {
    let t = DateFormatter.localizedString(from: Date(), dateStyle: .none, timeStyle: .medium)
    print("[\(t)] \(s)")
}

// MARK: - The Jam's controls (cabl bit order in report 0x01, byte 2 + n/8, bit n%8)

enum Ctl {
    case pad(Int)          // 0..63, hardware order (cabl: Pad1 = top-left, row by row)
    case scene(Int)        // 0..7 (the numbered keys above the grid)
    case group(Int)        // 0..7 (A..H)
    case key(Int)          // a key with a single-colour LED or none: index into `keys`
    case encoderPush, encoderTouch
}

/// Non-grid keys: cabl bit, MIDI CC, LED byte in report 0x80 (nil: no LED), name.
struct Key { let bit: Int; let cc: UInt8; let led: Int?; let name: String }
let keys: [Key] = [
    Key(bit: 0, cc: 16, led: 0, name: "Song (Arrange)"),
    Key(bit: 9, cc: 17, led: 1, name: "Step"),
    Key(bit: 10, cc: 18, led: 2, name: "Pad Mode"),
    Key(bit: 11, cc: 19, led: 3, name: "Clear"),
    Key(bit: 12, cc: 20, led: 4, name: "Duplicate"),
    Key(bit: 13, cc: 21, led: 5, name: "Up"),
    Key(bit: 14, cc: 22, led: 6, name: "Left"),
    Key(bit: 15, cc: 23, led: 7, name: "Right"),
    Key(bit: 16, cc: 24, led: 8, name: "Down"),
    Key(bit: 17, cc: 25, led: 9, name: "Note Repeat / Arp"),
    Key(bit: 95, cc: 26, led: 17, name: "Macro"),
    Key(bit: 96, cc: 27, led: 18, name: "Level"),
    Key(bit: 97, cc: 28, led: 19, name: "Aux"),
    Key(bit: 98, cc: 29, led: 20, name: "Control"),
    Key(bit: 99, cc: 30, led: 21, name: "Auto"),
    Key(bit: 94, cc: 31, led: 16, name: "Browse"),
    Key(bit: 100, cc: 32, led: 22, name: "Perform"),
    Key(bit: 101, cc: 33, led: 23, name: "Notes (Variation)"),
    Key(bit: 102, cc: 34, led: 24, name: "Lock"),
    Key(bit: 103, cc: 35, led: 25, name: "Tune"),
    Key(bit: 104, cc: 36, led: 26, name: "Swing"),
    Key(bit: 114, cc: 37, led: 36, name: "Select"),
    Key(bit: 90, cc: 38, led: 10, name: "Mst"),
    Key(bit: 91, cc: 39, led: 11, name: "Grp"),
    Key(bit: 92, cc: 40, led: 12, name: "In 1"),
    Key(bit: 93, cc: 41, led: 14, name: "Headphones (Cue)"),
    Key(bit: 105, cc: 46, led: 27, name: "Shift"),
    Key(bit: 110, cc: 47, led: 32, name: "Tempo"),
    Key(bit: 108, cc: 66, led: 30, name: "Transport Left"),
    Key(bit: 109, cc: 67, led: 31, name: "Transport Right"),
    Key(bit: 106, cc: 68, led: 28, name: "Play"),
    Key(bit: 107, cc: 69, led: 29, name: "Rec"),
    Key(bit: 112, cc: 70, led: 34, name: "Solo"),
    Key(bit: 113, cc: 71, led: 35, name: "Mute"),
    Key(bit: 111, cc: 72, led: 33, name: "Grid"),
]
let ccEncoder: UInt8 = 42, ccEncoderRel: UInt8 = 43, ccEncoderPush: UInt8 = 44, ccEncoderTouch: UInt8 = 45
let ccStrip: UInt8 = 48, ccStripTouch: UInt8 = 56, ccLevelL: UInt8 = 76, ccLevelR: UInt8 = 77

let ctlForBit: [Int: Ctl] = {
    var m = [Int: Ctl]()
    for p in 0..<64 { m[18 + p] = .pad(p) }
    for i in 0..<8 { m[1 + i] = .scene(i); m[82 + i] = .group(i) }
    for (i, k) in keys.enumerated() { m[k.bit] = .key(i) }
    m[115] = .encoderTouch
    m[116] = .encoderPush
    return m
}()
let keyForCC = Dictionary(uniqueKeysWithValues: keys.enumerated().map { ($0.element.cc, $0.offset) })

/// Hardware pad (cabl order, 0 = top-left) to MIDI note (NI order, 0 = bottom-left).
func noteForPad(_ p: Int) -> UInt8 { UInt8((7 - p / 8) * 8 + p % 8) }
func padForNote(_ n: Int) -> Int { (7 - n / 8) * 8 + n % 8 }

// MARK: - LED state

/// The colour palette (cabl): 0 off, 4 + 4 * hue + level for 16 hues x 4 levels
/// (dim, half, full, pale), 68..70 white from dim to bright, 71 ice blue. 1..3 are off and the
/// hardware shows 72..127 as 71 (both checked on a Jam, 17 November 2026).
func colour(_ v: Int) -> UInt8 { UInt8(min(max(v, 0), 71)) }
/// The colour a held pad shows: one level brighter, or white when the host gave it none.
func pressedColour(_ c: UInt8) -> UInt8 {
    if c < 4 { return 70 }
    if c >= 68 { return c < 70 ? 70 : c }
    let level = (Int(c) - 4) % 4
    return level < 2 ? c + UInt8(2 - level) : c
}

var ledButtons = [UInt8](repeating: 0, count: 53)  // report 0x80
var ledPads = [UInt8](repeating: 0, count: 80)     // report 0x81: scenes 0..7, pads 8..71, groups 72..79
var ledStrips = [UInt8](repeating: 0, count: 88)   // report 0x82: 8 strips x 11
var hostKey = [UInt8](repeating: 0, count: keys.count)  // the brightness the host set per key
var hostPad = [UInt8](repeating: 0, count: 80)          // the colour the host set (0x81 layout)
var hostStripValue = [Int](repeating: -1, count: 8)     // last bar value, -1: none
var stripColour = [UInt8](repeating: 0, count: 8)       // 0: use the default
var hostStripLed = [UInt8](repeating: 0, count: 88)     // individual LEDs (channel 2), win over the bar
var hostLevel = [UInt8](repeating: 0, count: 16)        // level meter segments (0x80 bytes 37..52)
var pressed = Set<Int>()                                // cabl bits held down
var dirty = (buttons: true, pads: true, strips: true)

/// The colour test: while on, every LED shows a test pattern instead of what the host set.
var colourTest = false { didSet { render(); flushLeds(force: true) } }

let defaultStripColour: [UInt8] = [6, 14, 18, 30, 38, 46, 54, 58]

func render() {
    for (i, k) in keys.enumerated() {
        guard let led = k.led else { continue }
        let host = colourTest ? 32 : hostKey[i]
        let v = pressed.contains(k.bit) ? UInt8(127) : host
        if ledButtons[led] != v { ledButtons[led] = v; dirty.buttons = true }
    }
    for j in 0..<16 {
        let v = colourTest ? UInt8(j % 8 < 5 ? 127 : 0) : hostLevel[j]
        if ledButtons[37 + j] != v { ledButtons[37 + j] = v; dirty.buttons = true }
    }
    for i in 0..<80 {
        let bit: Int
        switch i {
        case 0..<8: bit = 1 + i
        case 8..<72: bit = 18 + (i - 8)
        default: bit = 82 + (i - 72)
        }
        let host = colourTest ? UInt8(4 + (i % 64)) : hostPad[i]
        let v = pressed.contains(bit) ? pressedColour(host) : host
        if ledPads[i] != v { ledPads[i] = v; dirty.pads = true }
    }
    for s in 0..<8 {
        let c = stripColour[s] == 0 ? defaultStripColour[s] : stripColour[s]
        let value = colourTest ? 127 : hostStripValue[s]
        let lit = value < 0 ? 0 : (value * 11 + 63) / 127
        for j in 0..<11 {
            let idx = s * 11 + j
            let single = colourTest ? 0 : hostStripLed[idx]
            let v = single != 0 ? single : (j < lit ? (colourTest ? defaultStripColour[s] : c) : 0)
            if ledStrips[idx] != v { ledStrips[idx] = v; dirty.strips = true }
        }
    }
}

// MARK: - MIDI

private var client = MIDIClientRef(), source = MIDIEndpointRef(), dest = MIDIEndpointRef()

func send(_ bytes: [UInt8]) {
    var list = MIDIPacketList()
    let pkt = MIDIPacketListInit(&list)
    _ = MIDIPacketListAdd(&list, MemoryLayout<MIDIPacketList>.size, pkt, 0, bytes.count, bytes)
    MIDIReceived(source, &list)
    if verbose { log("out  " + bytes.map { String(format: "%02X", $0) }.joined(separator: " ")) }
}
func cc(_ n: UInt8, _ v: UInt8, ch: UInt8 = 0) { send([0xB0 | ch, n, v]) }

func handleMIDI(_ b: [UInt8]) {
    guard b.count >= 3, b[0] >= 0x80, b[0] < 0xF0 else { return }
    let status = b[0] & 0xF0, ch = b[0] & 0x0F, n = Int(b[1]), v = b[2]
    if verbose { log("in   " + b.map { String(format: "%02X", $0) }.joined(separator: " ")) }
    let on = status == 0x90 ? v : (status == 0x80 ? 0 : nil)
    DispatchQueue.main.async {
        if ch == 0 {
            if let v = on, n < 64 { hostPad[8 + padForNote(n)] = colour(Int(v)) }
            if status == 0xB0 {
                switch UInt8(n) {
                case 0...7: hostPad[n] = colour(Int(v))
                case 8...15: hostPad[72 + n - 8] = colour(Int(v))
                case ccStrip...(ccStrip + 7): hostStripValue[n - Int(ccStrip)] = Int(v)
                case ccLevelL, ccLevelR:
                    let base = UInt8(n) == ccLevelL ? 0 : 8, lit = (Int(v) * 8 + 63) / 127
                    for j in 0..<8 { hostLevel[base + j] = j < lit ? 127 : 0 }
                case ccEncoder: encoderAbs = Int(v)
                default: if let k = keyForCC[UInt8(n)] { hostKey[k] = v }
                }
            }
        } else if ch == 1 {
            // Channel 2: the touch strips' LEDs one by one (notes 0..87, strip s LED j = 11 * s + j,
            // velocity = colour) and each strip's bar colour (CC 48..55).
            if let v = on, n < 88 { hostStripLed[n] = colour(Int(v)) }
            if status == 0xB0, (Int(ccStrip)..<Int(ccStrip) + 8).contains(n) { stripColour[n - Int(ccStrip)] = colour(Int(v)) }
        }
        render(); flushLeds()
    }
}

private func setUpMIDI(_ portName: String) {
    MIDIClientCreateWithBlock(portName as CFString, &client) { note in
        if note.pointee.messageID == .msgSetupChanged, verbose { log("MIDI setup changed") }
    }
    MIDISourceCreate(client, portName as CFString, &source)
    MIDIDestinationCreateWithBlock(client, portName as CFString, &dest) { list, _ in
        for packet in list.unsafeSequence() {
            let len = Int(packet.pointee.length)
            let bytes = withUnsafeBytes(of: packet.pointee.data) { Array($0.prefix(len)) }
            // split running messages (3-byte channel messages, 2-byte for program / channel pressure)
            var i = 0
            while i < bytes.count {
                let s = bytes[i]
                if s >= 0xF0 { i += 1; continue }
                let len = (s & 0xE0) == 0xC0 ? 2 : 3
                if i + len <= bytes.count { handleMIDI(Array(bytes[i..<i + len])) }
                i += len
            }
        }
    }
    // Stable IDs so apps (Arena) recognise the ports after a restart.
    MIDIObjectSetIntegerProperty(source, kMIDIPropertyUniqueID, 0x4A414D01)
    MIDIObjectSetIntegerProperty(dest, kMIDIPropertyUniqueID, 0x4A414D02)
    log("MIDI ports ready: \"\(portName)\" (in and out)")
}

// MARK: - HID

private var device: IOHIDDevice?
private let inBuf = UnsafeMutablePointer<UInt8>.allocate(capacity: 256)
private var lastButtons = [UInt8](repeating: 0, count: 15)
private var lastEncoder = -1
var encoderAbs = 64
private var stripTouched = [Bool](repeating: false, count: 8), stripLast = [Int](repeating: -1, count: 8)

private func setReport(_ id: UInt8, _ data: [UInt8]) {
    guard let d = device else { return }
    let r = IOHIDDeviceSetReport(d, kIOHIDReportTypeOutput, CFIndex(id), [id] + data, data.count + 1)
    if r != kIOReturnSuccess, verbose { log(String(format: "LED write %02X failed: %08X", id, r)) }
}
func flushLeds(force: Bool = false) {
    if dirty.buttons || force { setReport(0x80, ledButtons); dirty.buttons = false }
    if dirty.pads || force { setReport(0x81, ledPads); dirty.pads = false }
    if dirty.strips || force { setReport(0x82, ledStrips); dirty.strips = false }
}

private func controlChanged(bit: Int, down: Bool) {
    if down { pressed.insert(bit) } else { pressed.remove(bit) }
    let v: UInt8 = down ? 127 : 0
    guard let c = ctlForBit[bit] else {
        log("unknown key, bit \(bit) \(down ? "down" : "up")"); return
    }
    switch c {
    case .pad(let p): send([down ? 0x90 : 0x80, noteForPad(p), down ? 127 : 0])
    case .scene(let i): cc(UInt8(i), v)
    case .group(let i): cc(UInt8(8 + i), v)
    case .key(let k): cc(keys[k].cc, v)
    case .encoderPush: cc(ccEncoderPush, v)
    case .encoderTouch: cc(ccEncoderTouch, v)
    }
    if verbose { log("\(describe(c)) \(down ? "down" : "up")") }
}
func describe(_ c: Ctl) -> String {
    switch c {
    case .pad(let p): return "pad row \(p / 8 + 1) col \(p % 8 + 1) (note \(noteForPad(p)))"
    case .scene(let i): return "scene \(i + 1)"
    case .group(let i): return "group \(["A","B","C","D","E","F","G","H"][i])"
    case .key(let k): return keys[k].name
    case .encoderPush: return "encoder push"
    case .encoderTouch: return "encoder touch"
    }
}

private func handleReport(_ r: [UInt8]) {
    guard let id = r.first else { return }
    if rawMode { log(r.map { String(format: "%02x", $0) }.joined(separator: " ")) }
    if id == 0x01, r.count >= 17 {
        let enc = Int(r[1] & 0x0F)
        if lastEncoder >= 0, enc != lastEncoder {
            var d = enc - lastEncoder
            if d > 8 { d -= 16 } else if d < -8 { d += 16 }
            encoderAbs = min(127, max(0, encoderAbs + d))
            cc(ccEncoder, UInt8(encoderAbs))
            for _ in 0..<abs(d) { cc(ccEncoderRel, d > 0 ? 1 : 127) }
            if verbose { log("encoder \(d > 0 ? "+" : "")\(d) -> \(encoderAbs)") }
        }
        lastEncoder = enc
        for i in 0..<15 where r[2 + i] != lastButtons[i] {
            let changed = r[2 + i] ^ lastButtons[i]
            for b in 0..<8 where changed & (1 << b) != 0 {
                controlChanged(bit: i * 8 + b, down: r[2 + i] & (1 << b) != 0)
            }
            lastButtons[i] = r[2 + i]
        }
        render(); flushLeds()
    } else if id == 0x02, r.count >= 49 {
        for s in 0..<8 {
            let o = 1 + s * 6
            let pos = Int(r[o + 2]) | Int(r[o + 3]) << 8
            let touched = pos != 0
            if touched != stripTouched[s] {
                stripTouched[s] = touched
                cc(ccStripTouch + UInt8(s), touched ? 127 : 0)
                if verbose { log("strip \(s + 1) \(touched ? "touched" : "released")") }
            }
            if touched {
                let v = min(127, pos * 128 / 1024)
                if v != stripLast[s] {
                    stripLast[s] = v
                    cc(ccStrip + UInt8(s), UInt8(v))
                    if verbose { log("strip \(s + 1) = \(v) (raw \(pos))") }
                }
            }
        }
    }
}

private func attach(_ d: IOHIDDevice) {
    device = d
    lastButtons = [UInt8](repeating: 0, count: 15); lastEncoder = -1; pressed.removeAll()
    IOHIDDeviceRegisterInputReportCallback(d, inBuf, 256, { _, _, _, _, _, rep, len in
        handleReport(Array(UnsafeBufferPointer(start: rep, count: len)))
    }, nil)
    render(); flushLeds(force: true)
    jamConnected = true
    log("Maschine Jam connected")
    onConnectionChange?(true)
}

private let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))

/// Creates the MIDI ports and starts watching for the Jam. Call once, on the main thread.
func startDriver(portName: String = "Maschine Jam MIDI") {
    setUpMIDI(portName)
    IOHIDManagerSetDeviceMatching(manager, [kIOHIDVendorIDKey: 0x17CC, kIOHIDProductIDKey: 0x1500] as CFDictionary)
    IOHIDManagerRegisterDeviceMatchingCallback(manager, { _, _, _, d in attach(d) }, nil)
    IOHIDManagerRegisterDeviceRemovalCallback(manager, { _, _, _, _ in
        device = nil; jamConnected = false
        log("Maschine Jam disconnected, waiting for it")
        onConnectionChange?(false)
    }, nil)
    IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetMain(), CFRunLoopMode.defaultMode.rawValue)
    let r = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
    if r != kIOReturnSuccess { log(String(format: "could not open USB HID: %08X", r)) }
    log("waiting for the Maschine Jam")
    // Refresh the LEDs now and then: NI's agent or a replug can overwrite them.
    Timer.scheduledTimer(withTimeInterval: 2.0, repeats: true) { _ in flushLeds(force: true) }
}

/// Turns every LED off (on quit).
func ledsOff() {
    for i in ledButtons.indices { ledButtons[i] = 0 }
    for i in ledPads.indices { ledPads[i] = 0 }
    for i in ledStrips.indices { ledStrips[i] = 0 }
    flushLeds(force: true)
}
