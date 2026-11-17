// The MIDI side of the driver, behind a small interface so the backend can be swapped.
//
// macOS lets a program create its own virtual MIDI port (MIDISourceCreate). Classic Windows MIDI
// does not, so there are two ways to get one, both from WINDOWS.md:
//
//   Virtual   (option B) the driver creates one port itself through teVirtualMIDI, exactly like
//             macOS: a single port named "Maschine Jam MIDI" that apps open for in and out.
//             Needs teVirtualMIDI64.dll, which loopMIDI installs. Loaded at run time, so the
//             driver still builds and runs without it. See the licence note in windows/README.md.
//
//   LoopMidi  (option A) the driver borrows a pair of ports someone made in loopMIDI:
//               name          the driver writes the Jam's controls here; your app opens it as an input
//               feedbackName  your app writes LED messages here; the driver opens it as an input
//             Two ports, because a loopMIDI port loops straight back and one port would make the
//             driver hear its own output.
//
// Auto takes Virtual when it can and falls back to LoopMidi.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace midi {

enum class Backend { Auto, Virtual, LoopMidi, None };

Backend backendFromName(const std::string& s, bool* ok);
const char* backendName(Backend b);

// Every MIDI output / input port Windows knows about right now.
std::vector<std::string> outPorts();
std::vector<std::string> inPorts();

// Opens the MIDI side. `onReceive` runs on a callback thread, so marshal it.
bool open(Backend want, const std::string& name, const std::string& feedbackName,
          std::function<void(const uint8_t*, size_t)> onReceive);

void send(const uint8_t* bytes, size_t n);

bool isOpen();

// Which backend actually opened, or None.
Backend backend();

void close();

}  // namespace midi
