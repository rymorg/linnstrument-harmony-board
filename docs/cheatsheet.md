Harmony Board cheat sheet
=========================

Columns count from the control buttons (column 0); rows count from the bottom (row 1 is nearest you). This is the default layout: the zone on the left, 6 columns wide. [HARMONY.md](../HARMONY.md) has every detail.

![The Harmony Board in Compose mode](images/harmony-board-compose.jpg)

Control buttons
---------------

| Button | Does |
|---|---|
| **Split** | Tap: switch Compose (cyan) and Perform (magenta). Hold half a second: the Harmony page (white). |
| Everything else | Its factory job. Global Settings, Per-Split Settings, Preset, Volume, Octave/Transpose, Switch 1 and 2 work as usual. |

Which version? Global Settings → **VER** (column 16, second from the bottom) shows `234`, the stock firmware it's built on; tap once for the Harmony Board version, such as `H0.1`.

Compose mode: the harmony zone
------------------------------

| Column | Rows 1–7 | Row 8 (top) |
|---|---|---|
| 1 · Key ♯/♭ | C♯, D♯, **REL**, F♯, G♯, A♯, **PAR** | down a 5th |
| 2 · Key | C, D, E, F, G, A, B | up a 5th |
| 3 · Mode | the family's 7 modes, darkest at the bottom | next mode family |
| 4 · Alter | **RESET**, then degrees 2–7 (tap to flip ♭/♮/♯) | **KEEP** alterations |
| 5 · Chord | I–VII (tap again to go back to the tonic) | chord size: triad, 7th, 9th |
| 6 · Memory | slots 1–7: tap empty to store, tap to recall, hold to clear | slot 8 |

**REL** keeps the notes and moves to the relative major or minor; **PAR** keeps the root and switches major and minor.

Perform mode: left-hand chords
------------------------------

| Column | Rows 2–8 (I at the bottom) | Row 1 (thumb strip) |
|---|---|---|
| 1 | root position | **V/x**: the dominant that leads into the next chord |
| 2 | 1st inversion | **Borrow** from parallel major/minor (Jazz: tritone sub) |
| 3 | 2nd inversion | **sus4** |
| 4 | 7th chord | **add9** |
| 5 | Smart: nearest voicing | **Latch** |
| 6 | Memory, as in Compose mode | |

Chords and their bass go out on **MIDI channel 16**. Tap a modifier for the next chord, or hold it while you play chords.

The Harmony page (hold Split)
-----------------------------

| Column | From the top pad down |
|---|---|
| 1 | LED check (top) · Reset to defaults (bottom: tap, then tap again within 3 seconds) |
| 3 | Zone side: Left, Right |
| 5 | Zone width: 6, 5, 4 |
| 7 | Harmony mode: On, Off |
| 9 | Compose, Perform |
| 11 | Style: Pop, Rock, Blues, Jazz, Soul, Cinematic, Flamenco, Free |
| 13 | Template: Off, then the style's progressions |
| 15 | Lights: Guide, Chord, Degrees, Notes |
| 17 | Hints: On, Off |
| 19 | Layout: Scale steps, Chromatic, Chromatic with snap |
| 21 | Rows apart: 3rd, 4th, 5th, octave |
| 23 | Held notes: Morph, Hold, Retrigger |
| 25 | Pattern: Sustain, Pulse, Strum, Arpeggio · bottom two pads: clock Host, Internal |

The current choice is white. Tap Split to go back to playing.

Lights
------

| Playing surface (Guide) | Harmony zone (hints on) |
|---|---|
| **Cyan**: the chord's root | Key, Mode, Memory pads: **white** same notes, **green** 1 note changes, **yellow** 2, **orange** 3 or more |
| **Green**: other chord tones | Chord pads: **azure** home, **green** away, **orange** tension, **red** strongest pull home |
| **Azure**: color tones, good to hold | Bright: a natural next move. Dim: a weaker one. Blinking: where the style (or template) wants to go next |
| **Dim red**: a half step above a chord tone, pass through it | A blinking Memory pad is the next one a NEXT footswitch recalls |
| **Violet**: the style's flavor notes | |

Footswitches
------------

Assign a footswitch or switch to the sequencer's **PREV** or **NEXT** in Global Settings (pick the switch in column 7, then hold the custom-assignment pad, column 9, fourth from the bottom, and choose PREV or NEXT from the list) to step through memories hands-free. Other assignments, like Sustain, work as usual.
