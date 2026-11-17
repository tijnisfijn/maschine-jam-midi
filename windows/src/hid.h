// USB HID access to the Maschine Jam. One background thread opens the device, reads input reports
// and picks the Jam up again after a replug; LED reports are written from the calling thread.
//
// The callbacks run on the HID thread, so main.cpp marshals them onto the main loop.

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

namespace hid {

// Starts watching for the Jam. `onReport` gets every input report (report ID first),
// `onConnection` gets true on connect and false on disconnect. Both run on the HID thread.
void start(std::function<void(const uint8_t*, size_t)> onReport,
           std::function<void(bool)> onConnection);

// Writes one output report. `data` excludes the report ID; it is padded to the length the
// device reports. Safe to call from another thread; returns false when the Jam is not there.
bool write(uint8_t reportId, const uint8_t* data, size_t n);

bool connected();

// Stops the thread and closes the device.
void stop();

}  // namespace hid
