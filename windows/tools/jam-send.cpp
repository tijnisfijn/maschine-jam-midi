// jam-send: send MIDI to an output port by name.
// Usage: jam-send "Maschine Jam MIDI Feedback" B0 4C 7F [90 00 06 ...]
//
// With loopMIDI, send to the *feedback* port: that is the one the driver listens on.

#include <windows.h>
// clang-format off
#include <mmsystem.h>
// clang-format on

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "winmm.lib")

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("usage: jam-send PORT HEXBYTES...\n");
        std::printf("MIDI outputs:\n");
        for (UINT i = 0; i < midiOutGetNumDevs(); i++) {
            MIDIOUTCAPSA caps{};
            if (midiOutGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR)
                std::printf("  %s\n", caps.szPname);
        }
        return 1;
    }

    int index = -1;
    for (UINT i = 0; i < midiOutGetNumDevs(); i++) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof caps) != MMSYSERR_NOERROR) continue;
        if (_stricmp(caps.szPname, argv[1]) == 0) {
            index = static_cast<int>(i);
            break;
        }
    }
    if (index < 0) {
        std::printf("no MIDI output named %s\n", argv[1]);
        return 1;
    }

    std::vector<uint8_t> bytes;
    for (int i = 2; i < argc; i++) bytes.push_back(static_cast<uint8_t>(std::strtol(argv[i], nullptr, 16)));

    HMIDIOUT out = nullptr;
    if (midiOutOpen(&out, static_cast<UINT>(index), 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        std::printf("could not open %s\n", argv[1]);
        return 1;
    }
    // Split the bytes into channel messages: 3 bytes, or 2 for program change / channel pressure.
    for (size_t i = 0; i < bytes.size();) {
        uint8_t status = bytes[i];
        size_t len = (status & 0xE0) == 0xC0 ? 2 : 3;
        if (i + len > bytes.size()) break;
        DWORD msg = bytes[i];
        msg |= static_cast<DWORD>(bytes[i + 1]) << 8;
        if (len > 2) msg |= static_cast<DWORD>(bytes[i + 2]) << 16;
        midiOutShortMsg(out, msg);
        i += len;
    }
    Sleep(50);
    midiOutReset(out);
    midiOutClose(out);
    return 0;
}
