// strip-test PASS: writes raw values to the strip LEDs (report 0x82), everything else off.
// Stop jam-midi first.
// Pass 1/2: strip s = hue 2s (+1 for pass 2), LEDs bottom-up dim,dim,half,half,full,full,pale,pale.
// Pass 3: raw values 72..127 from strip 1 LED 1 upward, then strip 6 LED 2..4 = 1,2,3;
//         strip 8 = 71 (reference white).

#include "../src/hid.h"
#include "../src/jam.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
    int pass = argc > 1 ? std::atoi(argv[1]) : 1;

    std::vector<uint8_t> strips(88, 0);
    if (pass <= 2) {
        for (int st = 0; st < 8; st++) {
            int hue = st * 2 + (pass - 1);
            for (int j = 0; j < 8; j++) strips[st * 11 + j] = static_cast<uint8_t>(4 + hue * 4 + j / 2);
        }
    } else {
        for (int v = 72; v <= 127; v++) strips[v - 72] = static_cast<uint8_t>(v);
        strips[5 * 11 + 1] = 1;
        strips[5 * 11 + 2] = 2;
        strips[5 * 11 + 3] = 3;
        for (int j = 0; j < 11; j++) strips[7 * 11 + j] = 71;
    }

    hid::start(nullptr, nullptr);
    for (int i = 0; i < 40 && !hid::connected(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    if (!hid::connected()) {
        std::printf("no Jam\n");
        hid::stop();
        return 1;
    }

    std::vector<uint8_t> buttons(53, 0), pads(80, 0);
    std::printf("80 %d\n", hid::write(0x80, buttons.data(), buttons.size()));
    std::printf("81 %d\n", hid::write(0x81, pads.data(), pads.size()));
    std::printf("82 %d\n", hid::write(0x82, strips.data(), strips.size()));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    hid::stop();
    return 0;
}
