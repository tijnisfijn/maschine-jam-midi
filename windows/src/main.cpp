// jam-midi: the Maschine Jam as an ordinary MIDI device on Windows, without Controller Editor.
//
// The HID thread and the MIDI callback thread both push events into one queue; everything that
// touches the driver's state runs on this thread, which is what the macOS version gets from the
// main run loop.

#include "hid.h"
#include "jam.h"
#include "midi.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct Event {
    enum Kind { Report, MidiIn, Connected, Disconnected } kind;
    std::vector<uint8_t> data;
};

std::deque<Event> queue;
std::mutex queueMutex;
std::condition_variable queueSignal;
std::atomic<bool> running{true};

void push(Event e) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        queue.push_back(std::move(e));
    }
    queueSignal.notify_one();
}

BOOL WINAPI consoleHandler(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        running = false;
        queueSignal.notify_all();
        if (type == CTRL_CLOSE_EVENT) Sleep(800);  // give the main thread a moment to turn the LEDs off
        return TRUE;
    }
    return FALSE;
}

void usage() {
    std::printf(
        "jam-midi: the Maschine Jam as an ordinary MIDI device (no Controller Editor)\n"
        "  -v, --verbose      print every control and every message\n"
        "  --name NAME        name of the MIDI port (default \"Maschine Jam MIDI\")\n"
        "  --feedback NAME    loopMIDI only: the port LED messages come from\n"
        "                     (default: the name above plus \" Feedback\")\n"
        "  --midi-backend B   auto (default), virtual (the driver makes its own port\n"
        "                     through teVirtualMIDI) or loopmidi (a pair of loopMIDI ports)\n"
        "  --demo             light every LED with a test pattern\n"
        "  --raw              print raw HID input reports (for working out the protocol)\n"
        "  --list-ports       print the MIDI ports Windows knows about and quit\n"
        "  --no-midi          do not open any MIDI port (HID and LEDs only)\n"
        "  --no-refresh       do not rewrite all the LEDs every 2 s (to track down flicker)\n");
}

std::string argAfter(const std::vector<std::string>& args, const std::string& flag) {
    for (size_t i = 0; i + 1 < args.size(); i++) {
        if (args[i] == flag) return args[i + 1];
    }
    return {};
}

bool has(const std::vector<std::string>& args, const std::string& flag) {
    for (const auto& a : args) {
        if (a == flag) return true;
    }
    return false;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv, argv + argc);

    if (has(args, "-h") || has(args, "--help")) {
        usage();
        return 0;
    }
    if (has(args, "--list-ports")) {
        std::printf("MIDI outputs (the driver writes the Jam's controls to one of these):\n");
        for (const auto& n : midi::outPorts()) std::printf("  %s\n", n.c_str());
        std::printf("MIDI inputs (the driver reads LED messages from one of these):\n");
        for (const auto& n : midi::inPorts()) std::printf("  %s\n", n.c_str());
        return 0;
    }

    jam::verbose = has(args, "-v") || has(args, "--verbose");
    jam::rawMode = has(args, "--raw");

    std::string name = argAfter(args, "--name");
    if (name.empty()) name = "Maschine Jam MIDI";
    std::string feedback = argAfter(args, "--feedback");
    if (feedback.empty()) feedback = name + " Feedback";

    SetConsoleCtrlHandler(consoleHandler, TRUE);

    // Wire the core to the platform.
    jam::writeReport = [](uint8_t id, const uint8_t* data, size_t n) { hid::write(id, data, n); };
    jam::sendMidi = [](const uint8_t* b, size_t n) { midi::send(b, n); };

    midi::Backend wantBackend = midi::Backend::Auto;
    std::string backendArg = argAfter(args, "--midi-backend");
    if (!backendArg.empty()) {
        bool ok = false;
        wantBackend = midi::backendFromName(backendArg, &ok);
        if (!ok) {
            std::printf("unknown --midi-backend \"%s\": use auto, virtual or loopmidi\n",
                        backendArg.c_str());
            return 1;
        }
    }

    const bool wantMidi = !has(args, "--no-midi");
    bool midiComplaint = false;

    // The port may not be makeable yet: loopMIDI can start after us at logon, or its ports may
    // still have to be made. Keep trying, so the driver need not be restarted by hand.
    auto tryOpenMidi = [&] {
        if (midi::open(wantBackend, name, feedback, [](const uint8_t* b, size_t n) {
                push({Event::MidiIn, std::vector<uint8_t>(b, b + n)});
            })) {
            return true;
        }
        if (!midiComplaint) {
            midiComplaint = true;
            jam::log("no MIDI port yet. Windows cannot make one by itself, so the driver needs one");
            jam::log("of these, and keeps looking until it finds one:");
            jam::log("  teVirtualMIDI (installed with loopMIDI): the driver makes \"" + name + "\" itself");
            jam::log("  or two loopMIDI ports named \"" + name + "\" and \"" + feedback + "\"");
            jam::log("See windows/README.md. The Jam and its LEDs work meanwhile.");
        }
        return false;
    };

    if (!wantMidi) {
        jam::log("running without MIDI (--no-midi): HID and LEDs only");
    } else {
        tryOpenMidi();
    }

    hid::start([](const uint8_t* r, size_t n) { push({Event::Report, std::vector<uint8_t>(r, r + n)}); },
               [](bool up) { push({up ? Event::Connected : Event::Disconnected, {}}); });

    const bool refresh = !has(args, "--no-refresh");
    if (!refresh) jam::log("the 2 s LED refresh is off (--no-refresh)");

    if (has(args, "--demo")) jam::setColourTest(true);
    jam::log("waiting for the Maschine Jam");
    jam::log("Ctrl-C to quit");

    auto lastRefresh = std::chrono::steady_clock::now();
    auto lastMidiTry = lastRefresh;
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueSignal.wait_for(lock, std::chrono::milliseconds(200), [] { return !queue.empty() || !running; });
        std::deque<Event> batch;
        batch.swap(queue);
        lock.unlock();

        for (auto& e : batch) {
            switch (e.kind) {
                case Event::Report: jam::handleReport(e.data.data(), e.data.size()); break;
                case Event::MidiIn: jam::handleMidi(e.data.data(), e.data.size()); break;
                case Event::Connected:
                    jam::resetInputState();
                    jam::log("Maschine Jam connected");
                    jam::render();
                    jam::flushLeds(true);
                    break;
                case Event::Disconnected:
                    jam::log("Maschine Jam disconnected, waiting for it");
                    break;
            }
        }

        // Refresh the LEDs now and then: NI's agent or a replug can overwrite them.
        auto now = std::chrono::steady_clock::now();
        if (wantMidi && !midi::isOpen() && now - lastMidiTry >= std::chrono::seconds(2)) {
            lastMidiTry = now;
            tryOpenMidi();
        }

        if (refresh && now - lastRefresh >= std::chrono::seconds(2)) {
            lastRefresh = now;
            if (hid::connected()) jam::flushLeds(true);
        }
    }

    jam::log("quitting, LEDs off");
    jam::ledsOff();
    hid::stop();
    midi::close();
    return 0;
}
