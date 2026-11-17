# Maschine Jam MIDI

**Use the Native Instruments Maschine Jam as an ordinary MIDI controller, without Controller Editor and with full control of every LED colour.**

Normally the Maschine Jam only works as a MIDI controller while NI's Controller Editor runs. Even then it has hardware pages, Shift and Select send nothing, and you can't freely set the colours. `jam-midi` is a small open-source driver that talks to the Jam over USB itself. It turns the Jam into a plain MIDI device called **Maschine Jam MIDI** that any app can use: Resolume, Ableton Live, Bitwig, TouchDesigner, a DAW, or your own code.

- **Every control sends MIDI:** 64 pads, scene and group keys, every other key including Shift and Select, the encoder (absolute and relative, plus push and touch), and all 8 touch strips (position and touch).
- **Every LED listens to MIDI:** pad, scene and group colours (68 colours), key brightness, the touch strip LEDs (as a bar or one by one), and the level meter.
- **No hardware pages:** every control sends one fixed message, and every LED has one fixed address.
- **Compatible:** the numbers follow page 1 / page A of NI's default template, so a mapping made for that template mostly still works. The few exceptions are listed in [MIDI-MAP.md](MIDI-MAP.md).
- **Small:** one Swift file with no dependencies. It picks the Jam up again after a replug.

**Platform:** macOS. A Windows version is planned, see [WINDOWS.md](WINDOWS.md).

| File | What |
|---|---|
| [MIDI-MAP.md](MIDI-MAP.md) | every MIDI message in and out, and the colour palette |
| [PROTOCOL.md](PROTOCOL.md) | the Jam's USB HID protocol, as checked on a real Jam |
| [WINDOWS.md](WINDOWS.md) | handover for building the Windows version |
| `Sources/main.swift` | the driver |
| `Tools/` | test tools: send MIDI, test the strip colours |

## Download

Ready-built for macOS 11 or newer (Apple Silicon and Intel) under [Releases](https://github.com/tijnisfijn/maschine-jam-midi/releases/latest). The binaries are not notarised, so after unzipping run `xattr -dr com.apple.quarantine` on the folder once. The release notes give the exact steps.

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

Start it before the app that uses the Jam, then pick **Maschine Jam MIDI** as both the MIDI input and output in that app. It waits for the Jam and picks it up again after a replug. Ctrl-C turns the LEDs off and quits.

Two tools help with testing:
- `build/jam-send "Maschine Jam MIDI" B0 4C 7F` sends raw MIDI to the driver.
- `build/strip-test 1|2|3` writes test colours straight to the strip LEDs. Stop the driver before you run it.

## NI's software

Controller Editor does not need to run. NI's `NIHardwareAgent` can keep running: macOS lets both open the Jam, and the agent does not overwrite the LEDs. The driver writes the LEDs again every 2 seconds anyway. NI's own `Maschine Jam - 1` ports stay as well. Use **Maschine Jam MIDI** instead.

## Made with AI

This driver was built by Tijn together with Claude (Anthropic's AI) in Claude Code. Claude wrote the code and the docs. Every control, LED and colour was checked by hand on a real Maschine Jam. Issues and pull requests are welcome.

## Credits and licence

MIT licence, see [LICENSE](LICENSE) and [NOTICE](NOTICE). The HID layout (which bit is which key, the LED reports and the colour palette) comes from [cabl](https://github.com/shaduzlabs/cabl), Copyright (C) 2015 Vincenzo Pacella, MIT licence. It was checked against a real Jam on 17 November 2026.

This is an independent project. It is not made by, endorsed by or connected to Native Instruments. Maschine and Maschine Jam are trademarks of Native Instruments GmbH.
