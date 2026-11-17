// jam-listen: open a MIDI input by name and print what arrives, decoded back into the Jam's
// controls. The other half of jam-send, and the way to check the driver's output without a DAW.
//
//   jam-listen                          list the MIDI inputs
//   jam-listen "Maschine Jam MIDI"      listen for 30 seconds
//   jam-listen "Maschine Jam MIDI" 120  listen for 120 seconds (0 = until Ctrl-C)
//
// Run the driver first, then this, then press things on the Jam. The names it prints come from the
// same table the driver sends from (jam.h), so what you see here is MIDI-MAP.md read back.

#include "../src/jam.h"

#include <windows.h>
// clang-format off
#include <mmsystem.h>
// clang-format on

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "winmm.lib")

namespace {

struct Message {
    uint8_t bytes[3];
};

std::deque<Message> queue;
std::mutex queueMutex;
std::atomic<bool> running{true};

void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR param1, DWORD_PTR) {
    if (msg != MIM_DATA) return;
    Message m{{static_cast<uint8_t>(param1 & 0xFF), static_cast<uint8_t>((param1 >> 8) & 0x7F),
               static_cast<uint8_t>((param1 >> 16) & 0x7F)}};
    std::lock_guard<std::mutex> lock(queueMutex);
    queue.push_back(m);
}

BOOL WINAPI consoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT) {
        running = false;
        return TRUE;
    }
    return FALSE;
}

std::vector<std::string> inPorts() {
    std::vector<std::string> names;
    for (UINT i = 0; i < midiInGetNumDevs(); i++) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR) names.push_back(caps.szPname);
    }
    return names;
}

// Turn a message back into the control it came from, using the driver's own table.
std::string describe(const Message& m) {
    const uint8_t status = m.bytes[0] & 0xF0, ch = m.bytes[0] & 0x0F;
    const int n = m.bytes[1], v = m.bytes[2];

    if (status == 0x90 || status == 0x80) {
        const bool down = status == 0x90 && v > 0;
        if (ch == 0 && n < 64) {
            const int p = jam::padForNote(n);
            return "pad row " + std::to_string(p / 8 + 1) + " col " + std::to_string(p % 8 + 1) +
                   (down ? " down" : " up");
        }
        return "note " + std::to_string(n) + (down ? " on" : " off") + ", velocity " + std::to_string(v);
    }

    if (status != 0xB0) return "";

    if (ch == 0) {
        if (n <= 7) return "scene " + std::to_string(n + 1) + (v ? " down" : " up");
        if (n <= 15) return std::string("group ") + "ABCDEFGH"[n - 8] + (v ? " down" : " up");
        if (n == jam::ccEncoder) return "encoder, absolute " + std::to_string(v);
        if (n == jam::ccEncoderRel)
            return std::string("encoder turned ") + (v == 1 ? "right" : "left");
        if (n == jam::ccEncoderPush) return std::string("encoder push") + (v ? " down" : " up");
        if (n == jam::ccEncoderTouch) return std::string("encoder touch") + (v ? " on" : " off");
        if (n >= jam::ccStrip && n <= jam::ccStrip + 7)
            return "strip " + std::to_string(n - jam::ccStrip + 1) + " at " + std::to_string(v);
        if (n >= jam::ccStripTouch && n <= jam::ccStripTouch + 7)
            return "strip " + std::to_string(n - jam::ccStripTouch + 1) + (v ? " touched" : " released");
        for (const auto& k : jam::keys) {
            if (k.cc == n) return std::string(k.name) + (v ? " down" : " up");
        }
    }
    return "CC " + std::to_string(n) + " = " + std::to_string(v);
}

}  // namespace

int main(int argc, char** argv) {
    // Unbuffered, not _IOLBF: the MSVC runtime treats line buffering as full buffering, which
    // would hold everything back until the tool quits. The point of it is to watch messages land.
    setvbuf(stdout, nullptr, _IONBF, 0);

    const auto ports = inPorts();
    if (argc < 2) {
        std::printf("usage: jam-listen PORT [SECONDS]\n\nMIDI inputs:\n");
        for (const auto& p : ports) std::printf("  %s\n", p.c_str());
        if (ports.empty()) std::printf("  (none)\n");
        return 1;
    }

    const std::string wanted = argv[1];
    int seconds = argc > 2 ? std::atoi(argv[2]) : 30;

    int index = -1;
    for (size_t i = 0; i < ports.size(); i++) {
        if (_stricmp(ports[i].c_str(), wanted.c_str()) == 0) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        std::printf("no MIDI input named \"%s\"\n\nMIDI inputs:\n", wanted.c_str());
        for (const auto& p : ports) std::printf("  %s\n", p.c_str());
        return 1;
    }

    HMIDIIN handle = nullptr;
    if (midiInOpen(&handle, static_cast<UINT>(index), reinterpret_cast<DWORD_PTR>(midiInProc), 0,
                   CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        std::printf("could not open \"%s\"\n", ports[index].c_str());
        return 1;
    }
    midiInStart(handle);
    SetConsoleCtrlHandler(consoleHandler, TRUE);

    if (seconds > 0)
        std::printf("listening on \"%s\" for %d seconds, Ctrl-C to stop\n", ports[index].c_str(), seconds);
    else
        std::printf("listening on \"%s\", Ctrl-C to stop\n", ports[index].c_str());

    const auto start = std::chrono::steady_clock::now();
    int count = 0;
    while (running) {
        if (seconds > 0 && std::chrono::steady_clock::now() - start >= std::chrono::seconds(seconds))
            break;

        std::deque<Message> batch;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            batch.swap(queue);
        }
        for (const auto& m : batch) {
            count++;
            const std::string what = describe(m);
            std::printf("%02X %02X %02X%s%s\n", m.bytes[0], m.bytes[1], m.bytes[2],
                        what.empty() ? "" : "   ", what.c_str());
        }
        if (batch.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    midiInStop(handle);
    midiInReset(handle);
    midiInClose(handle);
    std::printf("%d message%s\n", count, count == 1 ? "" : "s");
    return count > 0 ? 0 : 2;
}
