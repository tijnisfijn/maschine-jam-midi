# Maschine Jam MIDI on Windows

The Windows build of the driver described in [../WINDOWS.md](../WINDOWS.md). It reads the Jam over
USB HID and turns it into a plain MIDI device, with exactly the messages in
[../MIDI-MAP.md](../MIDI-MAP.md), so a mapping made on macOS works here too.

**State:** working, and checked on a real Jam on Windows 11 (25H2, build 26200). The controls read,
every LED writes, the Jam is picked up again after a replug, and a MIDI port called **Maschine Jam
MIDI** appears for other apps to open.

Two things Windows needs that macOS does not:

- **`NIHardwareService` must be stopped**, or it fights the driver over the LEDs. See
  [Native Instruments' software](#native-instruments-software).
- **A virtual MIDI driver**, because Windows cannot make a MIDI port by itself. See
  [The MIDI port](#the-midi-port), which also covers the licence question that comes with it.

## Build

You need **Visual Studio Build Tools 2022** with the *Desktop development with C++* workload, which
brings MSVC, the Windows SDK, CMake and Ninja. There are no other dependencies: HID goes straight
through Win32 (`setupapi` + `hid.dll`), MIDI through WinMM.

```powershell
powershell -ExecutionPolicy Bypass -File windows\build.ps1
```

That writes `windows\build\jam-midi.exe`, `jam-send.exe` and `strip-test.exe`. Or do it by hand
from a *x64 Native Tools Command Prompt*:

```bash
cmake -S windows -B windows/build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build
```

## Install

Two ways, both per-user and neither needing admin rights. They put the same files in the same
place, `%LOCALAPPDATA%\Programs\Maschine Jam MIDI`.

**An installer** — build `Maschine Jam MIDI Setup <version>.exe` with
[Inno Setup 6](https://jrsoftware.org/isinfo.php):

```powershell
powershell -ExecutionPolicy Bypass -File windows\installer\build-installer.ps1
```

It lands in `windows\build\installer\`. Add `-InstallInno` to have the script fetch Inno Setup
through winget first. The installer offers a "start when I log on" tick box, warns if no virtual
MIDI driver is present, and shows the notes in `windows\installer\before-install.txt` before it
starts.

**Or no installer at all**, the equivalent of `scripts/install.sh` on macOS:

```powershell
powershell -ExecutionPolicy Bypass -File windows\install.ps1 -Startup
```

It builds, copies, optionally makes the Startup shortcut, and then prints what still has to be
done (virtual MIDI driver, NI's service). `windows\uninstall.ps1` undoes it.

### Signing

**Neither the installer nor `jam-midi.exe` is signed.** There is no code-signing certificate
behind this project, so Windows does not recognise the publisher and SmartScreen shows
*"Windows protected your PC"* the first time you run the installer. Click **More info**, then
**Run anyway**.

That warning is about provenance, not about anything found in the file. If you would rather not
take it on trust, build it yourself: the whole thing compiles from this repository with nothing
but Visual Studio Build Tools, and `windows\install.ps1` skips the installer entirely.

Both installs are per-user on purpose, so there is no UAC prompt on top of the SmartScreen one.
Signing would need an OV or EV certificate from a CA (yearly, and EV wants a hardware token);
`signtool.exe` from the Windows SDK would then sign both the exe and the installer, and EV
clears SmartScreen at once while OV has to build reputation first.

## Run

```powershell
windows\build\jam-midi.exe
```

| Option | What |
|---|---|
| `-v` | print every control and every MIDI message |
| `--demo` | the colour test |
| `--raw` | print the raw HID reports |
| `--name NAME` | the port the Jam's controls go to (default `Maschine Jam MIDI`) |
| `--feedback NAME` | the port LED messages come from (default: the name above plus ` Feedback`) |
| `--list-ports` | print every MIDI port Windows knows about, and quit |
| `--no-midi` | skip MIDI altogether: HID and LEDs only, handy for testing |
| `--no-refresh` | stop rewriting all the LEDs every 2 s, to track down flicker |

It waits for the Jam and picks it up again after a replug. Ctrl-C turns the LEDs off and quits.

## The MIDI port

On macOS the driver creates its own port with `MIDISourceCreate`. Classic Windows MIDI has no such
call, so a virtual MIDI driver has to supply one. The whole MIDI side sits behind `src/midi.h`
(`open` / `send` / a receive callback), with one file per backend, so another one drops in without
touching the driver.

**Either way you need [loopMIDI](https://www.tobias-erichsen.de/software/loopmidi.html)
installed** (free, Tobias Erichsen). What differs is which half of it the driver uses, and that
decides whether a licence comes with it.

`--midi-backend` picks: `auto` (the default), `loopmidi` or `virtual`.

### Option A: a pair of loopMIDI ports — no licence

The driver opens two ports you made in loopMIDI, through Microsoft's own WinMM. It touches no
third-party SDK, so **nothing here needs a licence**, whatever you use the driver for. This is
what `auto` takes when the ports exist, and it is the right choice for commercial use.

1. In loopMIDI, create **two** ports, named exactly:
   - `Maschine Jam MIDI`
   - `Maschine Jam MIDI Feedback`
2. Start `jam-midi.exe`.
3. In Resolume, Ableton, Bitwig, TouchDesigner, ... set:
   - **MIDI input** → `Maschine Jam MIDI` (the Jam's controls arrive here)
   - **MIDI output** → `Maschine Jam MIDI Feedback` (send LED messages here)

Two ports rather than one because a loopMIDI port is a loop: anything written to it comes straight
back out, so one port would make the driver hear its own output. The names are only defaults;
`--name` and `--feedback` take any pair.

### Option B: the driver makes its own port — licence for commercial use

loopMIDI installs `teVirtualMIDI64.dll`, and through it the driver creates **one** port called
`Maschine Jam MIDI` at startup, exactly like the macOS version: apps open the same name as their
MIDI input and output, and no ports have to be made by hand. The driver loads the DLL at run time,
so it still builds and runs without it, and none of it is bundled here.

The catch: the teVirtualMIDI SDK is **free for personal use, but commercial use needs a licence**
from its author. The driver says so in its log whenever it uses this route. If that matters, make
the two loopMIDI ports above and `auto` will prefer them, or pass `--midi-backend loopmidi`.

The port only exists while `jam-midi.exe` runs, and disappears from the list when it quits — the
same as the macOS virtual source.

### Option C: Windows MIDI Services — the eventual answer, not yet

This is the one with no third party at all, and Microsoft's own FAQ says selling an application
that uses it is fine. It is not reachable yet:

- The **service and transports are already in-box** on Windows 11 25H2 (`MidiSrv.exe`,
  `Midi2.VirtualMidiTransport.dll`, `Midi2.LoopbackMidiTransport.dll` in `System32`).
- The **API an app needs is not**. It is still the `Windows.Devices.Midi2` preview NuGet package
  (0.99.92-preview.11), shipped under a "Developer Go-live agreement", and on this machine the
  WinRT classes are not registered. It is not in winget either.
- As of *In-box Preview 14* (17 November 2026) Microsoft had not said when the API itself ships in
  Windows, only that it will, and that it needs 25H2 or newer.

When it lands, add a `midi_wms.cpp` beside the other two and put it first in `midi.cpp`. Until
then option A is the licence-free route.

### If no port can be made

The driver says so once, keeps running, and keeps looking every two seconds, so you can install
loopMIDI or make the ports while it runs and it will pick them up without a restart. The Jam and
its LEDs work meanwhile, so `--demo` is still worth running. `--list-ports` shows what Windows can
see.

## Native Instruments' software

**It can stay installed, but `NIHardwareService` must not be running.** This is the one real
difference from macOS, where NI's agent leaves the LEDs alone. Checked on this machine with
Controller Editor 2.8.2.0 and the Maschine Jam Driver 4.4.0.133 installed:

- **HID access is not the problem.** Windows exposes the Jam as a plain HID device
  (`HID\VID_17CC&PID_1500&MI_00`, *HID-compliant vendor-defined device*), and the driver opens it
  with `FILE_SHARE_READ | FILE_SHARE_WRITE` and reads and writes it fine *while* NI's service holds
  it too. No sharing error. Controller Editor does not need to run.
- **The LEDs are the problem.** With `NIHardwareService` running, pressing one of the left-hand
  mode keys (Macro, Level, Aux, Control, Auto, ...) sets NI's service writing the LEDs as well.
  It and the driver's own refresh then fight over them and every other key starts flashing.
  Stopping the service stops it dead. Nothing in the driver can fix this: there is no way to ask
  NI's service to let go.

So, before running the driver (both need admin):

```powershell
Stop-Service -Name NIHardwareService -Force
Set-Service -Name NIHardwareService -StartupType Manual   # so it stays away after a reboot
```

To put it back:

```powershell
Set-Service -Name NIHardwareService -StartupType Automatic
Start-Service -Name NIHardwareService
```

**This affects every NI device, not just the Jam.** `NIHardwareService` is what Maschine, Komplete
Kontrol and Traktor hardware talk through, and stopping it also takes away NI's own
`Maschine Jam - 1` MIDI port. If you use other NI gear on the same machine, start it again when you
need it. Uninstalling Controller Editor and the Maschine Jam Driver works too and is the tidier
answer if the Jam is the only NI device here, but it is not necessary and it is not reversible with
one command.

Use **Maschine Jam MIDI** (the loopMIDI port) in your app, not NI's `Maschine Jam - 1`.

If you hit `ERROR_ACCESS_DENIED` or `ERROR_SHARING_VIOLATION` on some other machine, the driver
names it in the log; quitting Controller Editor and stopping `NIHardwareService` is the first thing
to try there too.

## How the code is laid out

| File | What |
|---|---|
| `src/jam.h`, `src/jam.cpp` | the platform-neutral core, ported one to one from `Sources/Driver/Driver.swift`: the key table, pad numbering, the colour palette, `render()`, `handleMidi()`, `handleReport()` |
| `src/hid.h`, `src/hid_win.cpp` | Win32 HID: find the Jam by VID/PID, overlapped `ReadFile`, padded LED writes, reopen after a replug |
| `src/midi.h`, `src/midi.cpp` | the MIDI interface, and which backend to use |
| `src/midi_backend.h` | the internal interface the two backends implement |
| `src/midi_tevirtual.cpp` | option B: makes its own port through teVirtualMIDI, loaded at run time |
| `src/midi_winmm.cpp` | option A: a pair of loopMIDI ports through WinMM |
| `src/main.cpp` | the CLI and the event loop |
| `tools/jam-send.cpp` | send raw MIDI to a port by name |
| `tools/strip-test.cpp` | write raw colours straight to the strip LEDs |
| `install.ps1`, `uninstall.ps1` | install for the current user without an installer |
| `installer/` | the Inno Setup script and the script that builds the `.exe` |

The core keeps no Windows types, so it is the natural starting point if the macOS and Windows
versions ever share a C++ core, as the handover suggests. Don't do that first.

Threads: a HID thread reads input reports, WinMM calls back on its own thread, and both push into
one queue that `main.cpp` drains. Everything that touches driver state runs on that one thread,
which is what the macOS version gets from the main run loop.

### Windows details that mattered

- Read and write buffers must be exactly the length `HidP_GetCaps` reports. On the Jam that is
  **49 in** and **89 out**, as the handover guessed, so the shorter LED reports (`0x80`: 54 bytes,
  `0x81`: 81) are padded with zeros.
- Two handles are opened on the same device, a read handle (overlapped) and a write handle, so the
  LED writes never share an `OVERLAPPED` with the reader.
- The read waits in 200 ms slices, so Ctrl-C does not hang on a silent Jam.
- The Jam's HID collection is usage page `0xFF01`, feature report length 205 — exactly what
  [../PROTOCOL.md](../PROTOCOL.md) describes from macOS.

## Start it at logon

Tick the box in the installer, or run `windows\install.ps1 -Startup`. Either makes a minimised
shortcut in `shell:startup`.

The order no longer matters: loopMIDI starts at logon on its own setting, and if the driver gets
there first it simply keeps looking for the port every two seconds until loopMIDI is up.
