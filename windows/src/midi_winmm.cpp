// Option A: a pair of ports made in loopMIDI, opened through WinMM. Nothing here creates ports;
// see windows/README.md for the setup. Used when teVirtualMIDI is not there, or with
// --midi-backend loopmidi.

#include "midi_backend.h"

#include "jam.h"

#include <windows.h>
// clang-format off
#include <mmsystem.h>
// clang-format on

#include <algorithm>
#include <cctype>
#include <mutex>

namespace midi {
namespace winmm {
namespace {

HMIDIOUT outHandle = nullptr;
HMIDIIN inHandle = nullptr;
std::mutex outMutex;
Receiver receiver;

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// Windows truncates port names to 31 characters, so fall back to a prefix match.
bool nameMatches(const std::string& portName, const std::string& wanted) {
    std::string a = lower(portName), b = lower(wanted);
    if (a == b) return true;
    return !a.empty() && a.size() < b.size() && b.compare(0, a.size(), a) == 0;
}

void CALLBACK midiInProc(HMIDIIN, UINT msg, DWORD_PTR, DWORD_PTR param1, DWORD_PTR) {
    if (msg != MIM_DATA || !receiver) return;
    uint8_t bytes[3] = {static_cast<uint8_t>(param1 & 0xFF),
                        static_cast<uint8_t>((param1 >> 8) & 0x7F),
                        static_cast<uint8_t>((param1 >> 16) & 0x7F)};
    receiver(bytes, 3);
}

}  // namespace

std::vector<std::string> outPorts() {
    std::vector<std::string> names;
    UINT n = midiOutGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        MIDIOUTCAPSA caps{};
        if (midiOutGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR) names.push_back(caps.szPname);
    }
    return names;
}

std::vector<std::string> inPorts() {
    std::vector<std::string> names;
    UINT n = midiInGetNumDevs();
    for (UINT i = 0; i < n; i++) {
        MIDIINCAPSA caps{};
        if (midiInGetDevCapsA(i, &caps, sizeof caps) == MMSYSERR_NOERROR) names.push_back(caps.szPname);
    }
    return names;
}

bool open(const std::string& outName, const std::string& inName, Receiver onReceive,
          std::vector<std::string>* missing) {
    receiver = std::move(onReceive);

    int outIndex = -1, inIndex = -1;
    auto outs = outPorts();
    for (size_t i = 0; i < outs.size(); i++) {
        if (nameMatches(outs[i], outName)) {
            outIndex = static_cast<int>(i);
            break;
        }
    }
    auto ins = inPorts();
    for (size_t i = 0; i < ins.size(); i++) {
        if (nameMatches(ins[i], inName)) {
            inIndex = static_cast<int>(i);
            break;
        }
    }
    if (outIndex < 0 && missing) missing->push_back(outName);
    if (inIndex < 0 && missing) missing->push_back(inName);
    if (outIndex < 0 || inIndex < 0) {
        receiver = nullptr;
        return false;
    }

    if (midiOutOpen(&outHandle, static_cast<UINT>(outIndex), 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        jam::log("could not open the MIDI output \"" + outName + "\"");
        outHandle = nullptr;
        receiver = nullptr;
        return false;
    }
    if (midiInOpen(&inHandle, static_cast<UINT>(inIndex), reinterpret_cast<DWORD_PTR>(midiInProc), 0,
                   CALLBACK_FUNCTION) != MMSYSERR_NOERROR) {
        jam::log("could not open the MIDI input \"" + inName + "\"");
        midiOutClose(outHandle);
        outHandle = nullptr;
        inHandle = nullptr;
        receiver = nullptr;
        return false;
    }
    midiInStart(inHandle);
    jam::log("using loopMIDI ports: out \"" + outs[outIndex] + "\", in \"" + ins[inIndex] + "\"");
    return true;
}

void send(const uint8_t* bytes, size_t n) {
    std::lock_guard<std::mutex> lock(outMutex);
    if (!outHandle || n == 0) return;
    // WinMM short messages only: the driver never sends sysex.
    if (bytes[0] >= 0xF0) return;
    DWORD msg = bytes[0];
    if (n > 1) msg |= static_cast<DWORD>(bytes[1]) << 8;
    if (n > 2) msg |= static_cast<DWORD>(bytes[2]) << 16;
    midiOutShortMsg(outHandle, msg);
}

void close() {
    std::lock_guard<std::mutex> lock(outMutex);
    if (inHandle) {
        midiInStop(inHandle);
        midiInReset(inHandle);
        midiInClose(inHandle);
        inHandle = nullptr;
    }
    if (outHandle) {
        midiOutReset(outHandle);
        midiOutClose(outHandle);
        outHandle = nullptr;
    }
    receiver = nullptr;
}

}  // namespace winmm
}  // namespace midi
