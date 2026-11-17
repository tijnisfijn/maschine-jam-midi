# Maschine Jam: the USB HID protocol

USB vendor `0x17CC` (Native Instruments), product `0x1500`, product name `Maschine Jam`. A standard USB HID device, so no NI driver is needed. Usage page `0xFF01` (vendor-defined). The OS doesn't treat it as a keyboard, so macOS asks for no Input Monitoring permission.

The layout comes from [cabl](https://github.com/shaduzlabs/cabl) (MIT). Every part marked **checked** was confirmed on a real Jam on 17 November 2026. On macOS, the device also opened and worked while NI's `NIHardwareAgent` was running.

The report sizes below include the report ID byte. The descriptor allows input reports up to 49 bytes, output reports up to 89 and feature reports up to 205.

## Input 0x01: keys and encoder (17 bytes) — checked

| Byte | What |
|---|---|
| 0 | `0x01` |
| 1 | low 4 bits: the encoder, a counter 0–15 that wraps (+1 per click right). The high 4 bits were always 0 in testing |
| 2–16 | 120 key bits: key *n* is bit *n* % 8 (LSB first) of byte 2 + *n* / 8. 1 = down |

Key bits:

| Bit | Key | Bit | Key |
|---|---|---|---|
| 0 | Song (Arrange) | 90 | Mst |
| 1–8 | Scene 1–8 (the top row) | 91 | Grp |
| 9 | Step | 92 | In 1 |
| 10 | Pad Mode | 93 | Headphones (Cue) |
| 11 | Clear | 94 | Browse |
| 12 | Duplicate | 95 | Macro |
| 13 | D-pad Up | 96 | Level |
| 14 | D-pad Left | 97 | Aux |
| 15 | D-pad Right | 98 | Control |
| 16 | D-pad Down | 99 | Auto |
| 17 | Note Repeat (Arp) | 100 | Perform |
| 18–81 | the grid: bit 18 + *p*, *p* = 0 top-left, row by row (*p* = 8 × row + column, rows from the top) | 101 | Notes (Variation) |
| 82–89 | Group A–H | 102 | Lock |
| | | 103 | Tune |
| | | 104 | Swing |
| | | 105 | Shift |
| | | 106 | Play |
| | | 107 | Rec |
| | | 108 | Transport ◀ |
| | | 109 | Transport ▶ |
| | | 110 | Tempo |
| | | 111 | Grid |
| | | 112 | Solo |
| | | 113 | Mute |
| | | 114 | Select |
| | | 115 | Encoder touch |
| | | 116 | Encoder push |

Bits 117–119 are unused. NI's template mentions a footswitch (CC 64 and 65), but its bits have not been found. Bits 117–119 are a good guess.

Several keys can change in one report, so compare the whole bitfield with the previous one. Don't stop at the first change, which is what cabl does.

## Input 0x02: touch strips (49 bytes) — checked

8 blocks of 6 bytes. Strip *s* (0 = left) starts at byte 1 + 6*s*:

| Offset | What |
|---|---|
| 0–1 | a timestamp, little-endian, in ms. It wraps |
| 2–3 | position, little-endian: 0 = not touched, otherwise about 1 (bottom) to 1023 (top) |
| 4–5 | always 0 in testing. Possibly a second finger |

A report comes in while a strip is touched, plus one with position 0 when it is let go.

## Input 0x40 (25 bytes)

In the descriptor (12 × 16-bit). Never seen in testing.

## Output 0x80: key LEDs (54 bytes = ID + 53) — checked

One byte per LED, 0–127 = brightness. Most keys are white. Play is green, Rec red, Select yellow (fixed colours, the byte sets their brightness).

| Byte | LED | Byte | LED |
|---|---|---|---|
| 0 | Song | 18 | Level |
| 1 | Step | 19 | Aux |
| 2 | Pad Mode | 20 | Control |
| 3 | Clear | 21 | Auto |
| 4 | Duplicate | 22 | Perform |
| 5 | D-pad Up | 23 | Notes |
| 6 | D-pad Left | 24 | Lock |
| 7 | D-pad Right | 25 | Tune |
| 8 | D-pad Down | 26 | Swing |
| 9 | Note Repeat | 27 | Shift |
| 10 | Mst | 28 | Play |
| 11 | Grp | 29 | Rec |
| 12 | In 1 | 30 | Transport ◀ |
| 13 | (none) | 31 | Transport ▶ |
| 14 | Headphones | 32 | Tempo |
| 15 | (none) | 33 | Grid |
| 16 | Browse | 34 | Solo |
| 17 | Macro | 35 | Mute |
| | | 36 | Select |
| | | 37–44 | level meter, left column (8 segments) |
| | | 45–52 | level meter, right column |

Segment order inside the meter columns: 37 is the bottom, which follows cabl. Only "full" and "half" were checked.

## Output 0x81: pad, scene and group colours (81 bytes = ID + 80) — checked

| Byte | LED |
|---|---|
| 0–7 | Scene 1–8 |
| 8–71 | the grid: 8 + *p*, *p* = 0 top-left, row by row (same as the key bits) |
| 72–79 | Group A–H |

Each byte is a colour index (see the palette below).

## Output 0x82: touch strip LEDs (89 bytes = ID + 88) — checked

Strip *s*, LED *j* = byte 11*s* + *j*, with *j* = 0 at the bottom. Each byte is a colour index.

## Colour palette — checked

0 = off. 4 + 4 × hue + level: 16 hues (red, orange, light orange, yellow, lime, yellow-green, green, mint, cyan, sky, blue, indigo, violet, magenta, pink, red-pink) × 4 levels (dim, half, full, pale). 68–70 are white from dim to bright, and 71 is ice blue. 1–3 are off. The hardware shows 72–127 as 71.

On the touch strips the four levels look almost the same. The pads show them clearly.

## Feature reports

The descriptor has feature reports `0xD0`, `0xD8`–`0xDD`, `0xF1`, `0xF2` and an output report `0xF4` (3 bytes). None is needed: the Jam needs no setup message. It reports input and takes LED writes straight away.

## LEDs and other software

The Jam keeps the last LED values it got. NI's agent did not overwrite them on macOS in testing. The driver still writes all three LED reports every 2 seconds, which also restores them after a replug.
