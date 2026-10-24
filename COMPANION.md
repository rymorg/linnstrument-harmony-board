Harmony Board companion
=======================

`companion/index.html` is the Harmony Board mockup turned into a companion app. On its own it's the playable mockup. Connected to a LinnStrument running the Harmony Board firmware, it mirrors the instrument: the board shows the same key, mode, chord, lights and hints, the guides and readouts follow along, and the pads you play light up. Settings screens (Global Settings, Per-Split Settings, Preset, Volume, Octave/Transpose, the Harmony page and the rest) show the LinnStrument's own lights, exactly as the hardware does, with the screen's name above the board. On Global Settings, Per-Split Settings, Preset, Volume, Octave/Transpose, the Harmony page and the LED check, the pads also carry short labels, and hovering over one shows what it does; the descriptions come from the firmware's settings code.

This is phase 1: the app only watches. Changing the harmony from the app, pushing memory banks and saving song setups come next (see [HARMONY-STATUS.md](HARMONY-STATUS.md)).

Run it
------

The app uses Web MIDI, which works in Chrome and Edge (not Safari) on a page served from your own computer.

1. Turn OS Update off on the LinnStrument, so it shows up as LinnStrument MIDI.
2. Serve the folder:

   ```bash
   python3 -m http.server 8766 --bind 127.0.0.1 --directory companion
   ```

3. Open http://localhost:8766 in Chrome or Edge and click **Connect LinnStrument**. Allow MIDI, including the part about system exclusive messages, when the browser asks.

The same folder holds the browser flasher, at http://localhost:8766/flash/ when served this way (`companion/flash/`; see [INSTALL.md](INSTALL.md)). It uses Web Serial with OS Update on, not Web MIDI.

Ableton (or any other app) can use the LinnStrument at the same time: macOS lets several apps share a MIDI device. The app reconnects by itself if the LinnStrument is unplugged and plugged back in.

While it's mirroring, the board and the Controls panel on the page don't respond to clicks, so the screen can't drift away from the instrument. Pads that play a held note light white, matched by note number: every pad that plays that note lights, like the LinnStrument's "Same" note lights.

How it talks to the LinnStrument
--------------------------------

Over USB MIDI, with a small private SysEx protocol. Every message is:

```
F0 7D 48 42 <version> <command> <data...> F7
```

`7D` is MIDI's manufacturer ID for non-commercial use, `48 42` is "HB", the version is 1, and every data byte is 0–127. SysEx is used because DAWs ignore it on their inputs, so it doesn't reach your instruments.

| Command | Direction | When | Data |
|---|---|---|---|
| `01` State | LinnStrument → app | when the harmony, a setting or the screen changes (checked every 20 ms), and on request | 37 bytes, below |
| `02` Memories | LinnStrument → app | when a memory changes (checked every 100 ms), and on request | 8 × 13 bytes, below |
| `03` Frame | LinnStrument → app | while a settings screen is open, when its lights change (checked every 50 ms), and on request | columns, rows, then one byte per light, below |
| `10` Request | app → LinnStrument | when the app connects | none |

The LinnStrument only sends these over USB, not DIN, and not in OS Update mode.

**State** (offset: meaning):

| Byte | Meaning |
|---|---|
| 0 | Harmony mode on (1) or off (0) |
| 1 | Perform (1) or Compose (0) |
| 2 | Harmony page open |
| 3, 4, 5 | key (pitch class 0–11), mode family (0–4), mode (0–6) |
| 6–12 | alteration of degrees 1–7, plus 2 (so 2 is unaltered, 1 is flat, 3 is sharp) |
| 13 | Chord focus degree plus 1 (0 is none, the tonic) |
| 14 | chord size: 3, 4 or 5 |
| 15 | KEEP alterations |
| 16 | rows apart: 0 3rd, 1 4th, 2 5th, 3 octave |
| 17 | held notes: 0 Morph, 1 Hold, 2 Retrigger |
| 18 | style: 0 Pop, 1 Rock, 2 Blues, 3 Jazz, 4 Soul, 5 Cinematic, 6 Flamenco, 7 Free |
| 19 | template plus 1 (0 is off; templates are numbered 0–20 in the order HARMONY.md lists them) |
| 20 | the template's next step |
| 21 | lights: 0 Guide, 1 Chord, 2 Degrees, 3 Notes |
| 22 | hints on |
| 23 | pattern: 0 Sustain, 1 Pulse, 2 Strum, 3 Arpeggio |
| 24 | pattern clock: 0 Host, 1 Internal |
| 25 | Latch on |
| 26 | sounding left-hand chord's column plus 1 (0 is none; columns: Root, 1st, 2nd, 7th, Smart) |
| 27 | sounding chord's degree (0–6) |
| 28 | sounding chord's modifiers: bit 0 V/x, 1 Borrow, 2 sus4, 3 add9 |
| 29 | modifiers armed or held right now, same bits |
| 30 | octave transpose in octaves, plus 5 |
| 31, 32 | which memories are stored: bits 0–6, then bit 7 |
| 33 | the screen showing: 0 is normal play, otherwise the firmware's `DisplayMode` (1 Per-Split Settings, 2 Preset, 3 Volume, 4 Octave/Transpose, 6 and 7 Global Settings, 46 Harmony page, 47 LED check; the full list is the `DisplayMode` enum in `linnstrument-firmware.ino`) |
| 34 | layout: 0 scale steps, 1 chromatic, 2 chromatic with snap |
| 35 | zone side: 0 left, 1 right |
| 36 | zone width in Compose mode: 4, 5 or 6 (Perform mode always uses 6) |

**Frame**: the number of columns (26 on a LinnStrument 200, column 0 being the control buttons) and rows (8), then one byte per light, row by row from the bottom row, each row from column 0. Each byte holds the color in bits 0–3 (0 off, 1 red, 2 yellow, 3 green, 4 cyan, 5 blue, 6 magenta, 8 white, 9 orange, 10 lime, 11 pink, 12 teal, 13 azure, 14 violet) and how it's lit in bits 4–6 (0 off, 1 on, 2 fast blink, 3 slow blink, 4 blink with the tempo, 5 half brightness, 6 quarter brightness). Frames aren't sent in normal play, so nothing extra goes out while you play, nor during sleep and animations.

**Memories**: 8 slots of 13 bytes each: stored (1 or 0), key, family, mode, the 7 alterations plus 2, chord focus degree plus 1, and chord size.

The firmware side is at the end of `ls_harmony.ino` (`harmonyCompanionService` and `harmonyHandleSysEx`), with small hooks in `ls_midi.ino` and `ls_rtos.ino`. The app side is the "companion" section at the end of the script in `companion/index.html`. To test the app without a LinnStrument, call `window.__harmonyMirrorFeed([...])` in the browser console with the bytes of a message.
