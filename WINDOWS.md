# Handover: the Windows version

The goal: the same driver on Windows. You plug in the Jam, a MIDI port called **Maschine Jam MIDI** appears, and it behaves exactly as described in [MIDI-MAP.md](MIDI-MAP.md). Controller Editor isn't needed, and every LED and colour is under your control.

## What is already settled (from the macOS version)

- **The USB protocol is known and checked on hardware:** [PROTOCOL.md](PROTOCOL.md). It is plain HID, with three input reports and three LED output reports, and no setup message.
- **The MIDI behaviour is fixed:** [MIDI-MAP.md](MIDI-MAP.md). The Windows version must send and accept exactly the same messages, so mappings work on both systems.
- **The reference code is `Sources/Driver/Driver.swift`** (about 400 lines; `Sources/App` is the macOS menu bar app around it). The parts that don't depend on the platform are easy to find and port one to one:
  - **Tables:** `keys`, `ctlForBit`, `keyForCC`, `defaultStripColour`.
  - **Pad numbering:** `noteForPad` / `padForNote`, hardware order top-left to MIDI order bottom-left.
  - **Colours:** `colour` (clamped to 0–71) and `pressedColour` (a held pad gets one step brighter, or white 70).
  - **`render()`:** turns host state + held keys into the three LED buffers. A held key is full brightness.
  - **`handleMIDI()`:** channel 1 sets pads, scenes, groups, keys, strip bars, the level meter and the encoder's start value. Channel 2 sets single strip LEDs and bar colours.
  - **`handleReport()`:**
    - **Keys:** diff the whole key bitfield, every changed bit.
    - **Encoder:** a 4-bit counter delta that wraps, sent as absolute CC 42 and relative CC 43.
    - **Strips:** position 0 means released. Send a touch CC on change and a position CC when touched.
  - **LED refresh:** only write a report whose buffer changed, plus a forced rewrite of all three every 2 s. On quit, all LEDs off.

## What is different on Windows

### 1. Check first: does Windows see the Jam as HID?

On macOS the Jam is a plain HID device, and the driver opens it alongside NI's agent. On Windows this is the biggest unknown:
- **Device Manager:** look under *Human Interface Devices* for a device with `VID_17CC&PID_1500`. If it's there, the plan below works.
- **NI's driver:** if NI's driver (installed with Maschine / Controller Editor) has claimed it and it sits under a *Native Instruments* entry instead, HID access may fail. Try on a machine (or a user account) without NI's software first. Then find out whether NI's driver can stay installed.
- **NI's agent:** `NIHardwareAgent` may open the device exclusively on Windows. If `CreateFile` fails with a sharing error, quit the agent (Task Manager / its service) and note it in the README.

### 2. HID access

Two options:
- **[hidapi](https://github.com/libusb/hidapi)**: BSD/GPL dual-licensed, tiny, also works on macOS and Linux. `hid_open(0x17CC, 0x1500, NULL)`, `hid_read` for input reports, `hid_write` for LED reports. **Recommended**: it hides the Windows details below.
- **Raw Win32:**
  - **Opening:** find the device with `SetupDiGetClassDevs` + the HID interface GUID, then `CreateFile` with `FILE_SHARE_READ | FILE_SHARE_WRITE`.
  - **Input:** read input reports with overlapped `ReadFile`.
  - **LEDs:** write them with `WriteFile` or `HidD_SetOutputReport`.

Windows-specific pitfalls:
- Read and write buffers usually have to be exactly the size `HidP_GetCaps` reports, `InputReportByteLength` and `OutputReportByteLength` (likely 49 and 89). Pad the shorter LED reports (`0x80`: 54 bytes, `0x81`: 81) with zeros to that length. hidapi handles this for you on writes.
- The first byte is always the report ID (`0x01`/`0x02` in, `0x80`/`0x81`/`0x82` out). It's the same on macOS.
- Plug and play: reopen the device when it disappears and comes back. Poll every second, or use `RegisterDeviceNotification`.

### 3. A MIDI port that other apps can open

This is the real work. macOS lets any program create a virtual MIDI port (`MIDISourceCreate`). Classic Windows MIDI (WinMM) does not. Options, from fastest to cleanest:

| Option | How | For / against |
|---|---|---|
| **A. loopMIDI** (Tobias Erichsen, free) | the user creates two loopMIDI ports, e.g. `Maschine Jam MIDI` and `Maschine Jam MIDI Feedback`. The driver writes its output to one with WinMM `midiOutOpen` and reads LED messages from the other with `midiInOpen`. The app uses the opposite ends | Works today with no extra SDK, but the user has to install and set up loopMIDI, and there are two port names instead of one |
| **B. teVirtualMIDI SDK** (same author, used by loopMIDI) | the driver creates its own port at runtime, like on macOS | One port, made by the driver. Free for non-commercial use, a commercial licence is needed otherwise (check the current terms). The user still needs the virtualMIDI driver installed |
| **C. Windows MIDI Services** (Microsoft's new MIDI stack for Windows 11) | the driver creates an app-to-app virtual device through the Windows MIDI Services SDK, and older apps see it as a normal MIDI port | Native, no third-party driver. Only on Windows 11 with Windows MIDI Services installed or rolled out, so check the current status and SDK first |

Suggested order: build with **A** first to prove HID + MIDI end to end on your machine. Then move to **C** (or **B**) so the driver makes its own port. Keep the MIDI side behind a small interface (`send(bytes)` + a callback for received bytes) so swapping it is a single file.

### 4. Language and build

Use C++17 with MSVC (Visual Studio Build Tools) and CMake, plus hidapi as source or through vcpkg (`vcpkg install hidapi`). It's a console exe with the same options as macOS: `-v`, `--demo`, `--raw`, `--name`.

Later, the platform-neutral part could become a shared C++ core used by both versions (the macOS one is Swift today). Don't do that first. Get Windows working, then decide.

### 5. Starting automatically

Start with a shortcut in `shell:startup`, or a scheduled task at logon. Optionally add a tray icon later. It doesn't need to be a service: it needs the user's MIDI session anyway.

## Test checklist (what was checked on macOS)

Run with `-v --demo` and check:

1. **Demo lights:** the pads, scene and group keys show a rainbow. The other keys are dimly lit (Play green, Rec red, Select yellow). All 8 strips are full in 8 colours.
2. **Pads:** the top-left pad sends note 56 and the bottom-left sends note 0. Note 56 with velocity 5 from the app turns the top-left pad dim red.
3. **Every key** prints its name on press and release, including Shift and Select. D-pad Up / Left come out as named.
4. **Encoder:** clicks right count up (CC 42 rises, CC 43 = 1), clicks left count down. Push and touch send CC 44 and 45.
5. **Strip 1:** a finger at the bottom gives about 0 and at the top 127. Touch and release send CC 56 = 127 / 0.
6. **Level meter:** CC 76 = 127 lights the left column fully, CC 77 = 64 lights the right half.
7. **Strip LEDs:** CC 48 = 32 gives a short bar from the bottom of strip 1. Channel 2 note 0 with velocity 70 turns its bottom LED white.
8. **Replug:** unplug and plug in the Jam. The driver reports it, and the LEDs come back within 2 s.
9. **Quit:** all LEDs go off.
10. **NI:** note whether it works with NI's software installed and running, and write that in the README.

The macOS `Tools/send.swift` and `Tools/strip-test.swift` are worth porting too: a raw MIDI sender and a raw strip-colour test.

## Working with Claude Code on the Windows machine

Clone this repository, open it in Claude Code, and start with something like:

> Read WINDOWS.md, PROTOCOL.md, MIDI-MAP.md and Sources/Driver/Driver.swift. Build the Windows version as described in WINDOWS.md, in a `windows/` folder, starting with option A (loopMIDI). First check in Device Manager how Windows sees the Jam.

Keep the macOS files as they are and add the Windows code next to them (`windows/`). Update the README's platform line when it works.
