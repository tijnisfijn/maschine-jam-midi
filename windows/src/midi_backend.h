// Internal: the two MIDI backends midi.cpp picks between. Not part of the driver's interface.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace midi {

using Receiver = std::function<void(const uint8_t*, size_t)>;

// Option B: the driver creates its own port through teVirtualMIDI (loaded at run time).
namespace tevm {
// True when teVirtualMIDI64.dll is present and loadable.
bool available();
// The driver version, for the log. Empty when unavailable.
std::string driverVersion();
bool open(const std::string& name, Receiver onReceive, std::string* error);
void send(const uint8_t* bytes, size_t n);
void close();
}  // namespace tevm

// Option A: a pair of ports made in loopMIDI, opened through WinMM.
namespace winmm {
std::vector<std::string> outPorts();
std::vector<std::string> inPorts();
bool open(const std::string& outName, const std::string& inName, Receiver onReceive,
          std::vector<std::string>* missing);
void send(const uint8_t* bytes, size_t n);
void close();
}  // namespace winmm

}  // namespace midi
