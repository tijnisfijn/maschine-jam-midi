// Maschine Jam MIDI: the platform-neutral core of the driver.
//
// This is a port of Sources/Driver/Driver.swift (macOS) with every platform call lifted out into
// two hooks the platform layer fills in: `sendMidi` (driver -> host) and `writeReport` (LED reports
// -> the Jam). Everything here runs on one thread; main.cpp marshals HID and MIDI onto it.
//
// The HID layout (report 0x01 keys, 0x02 strips, LED reports 0x80 / 0x81 / 0x82, the colour
// palette) follows the MIT-licensed cabl library: Copyright (C) 2015 Vincenzo Pacella,
// https://github.com/shaduzlabs/cabl, MIT licence (http://opensource.org/licenses/MIT).
// The MIDI numbers follow page 1 / page A of NI's default Jam template; see MIDI-MAP.md.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace jam {

// ---------------------------------------------------------------- options and logging

extern bool verbose;
extern bool rawMode;

void log(const std::string& s);
std::string hex(const uint8_t* b, size_t n);

// ---------------------------------------------------------------- the Jam's controls

// Non-grid keys: HID bit in report 0x01, MIDI CC, LED byte in report 0x80 (-1: no LED), name.
struct Key {
    int bit;
    uint8_t cc;
    int led;
    const char* name;
};
extern const std::vector<Key> keys;

constexpr uint8_t ccEncoder = 42, ccEncoderRel = 43, ccEncoderPush = 44, ccEncoderTouch = 45;
constexpr uint8_t ccStrip = 48, ccStripTouch = 56, ccLevelL = 76, ccLevelR = 77;

constexpr uint16_t vendorId = 0x17CC, productId = 0x1500;

// Hardware pad (cabl order, 0 = top-left) to MIDI note (NI order, 0 = bottom-left), and back.
uint8_t noteForPad(int p);
int padForNote(int n);

// The colour palette: 0 off, 4 + 4 * hue + level for 16 hues x 4 levels (dim, half, full, pale),
// 68..70 white from dim to bright, 71 ice blue. 1..3 are off and the hardware shows 72..127 as 71.
uint8_t colour(int v);
// The colour a held pad shows: one level brighter, or white when the host gave it none.
uint8_t pressedColour(uint8_t c);

// ---------------------------------------------------------------- hooks the platform layer fills in

// Send a MIDI message to the host (the virtual port's output side).
extern std::function<void(const uint8_t* bytes, size_t n)> sendMidi;
// Write one LED report to the Jam. `data` excludes the report ID.
extern std::function<void(uint8_t reportId, const uint8_t* data, size_t n)> writeReport;

// ---------------------------------------------------------------- driving the core

// The colour test: while on, every LED shows a test pattern instead of what the host set.
void setColourTest(bool on);
bool colourTest();

void render();
void flushLeds(bool force = false);
void ledsOff();

// A MIDI message from the host (3 bytes for channel messages, 2 for program / channel pressure).
void handleMidi(const uint8_t* b, size_t n);
// An input report from the Jam, report ID first.
void handleReport(const uint8_t* r, size_t n);
// Forget which keys were down and where the encoder stood. Call on every (re)connect.
void resetInputState();

}  // namespace jam
