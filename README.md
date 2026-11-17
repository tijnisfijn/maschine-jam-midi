# Maschine Jam MIDI

The Native Instruments Maschine Jam as an ordinary MIDI device, without Controller Editor. `jam-midi` talks to the Jam over USB HID itself. It sends MIDI out of a port called **Maschine Jam MIDI** and sets every LED from MIDI it receives on that same port: pad colours, scene and group colours, key brightness, touch strip LEDs and the level meter.

- No hardware pages. Every control sends one fixed message, and every LED has one fixed address.
- Shift and Select send MIDI too. NI's MIDI mode keeps both for itself.
- The numbers follow page 1 / page A of NI's default template, so a mapping made for that template still works. The exceptions are listed in [MIDI-MAP.md](MIDI-MAP.md).

## Build and run

It runs on macOS and needs only Apple's command line tools (`xcode-select --install`). There are no other dependencies.

```bash
mkdir -p build && swiftc -O -swift-version 5 Sources/main.swift -o build/jam-midi
```

```bash
swiftc -O Tools/send.swift -o build/jam-send && swiftc -O Tools/strip-test.swift -o build/strip-test
```

```bash
./build/jam-midi
```

| Option | What |
|---|---|
| `-v` | print every control and every MIDI message |
| `--demo` | light everything with the palette (a quick check) |
| `--raw` | print the raw HID reports |
| `--name NAME` | name of the MIDI ports |

Start it before the app that uses the Jam. It waits for the Jam and picks it up again after a replug. Ctrl-C turns the LEDs off and quits.

Two tools help with testing:
- `build/jam-send "Maschine Jam MIDI" B0 4C 7F` sends raw MIDI to the driver.
- `build/strip-test 1|2|3` writes test colours straight to the strip LEDs. Stop the driver before you run it.

## NI's software

Controller Editor does not need to run. NI's `NIHardwareAgent` can keep running: macOS lets both open the Jam, and the agent does not overwrite the LEDs. The driver writes the LEDs again every 2 seconds anyway. NI's own `Maschine Jam - 1` ports stay as well. Use **Maschine Jam MIDI** instead.

## Credits and licence

MIT licence, see [LICENSE](LICENSE). The HID layout (which bit is which key, the LED reports and the colour palette) comes from [cabl](https://github.com/shaduzlabs/cabl), Copyright (C) 2015 Vincenzo Pacella, MIT licence. It was checked against a real Jam on 17 November 2026.

This is an independent project. It is not made by, endorsed by or connected to Native Instruments. Maschine and Maschine Jam are trademarks of Native Instruments GmbH.
