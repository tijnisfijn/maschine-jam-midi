// Option B: the driver creates its own MIDI port, the way the macOS version does.
//
// teVirtualMIDI (Tobias Erichsen) is the driver behind loopMIDI, and loopMIDI installs its DLL.
// We load it at run time, so jam-midi still builds and runs on a machine that does not have it.
// A port made this way shows up to other apps as one device named "Maschine Jam MIDI", both an
// input and an output, exactly like the macOS virtual source and destination.
//
// Licence: the teVirtualMIDI SDK is free for personal use; anything commercial needs a licence
// from its author. See the note in windows/README.md. Nothing of it is bundled here: the DLL is
// only used if the machine already has it, and the driver falls back to loopMIDI ports otherwise.

#include "midi_backend.h"

#include "jam.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <vector>

namespace midi {
namespace tevm {
namespace {

// From teVirtualMIDI.h. The DLL exports these undecorated and Unicode-only.
struct VM_MIDI_PORT;
using LPVM_MIDI_PORT = VM_MIDI_PORT*;
using LPVM_MIDI_DATA_CB = void(CALLBACK*)(LPVM_MIDI_PORT, LPBYTE, DWORD, DWORD_PTR);

using CreatePortEx2Fn = LPVM_MIDI_PORT(CALLBACK*)(LPCWSTR, LPVM_MIDI_DATA_CB, DWORD_PTR, DWORD, DWORD);
using ClosePortFn = void(CALLBACK*)(LPVM_MIDI_PORT);
using SendDataFn = BOOL(CALLBACK*)(LPVM_MIDI_PORT, LPBYTE, DWORD);
using GetDriverVersionFn = LPCWSTR(CALLBACK*)(PWORD, PWORD, PWORD, PWORD);

constexpr DWORD TE_VM_FLAGS_PARSE_RX = 1;
constexpr DWORD TE_VM_FLAGS_INSTANTIATE_BOTH = 12;

HMODULE dll = nullptr;
CreatePortEx2Fn createPort = nullptr;
ClosePortFn closePort = nullptr;
SendDataFn sendData = nullptr;
GetDriverVersionFn getDriverVersion = nullptr;

LPVM_MIDI_PORT port = nullptr;
std::mutex sendMutex;
Receiver receiver;

bool load() {
    if (dll) return true;
    dll = LoadLibraryW(L"teVirtualMIDI64.dll");
    if (!dll) dll = LoadLibraryW(L"teVirtualMIDI.dll");
    if (!dll) return false;
    createPort = reinterpret_cast<CreatePortEx2Fn>(
        reinterpret_cast<void*>(GetProcAddress(dll, "virtualMIDICreatePortEx2")));
    closePort = reinterpret_cast<ClosePortFn>(
        reinterpret_cast<void*>(GetProcAddress(dll, "virtualMIDIClosePort")));
    sendData = reinterpret_cast<SendDataFn>(
        reinterpret_cast<void*>(GetProcAddress(dll, "virtualMIDISendData")));
    getDriverVersion = reinterpret_cast<GetDriverVersionFn>(
        reinterpret_cast<void*>(GetProcAddress(dll, "virtualMIDIGetDriverVersion")));
    if (createPort && closePort && sendData) return true;
    FreeLibrary(dll);
    dll = nullptr;
    return false;
}

// teVirtualMIDI calls this on its own thread, one parsed MIDI command at a time
// (TE_VM_FLAGS_PARSE_RX). It must not block.
void CALLBACK onData(LPVM_MIDI_PORT, LPBYTE bytes, DWORD length, DWORD_PTR) {
    if (receiver && length > 0) receiver(bytes, length);
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

}  // namespace

bool available() { return load(); }

std::string driverVersion() {
    if (!load() || !getDriverVersion) return {};
    WORD major = 0, minor = 0, release = 0, build = 0;
    getDriverVersion(&major, &minor, &release, &build);
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(release) +
           "." + std::to_string(build);
}

bool open(const std::string& name, Receiver onReceive, std::string* error) {
    if (!load()) {
        if (error) *error = "teVirtualMIDI64.dll is not installed";
        return false;
    }
    receiver = std::move(onReceive);
    port = createPort(widen(name).c_str(), onData, 0, 65535,
                      TE_VM_FLAGS_PARSE_RX | TE_VM_FLAGS_INSTANTIATE_BOTH);
    if (!port) {
        DWORD e = GetLastError();
        if (error) {
            *error = e == ERROR_ALREADY_EXISTS
                         ? "a MIDI port called \"" + name +
                               "\" already exists (a loopMIDI port of that name? another copy of "
                               "jam-midi?)"
                         : "could not create the port, error " + std::to_string(e);
        }
        receiver = nullptr;
        return false;
    }
    return true;
}

void send(const uint8_t* bytes, size_t n) {
    std::lock_guard<std::mutex> lock(sendMutex);
    if (!port || n == 0) return;
    if (!sendData(port, const_cast<LPBYTE>(bytes), static_cast<DWORD>(n))) {
        // Say it once: if the port stops taking messages, every control would repeat it.
        static bool complained = false;
        if (!complained) {
            complained = true;
            jam::log("teVirtualMIDI refused a message (error " + std::to_string(GetLastError()) +
                     "). Nothing will reach your app until the port is remade.");
        }
    }
}

void close() {
    std::lock_guard<std::mutex> lock(sendMutex);
    if (port) {
        closePort(port);
        port = nullptr;
    }
    receiver = nullptr;
}

}  // namespace tevm
}  // namespace midi
