Changelog
=========

All notable changes to the Harmony Board. Versions before 1.0 are experimental: anything can change between them, and changes that affect saved data, backups or the companion protocol are listed under **Breaking**.

## [0.1.0] - 2026-10-24

The first public pre-release.

### Added

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
