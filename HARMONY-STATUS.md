Harmony Board: status and next steps
====================================

Where the work stands as of October 23, 2026. [HARMONY.md](HARMONY.md) is the user guide for what's in the firmware; this file tracks what's done and what's left.

Where things live
-----------------

- **Branch:** `harmony-board`, pushed to `origin/harmony-board`.
- **Firmware code:** almost everything is in `ls_harmony.ino`. Hooks into the stock code are small and live in `ls_handleTouches.ino`, `ls_displayModes.ino`, `ls_settings.ino` (Split button), `ls_switches.ino` (PREV/NEXT), `ls_lowRow.ino`, `ls_rtos.ino` and `ls_midi.ino` (clock ticks and same-note lights), and `ls_leds.ino` (new colors and dimming).
- **Flash helper:** `tools/flash_keep_settings.py` backs up settings, calibration and sequencer projects over serial, flashes, and restores them. Set it up once with the commands in HARMONY.md. Backups go to `~/linnstrument-backups/`.
- **Mockup:** the playable web version is the private artifact at https://claude.ai/artifact/1fP479mH7pkHc9nNYZKw6w. Its Build plan section has a "What's in the firmware" table, and FW tags and ✓ marks show which of its settings the firmware has.

Done
----

| Commit | What | Tested on the LinnStrument |
|---|---|---|
| `067f01f`, `a1c2de3` | Flash helper that keeps settings and calibration, and the fix for the restart that opening the serial port causes | Yes, every flash since |
| `2f0d933` | Harmony zone (Key, Mode, Alter, Chord, Memory), scale-step layout, scale-aware slides, Morph/Hold/Retrigger, memories with PREV/NEXT footswitches, Split as the Harmony button (tap: Compose/Perform, hold: Harmony page), Perform-mode chord split with thumb strip and Latch, chords and bass on MIDI channel 16 | Yes |
| `c3bd7aa` | Chord patterns (Sustain, Pulse, Strum, Arpeggio) with a Host or Internal clock | Yes, including following Ableton's MIDI clock |
| `cfd0f78` | Teal, azure and violet blends, half and quarter brightness, LED check page | Yes: all 13 colors distinct, dim levels steady |
| `6b7d194` | Lights (Guide, Chord, Degrees, Notes) and hints | Yes |
| `f1feb74` | Eight styles and 21 progression templates; same-note lights fixed for the harmony layout; melody-fit chord coloring removed | Yes: playing is visually clean |

Lessons from hardware testing, worth keeping in mind:

- Flashing directly through the USB bridge erases settings and calibration. Always use the flash helper.
- Opening the serial port restarts the LinnStrument and sometimes drops USB for a second or two; the helper waits that out.
- Dimming LEDs by skipping refreshes flickers badly. The working dimming turns pads off partway through each column's refresh.
- Incoming MIDI clock advances the clock in `ls_midi.ino`, not the main loop, so anything driven by the clock needs a hook there too.
- Lights that change on every melody touch are distracting on the real board. Keep the harmony zone still while playing.

Pending
-------

In the order I'd suggest:

1. **Save settings across power-off.** Right now the harmony, the 8 memories, and the Harmony page settings (Compose/Perform, Latch, style, template, lights, hints, rows apart, held notes, pattern, clock) reset when the LinnStrument is unplugged. They should be stored with the factory settings, in a way the flash helper's backup and restore still handle: today the helper checks that the backup's settings size matches the new firmware's. While doing this, let memories also store the pattern and style, as the mockup's do.
2. **Layout options**, the last Harmony page column (column 19): Scale steps (today's only layout), Chromatic with scale lights, and Chromatic with snap to scale. The mockup also has zone side (left or right) and zone width (4, 5 or 6 columns).
3. **Open checks:**
   - Does the melody MPE track also sound the channel 16 chords in Ableton (test step 17 in HARMONY.md)? Not reported yet.
   - The flash helper's tests use a simulated LinnStrument that doesn't restart when the port opens. Adding that would cover the bug found on hardware.
4. **Open design questions** from the mockup:
   - Should harmony changes also go out over MIDI (NRPN or SysEx), so a DAW or plugin can follow along?
   - Should Memory also store the left-hand chord, so the footswitch can step through a whole progression?
   - Should the chord channel (16) and the melody's MPE member channels (2–8) be settable?
   - The firmware targets the LinnStrument 200 (25 playing columns). The LinnStrument 128 hasn't been considered or tested.
