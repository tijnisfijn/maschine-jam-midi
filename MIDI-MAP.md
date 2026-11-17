# Maschine Jam MIDI: the map

All on **MIDI channel 1** unless noted. Buttons send 127 on press and 0 on release (pads: note on 127 / note off). Send the same number back to set its LED.

## From the Jam

| Control | Message | LED (send to the Jam) |
|---|---|---|
| Grid 8 x 8 | notes 0–63: bottom row 0–7, top row 56–63 | note on, velocity = colour |
| Scene 1–8 (top row) | CC 0–7 | CC value = colour |
| Group A–H | CC 8–15 (press and release; NI's template sends only a press) | CC value = colour |
| Song (Arrange) 16, Step 17, Pad Mode 18, Clear 19, Duplicate 20 | CC | CC value = brightness |
| D-pad Up 21, Left 22, Right 23, Down 24 | CC | CC value = brightness |
| Note Repeat 25, Macro 26, Level 27, Aux 28, Control 29, Auto 30, Browse 31 | CC | CC value = brightness |
| Perform 32, Notes (Variation) 33, Lock 34, Tune 35, Swing 36, **Select 37** | CC | CC value = brightness |
| Mst 38, Grp 39, In 1 40, Headphones (Cue) 41 | CC | CC value = brightness |
| Encoder | CC 42, absolute 0–127 (like NI's template) | CC 42 sets where it continues from |
| Encoder, relative | CC 43: 1 per click right, 127 per click left | |
| Encoder push / touch | CC 44 / CC 45 | |
| **Shift** | CC 46 | CC value = brightness |
| Tempo | CC 47 | CC value = brightness |
| Touch strips 1–8 | CC 48–55, position 0 (bottom) – 127 | CC 48–55 draws a bar |
| Touch strips 1–8, touched | CC 56–63: 127 touched, 0 released | |
| Transport ◀ 66, ▶ 67, Play 68, Rec 69, Solo 70, Mute 71, Grid 72 | CC | CC value = brightness |
| Level meter, left / right column | | CC 76 / 77, value 0–127 = 0–8 segments |

Every key keeps its LED at full brightness while it is held. A held pad, scene or group key turns one step brighter, or white if no colour was set.

**Not NI's numbers:** Shift, Select, the encoder push, the touch flags and the transport keys. NI's template puts Tempo, Solo, Mute and the encoder push on notes 16, 24, 73 and 42, which clash with grid notes, and it sends the touch flag as poly aftertouch. Here they are all CCs.

## Channel 2: the touch strips LED by LED

| Message | What |
|---|---|
| Note 0–87, velocity = colour | one strip LED: strip *s* (0–7), LED *j* (0–10) = note 11 *s* + *j*. Velocity 0 hands the LED back to the bar |
| CC 48–55, value = colour | the colour of strip 1–8's bar (0 = the default rainbow) |

## Colours

Velocity / value = **4 + 4 × hue + level**, 0 = off.

| Level | + | |
|---|---|---|
| dim | 0 | |
| half | 1 | |
| full | 2 | |
| pale | 3 | |

| Hue | Full colour | Hue | Full colour |
|---|---|---|---|
| 0 red | 6 | 8 cyan | 38 |
| 1 orange | 10 | 9 sky blue | 42 |
| 2 light orange | 14 | 10 blue | 46 |
| 3 yellow | 18 | 11 indigo | 50 |
| 4 lime | 22 | 12 violet | 54 |
| 5 yellow-green | 26 | 13 magenta | 58 |
| 6 green | 30 | 14 pink | 62 |
| 7 mint | 34 | 15 red-pink | 66 |

68–70: white, dim to bright. 71: ice blue. 1–3 are off, and the Jam itself shows everything above 71 as 71, so there are no more colours to find.

**On the touch strips** the four levels hardly differ: the LEDs are small and bright, so a strip shows about 16 colours plus white, not 64. Pick colours by hue there. The dim level of some hues looks like a different hue (dim orange looks red). The pads and the scene and group keys show the levels clearly.
