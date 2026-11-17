// The platform-neutral core: tables, LED state, render(), and the two directions of traffic
// (handleReport from the Jam, handleMidi from the host). See jam.h and Sources/Driver/Driver.swift.

#include "jam.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <ctime>
#include <map>
#include <unordered_map>

namespace jam {

bool verbose = false;
bool rawMode = false;

std::function<void(const uint8_t*, size_t)> sendMidi;
std::function<void(uint8_t, const uint8_t*, size_t)> writeReport;

// ---------------------------------------------------------------- logging

void log(const std::string& s) {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &t);
    char stamp[16];
    std::strftime(stamp, sizeof stamp, "%H:%M:%S", &tm);
    std::printf("[%s] %s\n", stamp, s.c_str());
    std::fflush(stdout);
}

std::string hex(const uint8_t* b, size_t n) {
    std::string s;
    char buf[4];
    for (size_t i = 0; i < n; i++) {
        std::snprintf(buf, sizeof buf, "%02X", b[i]);
        if (i) s += ' ';
        s += buf;
    }
    return s;
}

// ---------------------------------------------------------------- the key table

const std::vector<Key> keys = {
    {0, 16, 0, "Song (Arrange)"},
    {9, 17, 1, "Step"},
    {10, 18, 2, "Pad Mode"},
    {11, 19, 3, "Clear"},
    {12, 20, 4, "Duplicate"},
    {13, 21, 5, "Up"},
    {14, 22, 6, "Left"},
    {15, 23, 7, "Right"},
    {16, 24, 8, "Down"},
    {17, 25, 9, "Note Repeat / Arp"},
    {95, 26, 17, "Macro"},
    {96, 27, 18, "Level"},
    {97, 28, 19, "Aux"},
    {98, 29, 20, "Control"},
    {99, 30, 21, "Auto"},
    {94, 31, 16, "Browse"},
    {100, 32, 22, "Perform"},
    {101, 33, 23, "Notes (Variation)"},
    {102, 34, 24, "Lock"},
    {103, 35, 25, "Tune"},
    {104, 36, 26, "Swing"},
    {114, 37, 36, "Select"},
    {90, 38, 10, "Mst"},
    {91, 39, 11, "Grp"},
    {92, 40, 12, "In 1"},
    {93, 41, 14, "Headphones (Cue)"},
    {105, 46, 27, "Shift"},
    {110, 47, 32, "Tempo"},
    {108, 66, 30, "Transport Left"},
    {109, 67, 31, "Transport Right"},
    {106, 68, 28, "Play"},
    {107, 69, 29, "Rec"},
    {112, 70, 34, "Solo"},
    {113, 71, 35, "Mute"},
    {111, 72, 33, "Grid"},
};

// What a HID bit in report 0x01 means.
struct Ctl {
    enum Kind { Pad, Scene, Group, NamedKey, EncoderPush, EncoderTouch } kind;
    int index;  // pad 0..63 (hardware order), scene 0..7, group 0..7, key: index into `keys`
};

static const std::map<int, Ctl>& ctlForBit() {
    static const std::map<int, Ctl> m = [] {
        std::map<int, Ctl> t;
        for (int p = 0; p < 64; p++) t[18 + p] = {Ctl::Pad, p};
        for (int i = 0; i < 8; i++) {
            t[1 + i] = {Ctl::Scene, i};
            t[82 + i] = {Ctl::Group, i};
        }
        for (int i = 0; i < static_cast<int>(keys.size()); i++) t[keys[i].bit] = {Ctl::NamedKey, i};
        t[115] = {Ctl::EncoderTouch, 0};
        t[116] = {Ctl::EncoderPush, 0};
        return t;
    }();
    return m;
}

static const std::unordered_map<uint8_t, int>& keyForCC() {
    static const std::unordered_map<uint8_t, int> m = [] {
        std::unordered_map<uint8_t, int> t;
        for (int i = 0; i < static_cast<int>(keys.size()); i++) t[keys[i].cc] = i;
        return t;
    }();
    return m;
}

uint8_t noteForPad(int p) { return static_cast<uint8_t>((7 - p / 8) * 8 + p % 8); }
int padForNote(int n) { return (7 - n / 8) * 8 + n % 8; }

uint8_t colour(int v) { return static_cast<uint8_t>(std::min(std::max(v, 0), 71)); }

uint8_t pressedColour(uint8_t c) {
    if (c < 4) return 70;
    if (c >= 68) return c < 70 ? 70 : c;
    int level = (static_cast<int>(c) - 4) % 4;
    return level < 2 ? static_cast<uint8_t>(c + (2 - level)) : c;
}

// ---------------------------------------------------------------- LED state

static std::array<uint8_t, 53> ledButtons{};  // report 0x80
static std::array<uint8_t, 80> ledPads{};     // report 0x81: scenes 0..7, pads 8..71, groups 72..79
static std::array<uint8_t, 88> ledStrips{};   // report 0x82: 8 strips x 11

static std::vector<uint8_t> hostKey(keys.size(), 0);  // the brightness the host set per key
static std::array<uint8_t, 80> hostPad{};             // the colour the host set (0x81 layout)
static std::array<int, 8> hostStripValue{};           // last bar value, -1: none
static std::array<uint8_t, 8> stripColour{};          // 0: use the default
static std::array<uint8_t, 88> hostStripLed{};        // individual LEDs (channel 2), win over the bar
static std::array<uint8_t, 16> hostLevel{};           // level meter segments (0x80 bytes 37..52)
static std::array<bool, 128> pressed{};               // HID bits held down

static struct {
    bool buttons = true, pads = true, strips = true;
} dirty;

static bool colourTestOn = false;

static const std::array<uint8_t, 8> defaultStripColour = {6, 14, 18, 30, 38, 46, 54, 58};

static bool stateInitialised = [] {
    hostStripValue.fill(-1);
    return true;
}();

bool colourTest() { return colourTestOn; }

void setColourTest(bool on) {
    colourTestOn = on;
    render();
    flushLeds(true);
}

void render() {
    for (size_t i = 0; i < keys.size(); i++) {
        int led = keys[i].led;
        if (led < 0) continue;
        uint8_t host = colourTestOn ? 32 : hostKey[i];
        uint8_t v = pressed[keys[i].bit] ? 127 : host;
        if (ledButtons[led] != v) {
            ledButtons[led] = v;
            dirty.buttons = true;
        }
    }
    for (int j = 0; j < 16; j++) {
        uint8_t v = colourTestOn ? (j % 8 < 5 ? 127 : 0) : hostLevel[j];
        if (ledButtons[37 + j] != v) {
            ledButtons[37 + j] = v;
            dirty.buttons = true;
        }
    }
    for (int i = 0; i < 80; i++) {
        int bit;
        if (i < 8) bit = 1 + i;
        else if (i < 72) bit = 18 + (i - 8);
        else bit = 82 + (i - 72);
        uint8_t host = colourTestOn ? static_cast<uint8_t>(4 + (i % 64)) : hostPad[i];
        uint8_t v = pressed[bit] ? pressedColour(host) : host;
        if (ledPads[i] != v) {
            ledPads[i] = v;
            dirty.pads = true;
        }
    }
    for (int s = 0; s < 8; s++) {
        uint8_t c = stripColour[s] == 0 ? defaultStripColour[s] : stripColour[s];
        int value = colourTestOn ? 127 : hostStripValue[s];
        int lit = value < 0 ? 0 : (value * 11 + 63) / 127;
        for (int j = 0; j < 11; j++) {
            int idx = s * 11 + j;
            uint8_t single = colourTestOn ? 0 : hostStripLed[idx];
            uint8_t v = single != 0 ? single : (j < lit ? (colourTestOn ? defaultStripColour[s] : c) : 0);
            if (ledStrips[idx] != v) {
                ledStrips[idx] = v;
                dirty.strips = true;
            }
        }
    }
}

void flushLeds(bool force) {
    if (!writeReport) return;
    if (dirty.buttons || force) {
        writeReport(0x80, ledButtons.data(), ledButtons.size());
        dirty.buttons = false;
    }
    if (dirty.pads || force) {
        writeReport(0x81, ledPads.data(), ledPads.size());
        dirty.pads = false;
    }
    if (dirty.strips || force) {
        writeReport(0x82, ledStrips.data(), ledStrips.size());
        dirty.strips = false;
    }
}

void ledsOff() {
    ledButtons.fill(0);
    ledPads.fill(0);
    ledStrips.fill(0);
    flushLeds(true);
}

// ---------------------------------------------------------------- MIDI out

static void send(std::initializer_list<uint8_t> bytes) {
    std::vector<uint8_t> b(bytes);
    if (sendMidi) sendMidi(b.data(), b.size());
    if (verbose) log("out  " + hex(b.data(), b.size()));
}

static void cc(uint8_t n, uint8_t v, uint8_t ch = 0) {
    send({static_cast<uint8_t>(0xB0 | ch), n, v});
}

// ---------------------------------------------------------------- MIDI in (from the host)

static int encoderAbs = 64;

void handleMidi(const uint8_t* b, size_t n) {
    if (n < 3 || b[0] < 0x80 || b[0] >= 0xF0) return;
    uint8_t status = b[0] & 0xF0, ch = b[0] & 0x0F;
    int note = b[1];
    uint8_t v = b[2];
    if (verbose) log("in   " + hex(b, n));

    bool isNote = status == 0x90 || status == 0x80;
    uint8_t on = status == 0x90 ? v : 0;

    if (ch == 0) {
        if (isNote && note < 64) hostPad[8 + padForNote(note)] = colour(on);
        if (status == 0xB0) {
            if (note <= 7) {
                hostPad[note] = colour(v);
            } else if (note <= 15) {
                hostPad[72 + note - 8] = colour(v);
            } else if (note >= ccStrip && note <= ccStrip + 7) {
                hostStripValue[note - ccStrip] = v;
            } else if (note == ccLevelL || note == ccLevelR) {
                int base = note == ccLevelL ? 0 : 8;
                int lit = (static_cast<int>(v) * 8 + 63) / 127;
                for (int j = 0; j < 8; j++) hostLevel[base + j] = j < lit ? 127 : 0;
            } else if (note == ccEncoder) {
                encoderAbs = v;
            } else {
                auto it = keyForCC().find(static_cast<uint8_t>(note));
                if (it != keyForCC().end()) hostKey[it->second] = v;
            }
        }
    } else if (ch == 1) {
        // Channel 2: the touch strips' LEDs one by one (notes 0..87, strip s LED j = 11 * s + j,
        // velocity = colour) and each strip's bar colour (CC 48..55).
        if (isNote && note < 88) hostStripLed[note] = colour(on);
        if (status == 0xB0 && note >= ccStrip && note <= ccStrip + 7) {
            stripColour[note - ccStrip] = colour(v);
        }
    }
    render();
    flushLeds();
}

// ---------------------------------------------------------------- HID in (from the Jam)

static std::array<uint8_t, 15> lastButtons{};
static int lastEncoder = -1;
static std::array<bool, 8> stripTouched{};
static std::array<int, 8> stripLast{};

static bool inputInitialised = [] {
    stripLast.fill(-1);
    return true;
}();

void resetInputState() {
    lastButtons.fill(0);
    lastEncoder = -1;
    pressed.fill(false);
}

static std::string describe(const Ctl& c) {
    switch (c.kind) {
        case Ctl::Pad:
            return "pad row " + std::to_string(c.index / 8 + 1) + " col " +
                   std::to_string(c.index % 8 + 1) + " (note " +
                   std::to_string(noteForPad(c.index)) + ")";
        case Ctl::Scene: return "scene " + std::to_string(c.index + 1);
        case Ctl::Group: return std::string("group ") + "ABCDEFGH"[c.index];
        case Ctl::NamedKey: return keys[c.index].name;
        case Ctl::EncoderPush: return "encoder push";
        case Ctl::EncoderTouch: return "encoder touch";
    }
    return "?";
}

static void controlChanged(int bit, bool down) {
    pressed[bit] = down;
    uint8_t v = down ? 127 : 0;
    auto it = ctlForBit().find(bit);
    if (it == ctlForBit().end()) {
        log("unknown key, bit " + std::to_string(bit) + (down ? " down" : " up"));
        return;
    }
    const Ctl& c = it->second;
    switch (c.kind) {
        case Ctl::Pad:
            send({static_cast<uint8_t>(down ? 0x90 : 0x80), noteForPad(c.index),
                  static_cast<uint8_t>(down ? 127 : 0)});
            break;
        case Ctl::Scene: cc(static_cast<uint8_t>(c.index), v); break;
        case Ctl::Group: cc(static_cast<uint8_t>(8 + c.index), v); break;
        case Ctl::NamedKey: cc(keys[c.index].cc, v); break;
        case Ctl::EncoderPush: cc(ccEncoderPush, v); break;
        case Ctl::EncoderTouch: cc(ccEncoderTouch, v); break;
    }
    if (verbose) log(describe(c) + (down ? " down" : " up"));
}

void handleReport(const uint8_t* r, size_t n) {
    if (n == 0) return;
    if (rawMode) {
        std::string s;
        char buf[4];
        for (size_t i = 0; i < n; i++) {
            std::snprintf(buf, sizeof buf, "%02x", r[i]);
            if (i) s += ' ';
            s += buf;
        }
        log(s);
    }
    if (r[0] == 0x01 && n >= 17) {
        int enc = r[1] & 0x0F;
        if (lastEncoder >= 0 && enc != lastEncoder) {
            int d = enc - lastEncoder;
            if (d > 8) d -= 16;
            else if (d < -8) d += 16;
            encoderAbs = std::min(127, std::max(0, encoderAbs + d));
            cc(ccEncoder, static_cast<uint8_t>(encoderAbs));
            for (int i = 0; i < std::abs(d); i++) cc(ccEncoderRel, d > 0 ? 1 : 127);
            if (verbose)
                log("encoder " + std::string(d > 0 ? "+" : "") + std::to_string(d) + " -> " +
                    std::to_string(encoderAbs));
        }
        lastEncoder = enc;
        for (int i = 0; i < 15; i++) {
            if (r[2 + i] == lastButtons[i]) continue;
            uint8_t changed = r[2 + i] ^ lastButtons[i];
            for (int b = 0; b < 8; b++) {
                if (changed & (1 << b)) controlChanged(i * 8 + b, (r[2 + i] & (1 << b)) != 0);
            }
            lastButtons[i] = r[2 + i];
        }
        render();
        flushLeds();
    } else if (r[0] == 0x02 && n >= 49) {
        for (int s = 0; s < 8; s++) {
            int o = 1 + s * 6;
            int pos = r[o + 2] | (r[o + 3] << 8);
            bool touched = pos != 0;
            if (touched != stripTouched[s]) {
                stripTouched[s] = touched;
                cc(ccStripTouch + static_cast<uint8_t>(s), touched ? 127 : 0);
                if (verbose)
                    log("strip " + std::to_string(s + 1) + (touched ? " touched" : " released"));
            }
            if (touched) {
                int v = std::min(127, pos * 128 / 1024);
                if (v != stripLast[s]) {
                    stripLast[s] = v;
                    cc(ccStrip + static_cast<uint8_t>(s), static_cast<uint8_t>(v));
                    if (verbose)
                        log("strip " + std::to_string(s + 1) + " = " + std::to_string(v) +
                            " (raw " + std::to_string(pos) + ")");
                }
            }
        }
    }
}

}  // namespace jam
