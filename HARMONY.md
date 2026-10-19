Harmony Board firmware
======================

This branch turns the left six columns of a LinnStrument 200 into a harmony zone and plays the other 19 columns in the current scale, with full MPE. The playable mockup that this firmware follows is the reference for the pad layout.

What's in this build
--------------------

- **Harmony mode is on at power-up.** Split is the Harmony button: tap it to switch between Compose and Perform, hold it for the Harmony page. On the Harmony page you can also turn Harmony mode off to get the stock layout back.
- **Compose and Perform.** In Compose mode, columns 1–5 are the harmony controls (Key, Mode, Alter, Chord). In Perform mode, they become a left-hand chord split that plays chords and a bass note on MIDI channel 16, while the melody keeps MPE.
- **Scale-step layout.** Each column is the next note of the scale, and rows are three scale steps apart (a 4th in a 7-note scale) unless you change it on the Harmony page. The Octave/Transpose button still shifts octaves.
- **Scale-aware slides.** Sliding sideways glides through the row's notes and lands on scale tones at pad centers.
- **Held notes.** Notes you hold while changing the harmony glide to the same scale degree in the new harmony, using per-note pitch bend (Morph). The Harmony page can switch this to Hold, so held notes keep their pitch, or Retrigger, so they restart on their new notes.
- **Memories.** Store and recall harmonies on the Memory column, and step through them with any switch assigned to PREV or NEXT.

Not in this build yet: chord patterns (pulse, strum, arpeggio), styles and templates, the color guidance and hints, layout options, and saving the harmony, memories and page settings across power cycles. The mockup marks which of its settings the firmware has.

Control buttons
---------------

Every control button keeps its factory job except Split:

| Button | In this firmware |
|---|---|
| Global Settings | Factory: calibration, MIDI I/O, OS update and switch assignments |
| **Split** | **The Harmony button.** Tap to switch Compose (lit cyan) and Perform (lit magenta). Hold for half a second to open the Harmony page (lit white); tap again to close it. With Harmony mode off, a tap turns it back on. |
| Switch 1, Switch 2 | Factory, assignable (defaults: Sustain and Arpeggiator) |
| Octave/Transpose, Volume, Preset, Per-Split Settings | Factory |

Splits are the one factory feature this firmware gives up: the harmony zone and a left split both need the left side of the board.

The Harmony page
----------------

Hold **Split** for half a second to open the page, which takes over the playing surface like the factory settings screens. The Split button turns white. Tap Split again to go back to playing. Columns are counted from the control buttons, and options read from the top pad down:

| Column | Top pad down |
|---|---|
| 7 · Harmony | On, Off (stock layout) |
| 9 · Mode | Compose, Perform |
| 21 · Rows apart | 3rd, 4th, 5th, octave (in a 7-note scale) |
| 23 · Held notes | Morph (glide to the new harmony), Hold (keep their pitch), Retrigger (restart on the new notes) |

The current choice is white. The dark columns in between are where the mockup's other settings (style, template, lights, hints, layout and pattern) will go.

Perform mode
------------

Tap **Split** so it turns magenta. Columns 1–5 become a chord split for the left hand; Memory (column 6) and the melody pads don't move, so you can switch mid-song.

| Column | Rows 2–8 (I at the bottom, VII at the top) | Row 1 (thumb strip) |
|---|---|---|
| 1 | root position | **V/x**: the next chord becomes the dominant of that degree (V/ii, V/IV…) |
| 2 | 1st inversion (3rd in the bass) | **Borrow**: the next chord comes from the parallel major or minor |
| 3 | 2nd inversion (5th in the bass) | **sus4** |
| 4 | 7th chord | **add9** |
| 5 | Smart: the voicing nearest the last chord | **Latch**, on by default |

- **Chords.** A chord pad plays the chord and a bass note an octave or two below, with the pad's velocity. With Latch on, the chord keeps sounding after you let go; tap the same pad to stop it, or another pad to change it. With Latch off, it stops when you let go.
- **Expression.** While you hold a chord pad, its Y sends CC74 and its pressure sends channel pressure, both on channel 16.
- **Thumb strip.** Tap V/x, Borrow, sus4 or add9 to arm it for the next chord (it lights white), or hold it while you press chord pads. Tap an armed one again to disarm it.
- **Harmony changes.** Memories and footswitches still change the harmony in Perform mode. A sounding chord follows to the same degree in the new harmony, and re-strikes if its notes changed. With Held notes on Hold, it keeps ringing until the next chord.
- **Melody lights** follow the chord the left hand is playing.

In pentatonic and symmetric scales, rows above the scale's length are dark, and Borrow is off.

Footswitches
------------

Footswitches and the Switch 1 and 2 buttons keep their factory assignments, so a sustain pedal stays a sustain pedal. To step through memories with your feet, assign a switch to the sequencer's **PREV** or **NEXT**. While Harmony mode is on, those step to the previous or next stored memory; with it off, they do their factory sequencer job.

1. Press **Global Settings**.
2. In column 7, select the switch: the bottom pad is the left footswitch, the second is the right footswitch, then Switch 2 and Switch 1.
3. Touch the custom assignment pad (column 9, fourth from the bottom) to open its screen, step through the names until it shows **PREV** (or **NEXT**), and let go to assign it.
4. Repeat for the other footswitch, then press **Global Settings** to leave.

These are existing factory assignments, so the stored settings stay valid if you go back to the stock firmware.

The harmony zone
----------------

Rows are counted from the bottom (row 1 is nearest you). Column 0 is the control buttons.

| Column | Rows 1–7 | Row 8 (top) |
|---|---|---|
| 1 · Key ♯/♭ | C♯, D♯, **REL**, F♯, G♯, A♯, **PAR** | ◀ down a 5th |
| 2 · Key | C, D, E, F, G, A, B | up a 5th ▶ |
| 3 · Mode | the 7 modes of the family, darkest at the bottom, brightest at the top | next family |
| 4 · Alter | **RESET**, then degrees 2–7: each tap flips ♮2/♭2, ♮3/♭3, ♮4/♯4, ♮5/♭5, ♮6/♭6, ♮7/♭7 | **KEEP** alterations when the mode changes |
| 5 · Chord | I–VII: lights that chord on the surface, tap again to go back to the tonic | chord size: triad (lime), 7th (yellow), 9th (orange) |
| 6 · Memory | slots 1–7 | slot 8 |

- **REL** keeps the notes and moves to the relative major or minor.
- **PAR** keeps the root and switches between major (Ionian) and minor (Aeolian).
- **Mode families**, in order: major, melodic minor, harmonic minor, harmonic major, then pentatonic and symmetric (major pentatonic, minor pentatonic, blues, whole tone, both diminished scales, chromatic). Alter and Chord work with 7-note scales only.
- **Memory:** tap an empty slot to store the current harmony, tap a stored slot to recall it, and hold a stored slot for about a second to clear it. Slots 1–4 start with examples: D Dorian, G Mixolydian, C Lydian, and A minor with a raised 7th.

What the lights mean
--------------------

| Color | Playing surface | Harmony zone |
|---|---|---|
| Cyan | root of the lit chord (the tonic unless a Chord pad is selected) | the key's root |
| Green | other tones of the lit chord | notes in the scale (Key columns) |
| Orange | altered notes | altered degrees |
| White | — | the selected mode, chord or memory |
| Blue | — | available alterations, chords and stored memories |
| Yellow | — | action pads: fifths, REL, PAR, KEEP |
| Lime, pink, magenta, red, yellow | — | the mode family (Mode column) |
| Red | — | RESET when anything is altered |

In Perform mode, chord pads are blue, Smart pads lime, and the sounding chord white. On the thumb strip, V/x is orange, Borrow pink, sus4 and add9 yellow, Latch green while it's on, and an armed modifier white. The Split button is cyan in Compose mode, magenta in Perform mode and white while the Harmony page is open.

The other scale tones are unlit on purpose: in this layout every pad is in the scale.

Build
-----

The firmware builds with the Arduino Due core 1.6.12 and its GCC 4.8.3 toolchain, the same as the stock firmware. On Apple Silicon, that toolchain needs Rosetta.

```bash
softwareupdate --install-rosetta --agree-to-license
```

```bash
brew install arduino-cli
```

```bash
arduino-cli core install arduino:sam@1.6.12
```

```bash
arduino-cli compile --fqbn arduino:sam:arduino_due_x --libraries libraries --output-dir build .
```

The firmware image is `build/linnstrument-firmware.ino.bin`.

Flash
-----

Resetting the LinnStrument into its bootloader erases its whole flash, settings and calibration included, so a plain upload always starts from factory defaults. `tools/flash_keep_settings.py` works like the official Updater: it saves the settings and sequencer projects over serial, flashes, and sends them back to the new firmware.

1. Set up the helper once:

   ```bash
   python3 -m venv tools/.venv
   ```

   ```bash
   tools/.venv/bin/pip install -r tools/requirements.txt
   ```

2. Connect the LinnStrument over USB and press **Global Settings**.
3. In the Actions column (column 16), briefly press **OS Update**, the third pad from the bottom, so its light turns on. Don't hold it. The USB port switches from MIDI to serial. Use one finger and keep the rest of column 16 untouched: pressing OS Update together with the bottom pad of that column resets all settings to factory defaults (calibration is kept).
4. Flash:

   ```bash
   tools/.venv/bin/python tools/flash_keep_settings.py build/linnstrument-firmware.ino.bin
   ```

   It saves a backup in `~/linnstrument-backups/`, asks before erasing, flashes and verifies, then restores the backup. Leave the LinnStrument alone until it says Done.
5. The LinnStrument comes back in MIDI mode with its settings. If the backup had no calibration in it, it opens the calibration screen instead: follow the lights, sliding a finger along each lit row, then along each lit column. Finishing calibration also saves the restored settings.

If a run stops partway, the backup is still on disk; `--restore FILE` sends it back, with or without a `.bin` to flash first. Opening the serial port restarts the LinnStrument, and its USB sometimes drops out for a second or two while it does; the helper waits that out.

To flash without the helper, use `arduino-cli upload --fqbn arduino:sam:arduino_due_x --port /dev/cu.usbmodem… --input-dir build .` with OS Update on. That erases settings and calibration: calibrate when it restarts, turn OS Update off, then redo the MPE setup and any switch assignments. To go back to stock firmware at any time, use the LinnStrument Updater from rogerlinndesign.com.

Set up MPE
----------

Morph and per-note slides need MPE. The flash helper keeps this setting; after a flash without it, the MIDI settings are back to one channel with a ±2 bend range:

1. Press **Per-Split Settings**.
2. Hold **Channel Per Note** (column 1, second row from the top) until it changes color. That turns on MPE with member channels 2–8, pitch bend ±48, Y on CC74 and pressure on channel pressure.
3. Press **Per-Split Settings** again to return to playing.

Test in a DAW
-------------

Use any MPE-capable instrument with its pitch bend range set to 48 semitones and its MIDI input set to MPE: for example Ableton Live's Drift or Wavetable, Bitwig's Polymer, or Logic's Alchemy. The melody uses channel 1 and member channels 2–8.

For Perform mode, add a second instrument track for the chords, a pad or piano sound, with its MIDI input set to the LinnStrument on channel 16 only. It doesn't need MPE.

1. **Lights at power-up:** the harmony zone is lit and the surface shows D Dorian's tonic chord: cyan D, green F, A and C.
2. **Notes:** play a row and check the piano roll: D E F G A B C, repeating.
3. **Key and mode:** tap Key C, then Lydian (top of column 3). The lights and notes follow, and the F pads now play F♯.
4. **Slides:** press a pad and slide one pad to the right. The pitch glides to the next scale tone, a whole or half step depending on the pair.
5. **Morph:** hold a chord with one hand and tap a different Mode or Key pad with the other. The held notes glide to their new pitches without retriggering.
6. **Chord focus:** tap Chord V, and only that chord's tones light up.
7. **Memories:** tap an empty Memory pad to store. With footswitches assigned to PREV and NEXT (see Footswitches), step through slots 1–4 and your new one. A footswitch left on Sustain should still sustain.
8. **Harmony page:** press Split. Pick Rows apart 5th and check that the rows now start five scale steps apart, then pick Hold and check that held notes keep their pitch through a Mode change.
9. **Back to stock:** on the Harmony page, pick Off. The stock chromatic layout returns. Pick On to come back.
10. **Split tap and hold:** tap Split. It turns magenta and columns 1–5 show the chord split. Hold it for half a second and the Harmony page opens; tap it to close the page.
11. **Chords:** tap Memory 1 (D Dorian, bottom of column 6), then I in root position (column 1, second row from the bottom). The chord track plays D minor with a D bass on channel 16 and keeps playing after you let go. The melody lights show the chord. Tap it again to stop.
12. **Voicings:** try the same degree in columns 2–5 and check the bass note and spread change. Smart moves as few notes as it can from the last chord.
13. **Thumb strip:** tap V/x, then IV in the 7th-chord column (column 4). You get the dominant of IV: D7 in D Dorian. Hold sus4 while tapping chords and they come out suspended.
14. **Expression:** hold a chord pad and rock your finger up and down, then press harder. The chord track gets CC74 and channel pressure on channel 16.
15. **Harmony changes:** latch a chord, then tap Memory 2 or 3. The chord re-strikes as the same degree in the new harmony. Set Held notes to Hold and it rings on instead.
16. **Channels:** the melody track shouldn't sound the chords. If it does, it's listening on all 16 channels; let me know your DAW and setup.

Known limits
------------

- Morph only works in channel-per-note (MPE) mode with pitch X enabled. In one-channel mode, held notes keep their pitch.
- Chords share channel 16, so they re-strike on a harmony change instead of gliding.
- No chord patterns yet: chords sustain until you change or stop them.
- Low-row functions are off in Harmony mode, and splits are unavailable.
- Harmony, the Harmony page settings, Compose or Perform, Latch and memories reset at power-off.
- Compose mode has been tested on hardware. Perform mode, Split tap and hold, and Retrigger have been compiled and their chord voicings checked on a computer, but not yet tested on the LinnStrument.
