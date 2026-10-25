Changelog
=========

All notable changes to the Harmony Board. Versions before 1.0 are experimental: anything can change between them, and changes that affect saved data, backups or the companion protocol are listed under **Breaking**.

## Unreleased

### Changed

- Perform mode, column 4 is now the **color** column: each style plays the chord it's known for instead of always a 7th (Pop add9, Rock power chords, Blues 9ths, Jazz 9ths and 13ths, Soul minor 11ths and 9ths, Cinematic open 5th and 9th, Flamenco 7♭9 on I, Free the plain 7th). V/x there still plays a dominant 7th.
- Perform mode, Smart (column 5) walks the bass: the root when it's a step from the last bass note, otherwise the 3rd or 7th when one of those is, otherwise a leap to the root (I–V–vi–IV in C: C, G/B, Am, F/A). Before, Smart's bass was always the root, so on a single chord it sounded the same as the other columns.
- sus4 leaves chords without a 3rd (power chords, open 5ths) unchanged.
- A footswitch on PREV or NEXT never makes a sound by itself: it stops a sounding Perform-mode chord instead of re-striking it in the new harmony.

### Added

- Chord sound On/Off on the Harmony page (column 9, below Compose and Perform): with it off, chord pads still light their chord and steer the hints, but send nothing on channel 16. Useful when a DAW or another player handles the chords.
- The companion state message has a new field 37, chord sound.

### Breaking

- The saved Harmony Board data is now format 3 (one more setting). This firmware reads formats 1 and 2, but older firmware can't read format 3, so going back to 0.1.0 loses the harmony, memories and Harmony page settings, even from a backup made with this version.

## [0.1.0] - 2026-10-24

The first public pre-release.

### Added

- Browser flasher at https://rymorg.github.io/linnstrument-harmony-board/flash/: installs and updates from Chrome or Edge with nothing to download. It backs up settings, calibration, sequencer projects and Harmony Board data to the same `.lsbackup` files as the downloadable flasher, shows WAIT, writes the firmware through the SAM3X's ROM bootloader, checks every byte, and restores the backup. Recovery modes flash with a saved backup or without one (FWUP mode). Tested on a LinnStrument 200 in Chrome on macOS. The downloadable flasher stays as the fallback.

- Harmony zone: key (with REL, PAR and steps around the circle of fifths), mode across five families, per-degree alterations, chord focus, and 8 memories that footswitches assigned to PREV and NEXT step through.
- Scale-step layout with scale-aware slides; chromatic and chromatic-with-snap layouts; rows a 3rd, 4th, 5th or octave apart.
- Held notes that morph, hold or retrigger when the harmony changes.
- Perform mode: a left-hand chord split on MIDI channel 16 with root position, inversions, 7th and Smart voicing, a thumb strip (V/x, Borrow, sus4, add9, Latch), and chord patterns (Pulse, Strum, Arpeggio) that follow the DAW's MIDI clock or the internal tempo.
- Lights (Guide, Chord, Degrees, Notes), hints, eight styles and 21 progression templates; teal, azure and violet blends and two dim levels.
- Split is the Harmony button: tap for Compose or Perform, hold for the Harmony page. Every other control button keeps its factory job.
- The harmony zone can sit on either edge and be 4, 5 or 6 columns wide.
- The harmony, memories and Harmony page settings are saved across power-off.
- A reset pad on the Harmony page (column 1, bottom: tap twice).
- The Harmony Board version on the OS Version screen (`H0.1`) and in the companion app.
- Companion app: mirrors the LinnStrument over USB MIDI, settings screens included, at https://rymorg.github.io/linnstrument-harmony-board/.
- Flasher: backs up settings, calibration, sequencer projects and Harmony Board data, flashes, and restores them; with no arguments it downloads the latest release, and `--no-backup` recovers a LinnStrument that can't answer (for example in FWUP mode). Standalone apps for macOS (Apple Silicon), Windows and Linux.

[0.1.0]: https://github.com/rymorg/linnstrument-harmony-board/releases/tag/v0.1.0
