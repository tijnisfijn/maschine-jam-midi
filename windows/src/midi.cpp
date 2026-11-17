// Picks between the two MIDI backends (see midi.h) and presents one interface to the driver.

#include "midi.h"

#include "jam.h"
#include "midi_backend.h"

#include <algorithm>
#include <atomic>
#include <cctype>

namespace midi {
namespace {

std::atomic<Backend> chosen{Backend::None};

}  // namespace

const char* backendName(Backend b) {
    switch (b) {
        case Backend::Auto: return "auto";
        case Backend::Virtual: return "virtual";
        case Backend::LoopMidi: return "loopmidi";
        case Backend::None: return "none";
    }
    return "?";
}

Backend backendFromName(const std::string& s, bool* ok) {
    std::string v = s;
    std::transform(v.begin(), v.end(), v.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (ok) *ok = true;
    if (v == "auto") return Backend::Auto;
    if (v == "virtual") return Backend::Virtual;
    if (v == "loopmidi") return Backend::LoopMidi;
    if (ok) *ok = false;
    return Backend::Auto;
}

std::vector<std::string> outPorts() { return winmm::outPorts(); }
std::vector<std::string> inPorts() { return winmm::inPorts(); }

bool open(Backend want, const std::string& name, const std::string& feedbackName,
          Receiver onReceive) {
    // Option A first when the ports are there. It goes through Microsoft's own WinMM against ports
    // the user made, so it brings no third-party licence with it. Option B is nicer to live with
    // (one port, made by the driver, like macOS) but the SDK behind it needs a licence for
    // commercial use, so it is the fallback rather than the default.
    if (want == Backend::Auto || want == Backend::LoopMidi) {
        std::vector<std::string> missing;
        if (winmm::open(name, feedbackName, onReceive, &missing)) {
            chosen = Backend::LoopMidi;
            return true;
        }
        if (want == Backend::LoopMidi) return false;
    }

    if (want == Backend::Auto || want == Backend::Virtual) {
        if (!tevm::available()) {
            if (want == Backend::Virtual)
                jam::log("teVirtualMIDI64.dll is not installed, so the driver cannot make its own port");
            return false;
        }
        std::string error;
        if (tevm::open(name, onReceive, &error)) {
            chosen = Backend::Virtual;
            std::string v = tevm::driverVersion();
            jam::log("MIDI port \"" + name + "\" created (teVirtualMIDI" +
                     (v.empty() ? "" : " " + v) + ")");
            jam::log("  open it as both the MIDI input and the MIDI output in your app");
            jam::log("  note: teVirtualMIDI is free for personal use, but commercial use needs a");
            jam::log("  licence from its author. Two loopMIDI ports named \"" + name + "\" and");
            jam::log("  \"" + feedbackName + "\" avoid that; see windows/README.md.");
            return true;
        }
        if (want == Backend::Virtual) {
            jam::log("could not make the MIDI port: " + error);
            return false;
        }
        jam::log("teVirtualMIDI is there but the port could not be made: " + error);
    }
    return false;
}

void send(const uint8_t* bytes, size_t n) {
    switch (chosen.load()) {
        case Backend::Virtual: tevm::send(bytes, n); break;
        case Backend::LoopMidi: winmm::send(bytes, n); break;
        default: break;
    }
}

bool isOpen() { return chosen.load() != Backend::None; }

Backend backend() { return chosen.load(); }

void close() {
    switch (chosen.load()) {
        case Backend::Virtual: tevm::close(); break;
        case Backend::LoopMidi: winmm::close(); break;
        default: break;
    }
    chosen = Backend::None;
}

}  // namespace midi
