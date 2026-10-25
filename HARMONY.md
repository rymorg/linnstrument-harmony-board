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
- **Saved across power-off.** The harmony, the 8 memories, Compose or Perform, Latch and every Harmony page setting come back when you plug the LinnStrument in again. See Saving below.
- **Layouts and zone placement.** Besides the scale-step layout, the playing surface can be chromatic, with scale tones lit, or chromatic with snap, where pads outside the scale play the nearest scale tone. The harmony zone can sit on either edge and be 4, 5 or 6 columns wide.

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
| 19 · Layout | Scale steps, Chromatic, Chromatic with snap (see Layouts) |
| 21 · Rows apart | 3rd, 4th, 5th, octave (in a 7-note scale) |
| 1 · LED check and reset | Top: the LED check. Bottom: reset the harmony, memories and every Harmony page setting to defaults: tap it (it blinks red), then tap again within 3 seconds |
| 3 · Zone side | Left, Right: which edge the harmony zone sits on. On the right it's mirrored, Key ♯♭ outermost, and the 5th pads point outward |
| 5 · Zone width | 6, 5, 4 columns: 5 drops the Memory column (PREV and NEXT still step memories), 4 drops Chord too. Perform mode always uses all 6 |
| 11 · Style | Pop (cyan), Rock (red), Blues (blue), Jazz (magenta), Soul (orange), Cinematic (green), Flamenco (pink), Free (yellow): the rules behind the lights and hints (see Styles and templates) |
| 13 · Template | Off (red), then the style's progression templates (blue), in the order listed under Styles and templates |
| 15 · Lights | Guide, Chord, Degrees, Notes: how the playing surface is colored (see What the lights mean) |
| 17 · Hints | On, Off: whether the harmony zone and chord pads color themselves by what would sound good next |
| 23 · Held notes | Morph (glide to the new harmony), Hold (keep their pitch), Retrigger (restart on the new notes) |
| 25 · Pattern | Sustain (green), Pulse (yellow), Strum (orange), Arpeggio (pink): how Perform-mode chords play. The bottom two pads pick the clock: Host (blue) or Internal (cyan) |

The current choice is white. 
### LED check

The top pad of column 1 on the Harmony page (violet) opens a test screen for the colors the lights work will use. Tap Split to go back to the Harmony page.

Each color has one column, with a dark column between neighbors:

| Column | 1 | 3 | 5 | 7 | 9 | 11 | 13 | 15 | 17 | 19 | 21 | 23 | 25 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Color | red | orange | yellow | lime | green | **teal** | cyan | **azure** | blue | **violet** | magenta | pink | white |

From the top, rows 8 and 7 are full brightness, rows 6 and 5 half brightness, and rows 4 and 3 a quarter. Teal, azure and violet are new: like orange and lime, each alternates between two colors on every refresh (teal: cyan and green; azure: cyan and blue; violet: magenta and blue). The LEDs light one column at a time, and dim pads go dark halfway (or a quarter of the way) through their column's turn, so they refresh as fast as full-brightness pads. Things to look for: whether teal, azure and violet read as their own colors next to their neighbors, whether the dim rows look steady or flicker, and whether a dim color still looks like its full-brightness version.

Layouts
-------

| Layout | Each column is | Rows apart |
|---|---|---|
| **Scale steps** (default) | the next note of the scale, so every pad is in the scale | a 3rd, 4th, 5th or octave in scale steps |
| **Chromatic** | the next semitone, starting from C2 on the bottom row; scale tones light up by the Lights setting, the others stay dark but play | 4, 5, 7 or 12 semitones (the Rows apart setting) |
| **Chromatic with snap** | the next semitone, but a pad outside the scale plays the nearest scale tone, looking down first; slides move in steps between scale tones | as Chromatic |

Changing the layout, the zone side or the zone width lets go of every note and chord.

Perform mode
------------

Tap **Split** so it turns magenta. Columns 1–5 become a chord split for the left hand; Memory (column 6) and the melody pads don't move, so you can switch mid-song.

| Column | Rows 2–8 (I at the bottom, VII at the top) | Row 1 (thumb strip) |
|---|---|---|
| 1 | root position | **V/x**: the next chord becomes the dominant of that degree (V/ii, V/IV…) |
| 2 | 1st inversion (3rd in the bass) | **Borrow**: the next chord comes from the parallel major or minor |
| 3 | 2nd inversion (5th in the bass) | **sus4** |
| 4 | Color: the style's signature chord (see below) | **add9** |
| 5 | Smart: the voicing nearest the last chord, with a walking bass | **Latch**, on by default |

- **Chords.** A chord pad plays the chord and a bass note an octave or two below, with the pad's velocity. With Latch on, the chord keeps sounding after you let go; tap the same pad to stop it, or another pad to change it. With Latch off, it stops when you let go.
- **Color (column 4).** Each style plays the chord it's known for, built from the current scale: Pop add9, Rock power chords (root, 5th, octave), Blues 9ths (dominant 9ths on I, IV and V), Jazz 9ths with the 13th on dominant chords, Soul minor 11ths and otherwise 9ths, Cinematic an open 5th and 9th (neither major nor minor), Flamenco a 7th with 7♭9 on I, and Free the plain 7th chord. With V/x it plays the dominant 7th.
- **Smart (column 5).** It plays the chord size set in the Chord column, in the inversion and octave nearest the last chord, and walks the bass like a pianist's left hand: the root when it's a step away from the last bass note, otherwise the 3rd or 7th when one of those is (I–V–vi–IV in C gives C, G/B, Am, F/A), otherwise a leap to the root. The 5th never goes in the bass. Play a progression with Smart to hear it; a single chord on its own sounds like the other columns.
- **Expression.** While you hold a chord pad, its Y sends CC74 and its pressure sends channel pressure, both on channel 16.
- **Thumb strip.** Tap V/x, Borrow, sus4 or add9 to arm it for the next chord (it lights white), or hold it while you press chord pads. Tap an armed one again to disarm it.
- **Harmony changes.** Memories and footswitches still change the harmony in Perform mode. A sounding chord follows to the same degree in the new harmony, and re-strikes if its notes changed. With Held notes on Hold, it keeps ringing until the next chord.
- **Patterns.** The Pattern column on the Harmony page picks how a chord plays. Sustain holds it. The others play it on a 16th-note grid, starting with a hit as you press the pad:
  - **Pulse:** the chord on every 8th note, accented on the beat, with the bass on each beat.
  - **Strum:** a down-strum on each beat and quick up-strums on the "and" of beats 2 and 4, with the bass on beats 1 and 3.
  - **Arpeggio:** the chord tones and the octave one at a time on 8th notes, over a long bass note.

  Hits follow the pad's velocity. The bar starts on the beat you press the pad. A harmony change goes into the next hit, without re-striking. The clock pads at the bottom of the Pattern column pick what patterns follow. **Host**, the default, follows the DAW's MIDI clock while the DAW plays and sends clock to the LinnStrument, and the Global Settings tempo otherwise. **Internal** always uses the Global Settings tempo and ignores incoming clock.
- **Melody lights** follow the chord the left hand is playing.

In pentatonic and symmetric scales, rows above the scale's length are dark, and Borrow is off.

Footswitches
------------

Footswitches and the Switch 1 and 2 buttons keep their factory assignments, so a sustain pedal stays a sustain pedal. To step through memories with your feet, assign a switch to the sequencer's **PREV** or **NEXT**. While Harmony mode is on, those step to the previous or next stored memory; with it off, they do their factory sequencer job.

1. Press **Global Settings**.
2. In column 7, select the switch: the bottom pad is the left footswitch, the second is the right footswitch, then Switch 2 and Switch 1. The LinnStrument has one footswitch jack for one or two pedals; a single pedal works as the **right** footswitch (sustain at first), so that's the one to change.
3. Hold the custom assignment pad (column 9, fourth from the bottom) until its screen opens. It shows the current choice, **TAP** (tap tempo) at first. Let go, then slide a finger slowly to the right across the pads, one pad at a time: each pad steps to the next name (TAP, LEG, LAT, PR+, PR−, PCH, PLAY, **PREV**, **NEXT**…), and sliding left steps back. Stop on **PREV** or **NEXT** and lift your finger to assign it.
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

Styles and templates
--------------------

A style changes the rules behind the lights and hints, not the notes you play. Picking one also sets its usual chord size (the Chord column's top pad) and turns the template off.

| Style | Chord size | What changes |
|---|---|---|
| Pop | 7th | Tension chords lead home. Natural moves: up a 4th, down a 3rd, up a step. V/x is the bright modifier. |
| Rock | triad | ♭VII, ♭VI and IV back to I: IV, ♭VII and V pulse the tonic. Only the note a half step above the root is marked to pass through. Blue notes (♭3, ♭5) over major chords light violet. Borrow is the bright modifier. |
| Blues | 7th | I, IV and V play as dominant 7ths. V pulls hardest home, and V leads to IV or I. Blue notes light violet, and nothing is marked to pass through. |
| Jazz | 7th | ii–V chains: the chord a 5th below always pulses. Altered notes (♭9, ♯9, ♯11, ♭13) on dominant chords light violet. In Perform mode, Borrow plays the tritone substitute instead, the dominant 7th a half step above the chord. |
| Soul | 9th | Bass lines that walk down; the chord a 5th below pulses. Borrow and add9 are the bright modifiers. |
| Cinematic | 7th | Chords are judged by how many notes they share with home (azure: 2 or more, green: 1, orange: none), and chords built on the mode's signature notes pulse. Those notes light violet. sus4 and add9 are the bright modifiers. |
| Flamenco | triad | Phrygian with a major I (and a ♭9 on 9th chords). ♭II pulls hardest home, and iv, ♭III and ♭II pulse the next step down. The ♭2 lights violet. |
| Free | — | No judgments: only chord tones light up, every chord pad is blue, and every modifier stays bright. |

A template sets its mode in the current key, and the next chord of its progression blinks in time on the Chord column (or the chord pads in Perform mode), whether hints are on or off. Play that chord and the next one blinks. In Compose mode, while a template is on, tapping the lit Chord pad again doesn't clear it.

| Style | Templates, top pad down |
|---|---|
| Pop | I–V–vi–IV, vi–IV–I–V, I–vi–IV–V (Ionian) |
| Rock | I–♭VII–IV (Mixolydian), i–♭VI–♭VII (Aeolian), I–IV–V–IV (Ionian) |
| Blues | 12-bar, quick-change 12-bar, 8-bar (Mixolydian) |
| Jazz | ii–V–I, I–vi–ii–V, iii–vi–ii–V–I (Ionian), minor ii–V–i (Aeolian with a raised 7th) |
| Soul | IV–iii–ii–I (Ionian), i–iv (Dorian), ii–V–I (Ionian) |
| Cinematic | i–♭VI–♭III–♭VII (Aeolian), I–II (Lydian), i–♭VII–♭VI–♭VII (Aeolian) |
| Flamenco | Andalusian iv–♭III–♭II–I, I–♭II (Phrygian) |

What the lights mean
--------------------

**Playing surface.** The Lights column on the Harmony page picks one of four schemes. In each, "the chord" is the lit chord: the tonic, the Chord pad you picked, or in Perform mode the chord the left hand is playing.

| Lights | What you see |
|---|---|
| **Guide** (default) | cyan for the chord's root, green for its other tones, azure for color tones (good to hold), and dim red for a note a half step above a chord tone (tense if held, so pass through it) |
| Chord | only the chord's tones (cyan root, green) and altered notes (orange) |
| Degrees | one hue per scale degree: red, orange, yellow, green, teal, blue, violet for 1–7, bright for chord tones and dim for the rest |
| Notes | one hue per note name around the circle of fifths, so a C is always red; bright for chord tones, dim for the rest |

Notes that a harmony change brings in blink for about a second and a half. With the factory Note Lights setting "Same" (Per-Split Settings), touching a pad also lights every other playing pad with that note, in the harmony layout and never in the harmony zone; the harmony zone only changes when you change the harmony or chords.

**Harmony zone, hints on.** Pads that change the harmony (keys, fifths, REL, PAR, modes, memories) show how many notes would change: white for none, green for one, yellow for two, orange for three or more (dim orange for four or more). The memory the next NEXT press would recall blinks in time with the tempo.

Chord pads show what the chord does (the rules follow the style; these are Pop's):

| Color | Function |
|---|---|
| Azure | home |
| Green | away (it holds the 4th degree) |
| Orange | tension (it holds the leading tone; in modes without one, v and ♭VII) |
| Red | the strongest pull home (the leading tone and the note a tritone above it, like V7) |

They're bright for a natural next move from the current chord and dim otherwise, and the chord the style wants next blinks in time (in Pop, the tonic while a tension chord is playing). With V/x armed, the chord pads turn red. On the thumb strip, the modifiers the style leans on stay bright and the others are dim. Alter pads show raised degrees magenta and lowered ones orange.

**Hints off**, and anywhere hints don't apply:

| Color | Harmony zone |
|---|---|
| Cyan | the key's root |
| Green | notes in the scale (Key columns) |
| Orange | altered degrees |
| White | the selected mode, chord or memory |
| Blue | available alterations, chords and stored memories |
| Yellow | action pads: fifths, REL, PAR, KEEP |
| Lime, pink, magenta, red, yellow | the mode family (Mode column) |
| Red | RESET when anything is altered |

In Perform mode, chord pads are blue, Smart pads lime, and the sounding chord white. On the thumb strip, V/x is orange, Borrow pink, sus4 and add9 yellow, Latch green while it's on, and an armed modifier white. The Split button is cyan in Compose mode, magenta in Perform mode and white while the Harmony page is open.

Saving
------

The harmony, the memories and the Harmony page settings are saved on their own, a few seconds after you stop changing them, while no pad is touched (writing to flash pauses the LinnStrument for a few milliseconds). A playing chord pattern holds the save back for up to 20 seconds. Turning on OS Update saves right away, so a flash helper backup always has the latest. Nothing is saved if nothing changed.

They're kept apart from the factory settings, so those keep their size and stay compatible with the official Updater. Each save goes to the next of 16 flash pages, with a sequence number and a checksum, and at power-up the LinnStrument loads the newest page that checks out. If the power goes off in the middle of a save, the previous save is still there, and spreading saves over 16 pages makes the flash last 16 times longer.

Flashing erases everything, saved harmony included, so the flash helper backs it up and restores it along with the settings. A LinnStrument that's never saved anything, or a fresh flash restored from a backup without it, starts with D Dorian and the four example memories.

Build
-----

The firmware builds with the Arduino Due core 1.6.12 and its GCC 4.8.3 toolchain, the same as the stock firmware. On Apple Silicon, that toolchain needs Rosetta. arduino-cli also needs the project folder named like the main sketch file, so clone it as `linnstrument-firmware`:

```bash
git clone https://github.com/rymorg/linnstrument-harmony-board.git linnstrument-firmware
```

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

To install a ready-made release without building anything, follow [INSTALL.md](INSTALL.md). This section is for flashing your own build.

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

   With no file, it downloads and flashes the latest release instead. It saves a backup in `~/linnstrument-backups/` (settings, calibration, sequencer projects, and the Harmony Board's saved harmony and memories), asks before erasing, flashes and verifies, then restores the backup. Leave the LinnStrument alone until it says Done.
5. The LinnStrument comes back in MIDI mode with its settings. If the backup had no calibration in it, it opens the calibration screen instead: follow the lights, sliding a finger along each lit row, then along each lit column. Finishing calibration also saves the restored settings.

The [browser flasher](https://rymorg.github.io/linnstrument-harmony-board/flash/) (`companion/flash/`) does the same from Chrome or Edge without the Python setup: choose **Use a different file instead** and pick `build/linnstrument-firmware.ino.bin` together with its `.elf`. It talks to the SAM3X's ROM bootloader itself (SAM-BA over the USB bridge, like bossac), and its backups are the same `.lsbackup` files.

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
12. **Voicings:** try the same degree in columns 1–4 and check the bass note and spread change; column 4 plays the style's color chord (in Pop, an add9). Then tap Key C and Ionian, and play I, V, vi, IV in column 5 (Smart): the chords move as few notes as they can, and the bass walks C, B, A, A (C, G/B, Am, F/A).
13. **Thumb strip:** tap V/x, then IV in the color column (column 4). You get the dominant of IV: D7 in D Dorian. Hold sus4 while tapping chords and they come out suspended.
14. **Expression:** hold a chord pad and rock your finger up and down, then press harder. The chord track gets CC74 and channel pressure on channel 16.
15. **Harmony changes:** latch a chord, then tap Memory 2 or 3. The chord re-strikes as the same degree in the new harmony. Set Held notes to Hold and it rings on instead.
16. **Patterns:** hold Split, pick Pulse in column 25, and tap Split to close the page. Latch a chord: it pulses in time. Try Strum and Arpeggio, then turn on MIDI clock output to the LinnStrument in your DAW (in Ableton: Settings, Link/Tempo/MIDI, Sync on for the LinnStrument output) and press Play; the pattern follows the DAW's tempo. Pick Internal (bottom pad of column 25) and it goes back to the LinnStrument's own tempo while the DAW keeps playing.
17. **Channels:** the melody track shouldn't sound the chords. If it does, it's listening on all 16 channels; let me know your DAW and setup.

Known limits
------------

- Morph only works in channel-per-note (MPE) mode with pitch X enabled. In one-channel mode, held notes keep their pitch.
- Chords share channel 16, so a sustained chord re-strikes on a harmony change instead of gliding.
- On the Host clock, if the DAW stops sending clock without a Stop message, patterns stop too, and their notes end after half a second. Memories don't store the pattern yet.
- Low-row functions are off in Harmony mode, and splits are unavailable.
- Everything above has been tested on a LinnStrument 200. The LinnStrument 128 hasn't been tested. See [HARMONY-STATUS.md](HARMONY-STATUS.md) for what's done and what's left.
