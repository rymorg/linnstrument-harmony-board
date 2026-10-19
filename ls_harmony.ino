/******************************* ls_harmony: LinnStrument Harmony Board ****************************
Copyright 2026 Ryan Morgan

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
***************************************************************************************************
Harmony mode turns columns 1-6 into a harmony zone and plays the remaining columns in scale steps:

  col 1  Key: C# D# REL F# G# A# PAR <5th   (rows 0-7, bottom to top)
  col 2  Key: C D E F G A B 5th>
  col 3  Mode: the 7 modes of the current family, darkest at the bottom, top pad cycles the family
  col 4  Alter: RESET, flip degrees 2-7, KEEP (alterations survive mode changes)
  col 5  Chord: I-VII lights that chord on the playing surface, top pad cycles triad / 7th / 9th
  col 6  Memory: 8 harmony snapshots, tap empty to store, tap to recall, hold to clear

Every column of the playing surface is the next note of the current scale, and rows are three scale
steps apart. X slides interpolate through the row's notes so they land on scale tones, and notes that
are held while the harmony changes glide to their new degree with per-note pitch bend (MPE only).

Perform mode turns columns 1-5 into left-hand chords: rows 1-7 are degrees I-VII, the columns are root
position, 1st inversion, 2nd inversion, 7th chord and Smart (the voicing nearest the last chord), and the
bottom row is a thumb strip with V/x, Borrow, sus4, add9 and Latch. Chords and their bass note go out on
MIDI channel 16, with the chord pad's Y on CC74 and its pressure as channel pressure.

Every control button keeps its factory job except Split, which becomes the Harmony button: a tap switches
Compose and Perform, a hold opens the Harmony page (harmony on/off, Compose/Perform, rows apart and
held-note behavior). Foot switches keep their factory assignments; any switch assigned to the sequencer's
PREV or NEXT steps through the memories while harmony mode is active, which keeps the stored settings
valid for the stock firmware.
**************************************************************************************************/

#define HARM_COL_KEYACC     1
#define HARM_COL_KEYNAT     2
#define HARM_COL_MODE       3
#define HARM_COL_ALTER      4
#define HARM_COL_CHORD      5
#define HARM_COL_MEMORY     6
#define HARM_FIRST_PLAY     7
#define HARM_NOTE_NONE      255
#define HARM_FAMILIES       5
#define HARM_SYMMETRIC      4                // the family index of the pentatonic and symmetric scales
#define HARM_MEMORIES       8
#define HARM_COL_UNITS16    2730             // one column of X movement is 170.625 pitch units, times 16
#define HARM_PAGE_ONOFF     7                // Harmony page columns, matching the mockup's page
#define HARM_PAGE_MODE      9
#define HARM_PAGE_ROWS      21
#define HARM_PAGE_HELD      23
#define HARM_SPLIT_HOLD_MS  500              // hold Split this long to open the Harmony page
#define HARM_RETUNE_MORPH   0                // held notes when the harmony changes: glide, keep their pitch,
#define HARM_RETUNE_HOLD    1                // or retrigger in the new harmony
#define HARM_RETUNE_RETRIG  2
#define HARM_MEM_CLEAR_MS   900              // hold a memory pad this long to clear it

const byte HARM_MAJOR[7] = {0, 2, 4, 5, 7, 9, 11};
const byte HARM_PARENT[4][7] = {
  {0, 2, 4, 5, 7, 9, 11},                    // major, its modes from Ionian to Locrian
  {0, 2, 3, 5, 7, 9, 11},                    // melodic minor
  {0, 2, 3, 5, 7, 8, 11},                    // harmonic minor
  {0, 2, 4, 5, 7, 8, 11}                     // harmonic major
};
const byte HARM_SYM_LEN[7] = {5, 5, 6, 6, 8, 8, 12};
const byte HARM_SYM[7][12] = {
  {0, 2, 4, 7, 9},                           // major pentatonic
  {0, 3, 5, 7, 10},                          // minor pentatonic
  {0, 3, 5, 6, 7, 10},                       // blues
  {0, 2, 4, 6, 8, 10},                       // whole tone
  {0, 2, 3, 5, 6, 8, 9, 11},                 // diminished, whole-half
  {0, 1, 3, 4, 6, 7, 9, 10},                 // diminished, half-whole
  {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}     // chromatic
};
const byte HARM_FAMILY_COLOR[HARM_FAMILIES] = {COLOR_LIME, COLOR_PINK, COLOR_MAGENTA, COLOR_RED, COLOR_YELLOW};
const byte HARM_ROW_STEPS[4] = {2, 3, 4, 7};     // rows apart in scale steps: a 3rd, 4th, 5th or octave in a 7-note scale
const byte HARM_ROW_SEMIS[4] = {4, 5, 7, 12};    // the same distances in semitones, for scales of other sizes
const byte HARM_NATURAL_PC[7] = {0, 2, 4, 5, 7, 9, 11};
const signed char HARM_ACCIDENTAL_PC[8] = {1, 3, -1, 6, 8, 10, -1, -1};

struct HarmonyState {
  byte root;                                 // pitch class of the key, 0-11
  byte family;                               // 0-4, see HARM_PARENT and HARM_SYM
  byte mode;                                 // 0-6, the rotation of the parent scale or the symmetric scale index
  signed char alter[7];                      // semitone change per degree, only for 7-note scales
  signed char chordDeg;                      // the lit chord's degree, -1 lights the tonic chord
  byte chordSize;                            // 3, 4 or 5 notes
};

boolean harmonyEnabled = true;
boolean harmonyReady = false;                // set once the tables are built in performance mode
boolean harmonyKeepAlter = false;
byte harmonyRowIdx = 1;                      // index into HARM_ROW_STEPS, rows a 4th apart by default
byte harmonyRetune = HARM_RETUNE_MORPH;
unsigned long harmonySplitDownAt = 0;
boolean harmonySplitHeld = false;

// Perform mode: the left-hand chord split
#define HARM_CHORD_CHANNEL  16               // chords and their bass note go out on their own MIDI channel
#define HARM_CHORD_SPLIT    RIGHT            // note tracking slot for chord notes, apart from the melody's
#define HARM_CHORD_ROOT     0                // chord columns 1-5: root position, 1st inversion, 2nd inversion,
#define HARM_CHORD_INV1     1                // 7th chord and Smart voicing
#define HARM_CHORD_INV2     2
#define HARM_CHORD_SEVENTH  3
#define HARM_CHORD_SMART    4
#define HARM_THUMB_LATCH    4                // thumb strip under the chord columns: V/x, Borrow, sus4, add9, Latch
#define HARM_MOD_VX         1
#define HARM_MOD_BRW        2
#define HARM_MOD_SUS        4
#define HARM_MOD_ADD9       8
#define HARM_MOD_TAP_MS     350              // a thumb pad let go sooner than this stays armed for the next chord
#define HARM_MAX_CHORD      6

boolean harmonyPerform = false;
boolean harmonyLatch = true;
byte harmonyModArmed = 0;                    // modifiers tapped for the next chord
byte harmonyModHeld = 0;                     // modifiers held down right now
byte harmonyModUsed = 0;                     // held modifiers that a chord has already used
unsigned long harmonyModDownAt[4];

signed char harmonyChordType = -1;           // the sounding chord's column type and degree, -1 when silent
signed char harmonyChordDeg = -1;
byte harmonyChordMods = 0;
boolean harmonyChordLatched = false;
signed char harmonyChordPadCol = -1;         // the chord pad that's down right now, if any
signed char harmonyChordPadRow = -1;
byte harmonyChordVelocity = 96;
byte harmonyChordNotes[HARM_MAX_CHORD];      // the voiced chord, lowest first, without the bass
byte harmonyChordCount = 0;
byte harmonyChordBass = 0;
byte harmonyChordRootPc = 0;
signed char harmonyChordRel[HARM_MAX_CHORD]; // chord tones above the root, for the melody lights
byte harmonyChordSent[HARM_MAX_CHORD + 1];   // every note that went out, bass included
byte harmonyChordSentCount = 0;
byte harmonyChordPrev[HARM_MAX_CHORD];       // the last voicing, for Smart's voice leading
byte harmonyChordPrevCount = 0;
byte harmonyChordLastY = 255;
byte harmonyChordLastZ = 255;

// chord patterns: Sustain holds the chord; Pulse, Strum and Arp play it on a 16th-note grid
// from the shared 24PPQ clock (Global Settings tempo, or incoming MIDI clock)
#define HARM_PAT_SUSTAIN    0
#define HARM_PAT_PULSE      1
#define HARM_PAT_STRUM      2
#define HARM_PAT_ARP        3
#define HARM_PAT_COUNT      4
#define HARM_PAGE_PATTERN   25
#define HARM_PAT_VOICES     16
#define HARM_PAT_FREE       0
#define HARM_PAT_WAITING    1                // a strum note waiting for its moment
#define HARM_PAT_SOUNDING   2
#define HARM_STRUM_GAP_US   24000            // time between the notes of a strum
#define HARM_PAT_STALL_US   500000           // end sounding notes when the clock stops for this long
#define HARM_CLOCK_HOST     0                // follow the DAW's MIDI clock while it plays, the tempo setting otherwise
#define HARM_CLOCK_INTERNAL 1                // always the tempo setting, ignoring incoming MIDI clock

byte harmonyPattern = HARM_PAT_SUSTAIN;
boolean harmonyPatActive = false;            // the sounding chord is playing a pattern
unsigned long harmonyPatTicks = 0;           // 24PPQ ticks counted since power-up
unsigned long harmonyPatStartTick = 0;
unsigned long harmonyPatLastTickUs = 0;
byte harmonyPatClockSource = HARM_CLOCK_HOST;
byte harmonyPatClock = 0;                    // the 24PPQ position the patterns follow, from 0 to 23
int32_t harmonyPatOwnTempo = FXD4_FROM_INT(120);  // the tempo setting, kept while MIDI clock overrides it
unsigned long harmonyPatOwnNextUs = 0;       // the next tick of the patterns' own clock
unsigned short harmonyPatBeat = 0;           // beats since the pattern started
byte harmonyPatArpIdx = 0;
byte harmonyPatWaiting = 0;                  // how many strum notes are waiting
byte harmonyPatVoiceState[HARM_PAT_VOICES];
byte harmonyPatVoiceNote[HARM_PAT_VOICES];
byte harmonyPatVoiceVel[HARM_PAT_VOICES];
unsigned long harmonyPatVoiceOff[HARM_PAT_VOICES];  // the tick at which the note ends
unsigned long harmonyPatVoiceDue[HARM_PAT_VOICES];  // when a waiting note starts, in micros
HarmonyState harmonyCur = {2, 0, 1, {0, 0, 0, 0, 0, 0, 0}, -1, 4};
HarmonyState harmonyMem[HARM_MEMORIES];
boolean harmonyMemUsed[HARM_MEMORIES];
byte harmonyModeOrder[HARM_FAMILIES][7];     // per family, mode indices from the brightest to the darkest

byte harmonyBaseIv[12];                      // intervals of the selected mode before alterations
byte harmonyIv[12];                          // intervals in use
byte harmonyLen = 7;
signed char harmonyDegreeOfPc[12];           // scale degree of each pitch class, -1 when it's not in the scale
signed char harmonyLitRole[12];              // 0 for the lit chord's root, 1+ for its other tones, -1 otherwise
byte harmonyNotes[MAXCOLS][MAXROWS];         // MIDI note of each playing cell before octave transposition
int harmonyBendOffset[17];                   // per MIDI channel, the pitch offset of a held note after a harmony change

signed char harmonyMemPressRow = -1;
unsigned long harmonyMemPressStart = 0;
boolean harmonyMemPressDone = true;

/******************************* state and tables *******************************/

boolean harmonyActive() {
  return harmonyEnabled && harmonyReady && !userFirmwareActive;
}

boolean harmonyIsZoneCol(byte col) {
  return col >= 1 && col < HARM_FIRST_PLAY;
}

// whether the current sensor cell is a harmony zone pad that should be handled by this module
boolean harmonyHandlesSensorCell() {
  return harmonyActive() && displayMode == displayNormal && harmonyIsZoneCol(sensorCol);
}

byte harmonyModeIntervals(byte family, byte mode, byte* out) {
  if (family == HARM_SYMMETRIC) {
    byte len = HARM_SYM_LEN[mode];
    for (byte i = 0; i < len; ++i) {
      out[i] = HARM_SYM[mode][i];
    }
    return len;
  }

  const byte* parent = HARM_PARENT[family];
  for (byte i = 0; i < 7; ++i) {
    out[i] = (parent[(i + mode) % 7] + 12 - parent[mode]) % 12;
  }
  return 7;
}

void initializeHarmony() {
  // order the modes of each family by brightness, with the largest interval sum first
  for (byte f = 0; f < HARM_FAMILIES; ++f) {
    short sums[7];
    for (byte m = 0; m < 7; ++m) {
      harmonyModeOrder[f][m] = m;
      byte iv[12];
      byte len = harmonyModeIntervals(f, m, iv);
      sums[m] = 0;
      for (byte i = 0; i < len; ++i) {
        sums[m] += iv[i];
      }
    }
    if (f == HARM_SYMMETRIC) continue;

    for (byte i = 1; i < 7; ++i) {
      byte m = harmonyModeOrder[f][i];
      signed char j = i - 1;
      while (j >= 0 && sums[harmonyModeOrder[f][j]] < sums[m]) {
        harmonyModeOrder[f][j + 1] = harmonyModeOrder[f][j];
        j--;
      }
      harmonyModeOrder[f][j + 1] = m;
    }
  }

  // example memories, the same ones as the mockup: D Dorian, G Mixolydian, C Lydian, A minor with a raised 7th
  for (byte i = 0; i < HARM_MEMORIES; ++i) {
    harmonyMemUsed[i] = false;
  }
  harmonyMemUsed[0] = true; harmonyMem[0] = harmonyCur;
  harmonyMemUsed[1] = true; harmonyMem[1] = harmonyCur; harmonyMem[1].root = 7; harmonyMem[1].mode = 4; harmonyMem[1].chordDeg = 0;
  harmonyMemUsed[2] = true; harmonyMem[2] = harmonyCur; harmonyMem[2].root = 0; harmonyMem[2].mode = 3; harmonyMem[2].chordDeg = 0;
  harmonyMemUsed[3] = true; harmonyMem[3] = harmonyCur; harmonyMem[3].root = 9; harmonyMem[3].mode = 5; harmonyMem[3].chordDeg = 4; harmonyMem[3].alter[6] = 1;

  for (byte ch = 0; ch <= 16; ++ch) {
    harmonyBendOffset[ch] = 0;
  }

  if (harmonyEnabled) {
    Global.splitActive = false;
  }
  harmonyRecompute();
  harmonyReady = true;
}

void harmonyRecompute() {
  harmonyLen = harmonyModeIntervals(harmonyCur.family, harmonyCur.mode, harmonyBaseIv);
  boolean hepta = (harmonyLen == 7);
  for (byte i = 0; i < harmonyLen; ++i) {
    harmonyIv[i] = hepta ? harmonyBaseIv[i] + harmonyCur.alter[i] : harmonyBaseIv[i];
  }

  for (byte pc = 0; pc < 12; ++pc) {
    harmonyDegreeOfPc[pc] = -1;
    harmonyLitRole[pc] = -1;
  }
  for (byte i = 0; i < harmonyLen; ++i) {
    harmonyDegreeOfPc[(harmonyCur.root + harmonyIv[i]) % 12] = i;
  }

  // the lit chord: the focused chord, or the tonic chord
  if (hepta) {
    byte deg = harmonyCur.chordDeg >= 0 ? harmonyCur.chordDeg : 0;
    for (byte j = 0; j < harmonyCur.chordSize; ++j) {
      byte pc = (harmonyCur.root + harmonyIv[(deg + 2 * j) % 7]) % 12;
      if (harmonyLitRole[pc] < 0) {
        harmonyLitRole[pc] = j;
      }
    }
  }
  else {
    for (byte i = 0; i < harmonyLen; ++i) {
      byte pc = (harmonyCur.root + harmonyIv[i]) % 12;
      byte x = harmonyIv[i];
      if (x == 0) harmonyLitRole[pc] = 0;
      else if (x == 3 || x == 4) harmonyLitRole[pc] = 1;
      else if (x == 7) harmonyLitRole[pc] = 2;
    }
  }

  // the note of every playing cell
  short base = 36 + harmonyCur.root - (harmonyCur.root >= 7 ? 12 : 0);
  short steps = hepta ? HARM_ROW_STEPS[harmonyRowIdx] : (harmonyLen * HARM_ROW_SEMIS[harmonyRowIdx] + 6) / 12;
  if (steps < 1) steps = 1;
  for (byte col = 0; col < MAXCOLS; ++col) {
    for (byte row = 0; row < MAXROWS; ++row) {
      harmonyNotes[col][row] = HARM_NOTE_NONE;
    }
  }
  for (byte col = HARM_FIRST_PLAY; col < NUMCOLS; ++col) {
    for (byte row = 0; row < NUMROWS; ++row) {
      short s = (col - HARM_FIRST_PLAY) + row * steps;
      short note = base + 12 * (s / harmonyLen) + harmonyIv[s % harmonyLen];
      harmonyNotes[col][row] = (note >= 0 && note <= 127) ? note : HARM_NOTE_NONE;
    }
  }
}

byte harmonyNoteNumber(byte col, byte row) {
  if (col >= MAXCOLS || row >= MAXROWS) return HARM_NOTE_NONE;
  return harmonyNotes[col][row];
}

// recompute the tables, glide the held notes to the new harmony and repaint
void harmonyChanged() {
  harmonyRecompute();
  harmonyMorphHeldNotes();
  harmonyChordRetune();
  updateDisplay();
}

void harmonySetEnabled(boolean on) {
  if (harmonyEnabled == on) return;
  harmonyChordStop();
  harmonyPerform = false;
  resetAllTouches();
  harmonyEnabled = on;
  if (harmonyEnabled) {
    Global.splitActive = false;
    harmonyRecompute();
  }
  for (byte ch = 0; ch <= 16; ++ch) {
    harmonyBendOffset[ch] = 0;
  }
}

/******************************* pitch *******************************/

// map an X movement, measured from the cell where the touch started, onto the notes of its row,
// so that the pitch interpolates between neighboring scale tones and lands on them at cell centers
short harmonyMapX(short initialCol, byte row, short movedX) {
  if (!harmonyActive() || initialCol < HARM_FIRST_PLAY || initialCol >= NUMCOLS || row >= NUMROWS) return movedX;

  byte note0 = harmonyNotes[initialCol][row];
  if (note0 == HARM_NOTE_NONE) return movedX;

  int32_t q = (int32_t)movedX * 16;
  int32_t whole = q >= 0 ? q / HARM_COL_UNITS16 : -((-q + HARM_COL_UNITS16 - 1) / HARM_COL_UNITS16);
  int32_t frac = q - whole * HARM_COL_UNITS16;
  int32_t colA = initialCol + whole;
  if (colA < HARM_FIRST_PLAY) {
    colA = HARM_FIRST_PLAY;
    frac = 0;
  }
  if (colA >= NUMCOLS - 1) {
    colA = NUMCOLS - 1;
    frac = 0;
  }

  byte noteA = harmonyNotes[colA][row];
  byte noteB = frac ? harmonyNotes[colA + 1][row] : noteA;
  if (noteA == HARM_NOTE_NONE || noteB == HARM_NOTE_NONE) return movedX;

  return (short)((((int32_t)noteA - note0) * HARM_COL_UNITS16 + frac * ((int32_t)noteB - noteA)) / 16);
}

int harmonyPitchOffset(byte channel) {
  if (!harmonyActive() || channel < 1 || channel > 16) return 0;
  return harmonyBendOffset[channel];
}

void harmonyNoteStarted(byte channel) {
  if (channel >= 1 && channel <= 16) {
    harmonyBendOffset[channel] = 0;
  }
}

// held notes keep their degree when the harmony changes: Morph bends each note's channel to its new pitch,
// Hold keeps the pitch it had, and Retrigger ends the note and starts the new one in its place
void harmonyMorphHeldNotes() {
  for (byte col = HARM_FIRST_PLAY; col < NUMCOLS; ++col) {
    for (byte row = 0; row < NUMROWS; ++row) {
      TouchInfo& c = cell(col, row);
      if (c.touched != touchedCell || !c.hasNote() || c.channel < 1) continue;

      byte split = getSplitOf(col);
      short startCol = c.initialColumn >= HARM_FIRST_PLAY ? c.initialColumn : col;
      byte note = harmonyNotes[startCol][row];
      if (note == HARM_NOTE_NONE) continue;

      short target = note + c.octaveOffset + Split[split].transposePitch;
      int mapped = harmonyMapX(startCol, row, c.lastMovedX);

      if (harmonyRetune == HARM_RETUNE_RETRIG) {
        if (target == c.note || target < 0 || target > 127) continue;
        midiSendNoteOff(split, c.note, c.channel);
        noteTouchMapping[split].noteOff(c.note, c.channel);
        c.note = target;
        noteTouchMapping[split].noteOn(target, c.channel, col, row);
        harmonyBendOffset[c.channel] = 0;
        if (Split[split].sendX) {
          preSendPitchBend(split, mapped, c.channel);
        }
        midiSendNoteOn(split, target, c.velocity, c.channel);
        continue;
      }

      // Morph and Hold work through per-note pitch bend
      if (Split[split].midiMode != channelPerNote || !Split[split].sendX) continue;

      if (harmonyRetune == HARM_RETUNE_MORPH) {
        harmonyBendOffset[c.channel] = ((int)(target - c.note) * HARM_COL_UNITS16) / 16;
        preSendPitchBend(split, mapped + harmonyBendOffset[c.channel], c.channel);
      }
      else if (c.lastValueX != INVALID_DATA) {
        // hold: keep the pitch the note had before the change, later slides follow the new row
        harmonyBendOffset[c.channel] += c.lastValueX - mapped;
      }
    }
  }
}

/******************************* zone touches *******************************/

// a touch that slides off the playing surface onto the memory column shouldn't trigger it
boolean harmonyIgnoreZoneTouch() {
  return sensorCol == HARM_COL_MEMORY && cell(HARM_FIRST_PLAY, sensorRow).touched != untouchedCell;
}

void harmonyZoneTouch(byte col, byte row) {
  if (harmonyPerform && col < HARM_COL_MEMORY) {
    harmonyPerformTouch(col, row);
    return;
  }

  switch (col) {
    case HARM_COL_KEYACC: harmonyKeyAccidentalTouch(row); break;
    case HARM_COL_KEYNAT: harmonyKeyNaturalTouch(row); break;
    case HARM_COL_MODE:   harmonyModeTouch(row); break;
    case HARM_COL_ALTER:  harmonyAlterTouch(row); break;
    case HARM_COL_CHORD:  harmonyChordTouch(row); break;
    case HARM_COL_MEMORY: harmonyMemoryTouch(row); break;
  }
}

// called on every scan while a zone pad stays down
void harmonyZoneHold(byte col, byte row) {
  if (harmonyPerform && col < HARM_COL_MEMORY) {
    harmonyPerformHold(col, row);
    return;
  }

  if (col == HARM_COL_MEMORY && row == harmonyMemPressRow && !harmonyMemPressDone &&
      calcTimeDelta(millis(), harmonyMemPressStart) > HARM_MEM_CLEAR_MS) {
    harmonyMemPressDone = true;
    harmonyMemUsed[row] = false;
    updateDisplay();
  }
}

// called when a zone pad is let go
void harmonyZoneRelease(byte col, byte row) {
  if (harmonyPerform && col < HARM_COL_MEMORY) {
    harmonyPerformRelease(col, row);
  }
}

void harmonyClearAlter() {
  for (byte i = 0; i < 7; ++i) {
    harmonyCur.alter[i] = 0;
  }
}

boolean harmonyHasInterval(byte semitones) {
  for (byte i = 0; i < harmonyLen; ++i) {
    if (harmonyIv[i] == semitones) return true;
  }
  return false;
}

void harmonySetRoot(byte pc) {
  harmonyCur.root = pc % 12;
  harmonyChanged();
}

void harmonyKeyNaturalTouch(byte row) {
  if (row < 7) {
    harmonySetRoot(HARM_NATURAL_PC[row]);
  }
  else {
    harmonySetRoot(harmonyCur.root + 7);       // up a fifth
  }
}

void harmonyKeyAccidentalTouch(byte row) {
  if (HARM_ACCIDENTAL_PC[row] >= 0) {
    harmonySetRoot(HARM_ACCIDENTAL_PC[row]);
  }
  else if (row == 2) {
    harmonyRelative();
  }
  else if (row == 6) {
    harmonyParallel();
  }
  else {
    harmonySetRoot(harmonyCur.root + 5);       // down a fifth
  }
}

// keep the notes, move the root to the relative major or minor
void harmonyRelative() {
  if (harmonyCur.family == HARM_SYMMETRIC) {
    if (harmonyCur.mode == 0) {
      harmonyCur.root = (harmonyCur.root + 9) % 12;
      harmonyCur.mode = 1;
    }
    else if (harmonyCur.mode == 1) {
      harmonyCur.root = (harmonyCur.root + 3) % 12;
      harmonyCur.mode = 0;
    }
    else {
      return;
    }
  }
  else {
    byte base[12];
    harmonyModeIntervals(harmonyCur.family, harmonyCur.mode, base);
    byte target = (base[2] == 4) ? 5 : 2;
    harmonyCur.root = (harmonyCur.root + base[target]) % 12;
    harmonyCur.mode = (harmonyCur.mode + target) % 7;
    harmonyClearAlter();
  }
  harmonyChanged();
}

// keep the root, switch between major (Ionian) and minor (Aeolian)
void harmonyParallel() {
  if (harmonyCur.family == HARM_SYMMETRIC && harmonyCur.mode <= 1) {
    harmonyCur.mode = 1 - harmonyCur.mode;
  }
  else {
    boolean major = (harmonyHasInterval(4) && !harmonyHasInterval(3)) || (harmonyLen == 7 && harmonyIv[2] == 4);
    harmonyCur.family = 0;
    harmonyCur.mode = major ? 5 : 0;
    harmonyClearAlter();
  }
  harmonyChanged();
}

void harmonyModeTouch(byte row) {
  if (row == 7) {
    // next family, keeping the same brightness position
    byte rank = 0;
    for (byte i = 0; i < 7; ++i) {
      if (harmonyModeOrder[harmonyCur.family][i] == harmonyCur.mode) rank = i;
    }
    harmonyCur.family = (harmonyCur.family + 1) % HARM_FAMILIES;
    harmonyCur.mode = harmonyModeOrder[harmonyCur.family][rank];
  }
  else {
    harmonyCur.mode = harmonyModeOrder[harmonyCur.family][6 - row];
  }

  if (!harmonyKeepAlter) {
    harmonyClearAlter();
  }
  harmonyChanged();
}

void harmonyAlterTouch(byte row) {
  if (harmonyLen != 7) return;

  if (row == 0) {
    harmonyClearAlter();
    harmonyChanged();
  }
  else if (row == 7) {
    harmonyKeepAlter = !harmonyKeepAlter;
    updateDisplay();
  }
  else {
    // flip the degree between its major-scale form and its common alteration: b2 b3 #4 b5 b6 b7
    byte deg = row;
    byte natural = HARM_MAJOR[deg];
    byte flipped = (deg == 3) ? 6 : natural - 1;
    byte target = (harmonyIv[deg] == natural) ? flipped : natural;
    byte prev = harmonyIv[deg - 1];
    byte next = deg < 6 ? harmonyIv[deg + 1] : 12;
    if (target <= prev || target >= next) return;      // it would land on a neighboring degree's pitch

    harmonyCur.alter[deg] = target - harmonyBaseIv[deg];
    harmonyChanged();
  }
}

void harmonyChordTouch(byte row) {
  if (harmonyLen != 7) return;

  if (row == 7) {
    harmonyCur.chordSize = harmonyCur.chordSize >= 5 ? 3 : harmonyCur.chordSize + 1;
  }
  else {
    harmonyCur.chordDeg = (harmonyCur.chordDeg == row) ? -1 : row;
  }
  harmonyChanged();
}

/******************************* memories *******************************/

boolean harmonyMemIsCurrent(byte slot) {
  if (!harmonyMemUsed[slot]) return false;
  HarmonyState& m = harmonyMem[slot];
  if (m.root != harmonyCur.root || m.family != harmonyCur.family || m.mode != harmonyCur.mode ||
      m.chordDeg != harmonyCur.chordDeg || m.chordSize != harmonyCur.chordSize) return false;
  for (byte i = 0; i < 7; ++i) {
    if (m.alter[i] != harmonyCur.alter[i]) return false;
  }
  return true;
}

void harmonyRecall(byte slot) {
  if (slot >= HARM_MEMORIES || !harmonyMemUsed[slot]) return;
  harmonyCur = harmonyMem[slot];
  harmonyChanged();
}

void harmonyMemoryTouch(byte row) {
  harmonyMemPressRow = row;
  harmonyMemPressStart = millis();

  if (harmonyMemUsed[row]) {
    harmonyMemPressDone = false;               // holding it clears it
    harmonyRecall(row);
  }
  else {
    harmonyMemPressDone = true;
    harmonyMem[row] = harmonyCur;
    harmonyMemUsed[row] = true;
    updateDisplay();
  }
}

// step to the next or previous stored memory, wrapping around
void harmonyMemoryStep(signed char dir) {
  signed char current = -1;
  for (byte i = 0; i < HARM_MEMORIES; ++i) {
    if (harmonyMemIsCurrent(i)) {
      current = i;
      break;
    }
  }

  for (byte n = 1; n <= HARM_MEMORIES; ++n) {
    signed char slot;
    if (current < 0) {
      slot = dir > 0 ? n - 1 : HARM_MEMORIES - n;
    }
    else {
      slot = (current + dir * n + HARM_MEMORIES * 2) % HARM_MEMORIES;
    }
    if (harmonyMemUsed[slot]) {
      harmonyRecall(slot);
      return;
    }
  }
}

// while harmony mode is active, a switch assigned to the sequencer's PREV or NEXT steps through the memories;
// every other assignment, including the factory Arpeggiator and Sustain, works as usual
boolean harmonyHandlesSwitch(byte whichSwitch, boolean pressed) {
  if (!harmonyActive()) return false;

  byte assignment = Global.switchAssignment[whichSwitch];
  if (assignment != ASSIGNED_SEQUENCER_PREV && assignment != ASSIGNED_SEQUENCER_NEXT) return false;

  if (pressed) {
    harmonyMemoryStep(assignment == ASSIGNED_SEQUENCER_NEXT ? 1 : -1);
  }
  return true;
}

/******************************* Harmony page *******************************/

// the Split button opens and closes the Harmony page, which takes over the playing surface like the
// factory settings screens; the columns match the mockup's page, and the ones this build doesn't have stay dark
void harmonyToggleSettingsPage() {
  if (displayMode == displayHarmony) {
    setDisplayMode(displayNormal);
  }
  else {
    resetAllTouches();
    setDisplayMode(displayHarmony);
  }
  updateDisplay();
}

void paintHarmonyPage() {
  clearDisplay();

  // harmony on, off
  setLed(HARM_PAGE_ONOFF, 7, harmonyEnabled ? COLOR_WHITE : COLOR_CYAN, cellOn);
  setLed(HARM_PAGE_ONOFF, 6, harmonyEnabled ? COLOR_CYAN : COLOR_WHITE, cellOn);

  // rows apart: a 3rd, 4th, 5th or octave
  for (byte i = 0; i < 4; ++i) {
    setLed(HARM_PAGE_ROWS, 7 - i, harmonyRowIdx == i ? COLOR_WHITE : COLOR_YELLOW, cellOn);
  }

  // mode: compose, perform
  setLed(HARM_PAGE_MODE, 7, harmonyPerform ? COLOR_MAGENTA : COLOR_WHITE, cellOn);
  setLed(HARM_PAGE_MODE, 6, harmonyPerform ? COLOR_WHITE : COLOR_MAGENTA, cellOn);

  // held notes: morph, hold, retrigger
  for (byte i = 0; i < 3; ++i) {
    setLed(HARM_PAGE_HELD, 7 - i, harmonyRetune == i ? COLOR_WHITE : COLOR_PINK, cellOn);
  }

  // chord pattern: sustain, pulse, strum, arpeggio
  const byte patColors[HARM_PAT_COUNT] = {COLOR_GREEN, COLOR_YELLOW, COLOR_ORANGE, COLOR_PINK};
  for (byte i = 0; i < HARM_PAT_COUNT; ++i) {
    setLed(HARM_PAGE_PATTERN, 7 - i, harmonyPattern == i ? COLOR_WHITE : patColors[i], cellOn);
  }

  // pattern clock: host (DAW's MIDI clock while it plays) or internal
  setLed(HARM_PAGE_PATTERN, 1, harmonyPatClockSource == HARM_CLOCK_HOST ? COLOR_WHITE : COLOR_BLUE, cellOn);
  setLed(HARM_PAGE_PATTERN, 0, harmonyPatClockSource == HARM_CLOCK_INTERNAL ? COLOR_WHITE : COLOR_CYAN, cellOn);
}

void harmonyPageTouch(byte col, byte row) {
  if (col == HARM_PAGE_ONOFF && (row == 7 || row == 6)) {
    harmonySetEnabled(row == 7);
  }
  else if (col == HARM_PAGE_ROWS && row >= 4) {
    harmonyRowIdx = 7 - row;
    harmonyRecompute();
  }
  else if (col == HARM_PAGE_MODE && (row == 7 || row == 6)) {
    harmonySetPerform(row == 6);
  }
  else if (col == HARM_PAGE_HELD && row >= 5) {
    harmonyRetune = 7 - row;
  }
  else if (col == HARM_PAGE_PATTERN && row >= 8 - HARM_PAT_COUNT) {
    harmonySetPattern(7 - row);
  }
  else if (col == HARM_PAGE_PATTERN && row <= 1) {
    harmonyPatClockSource = (row == 1) ? HARM_CLOCK_HOST : HARM_CLOCK_INTERNAL;
  }
  else {
    return;
  }
  updateDisplay();
}

// Split is the Harmony button: a tap switches Compose and Perform (or turns harmony mode back on, or closes
// the Harmony page), a hold opens the Harmony page
void harmonySplitDown() {
  harmonySplitDownAt = millis();
  harmonySplitHeld = false;
}

// called on every scan while Split stays down
void harmonySplitHoldCheck() {
  if (harmonySplitHeld || calcTimeDelta(millis(), harmonySplitDownAt) < HARM_SPLIT_HOLD_MS) return;
  harmonySplitHeld = true;
  if (displayMode != displayHarmony) {
    harmonyToggleSettingsPage();
  }
  updateSwitchLeds();
}

void harmonySplitUp() {
  if (harmonySplitHeld) return;               // the hold already opened the page

  if (displayMode == displayHarmony) {
    harmonyToggleSettingsPage();
  }
  else if (displayMode == displayNormal) {
    if (!harmonyEnabled) {
      harmonySetEnabled(true);
    }
    else {
      harmonySetPerform(!harmonyPerform);
    }
    updateDisplay();
  }
  updateSwitchLeds();
}

boolean harmonyIsPerform() {
  return harmonyActive() && harmonyPerform;
}

/******************************* lights *******************************/

byte harmonyPitchClassColor(byte pc) {
  if (pc == harmonyCur.root) return COLOR_CYAN;
  return harmonyDegreeOfPc[pc] >= 0 ? COLOR_GREEN : COLOR_OFF;
}

byte harmonyZoneColor(byte col, byte row) {
  if (harmonyPerform && col < HARM_COL_MEMORY) return harmonyPerformZoneColor(col, row);

  boolean hepta = (harmonyLen == 7);
  switch (col) {
    case HARM_COL_KEYACC:
      if (HARM_ACCIDENTAL_PC[row] < 0) return COLOR_YELLOW;
      return harmonyPitchClassColor(HARM_ACCIDENTAL_PC[row]);

    case HARM_COL_KEYNAT:
      if (row == 7) return COLOR_YELLOW;
      return harmonyPitchClassColor(HARM_NATURAL_PC[row]);

    case HARM_COL_MODE:
      if (row == 7) return HARM_FAMILY_COLOR[harmonyCur.family];
      return harmonyModeOrder[harmonyCur.family][6 - row] == harmonyCur.mode ? COLOR_WHITE : HARM_FAMILY_COLOR[harmonyCur.family];

    case HARM_COL_ALTER:
      if (!hepta) return COLOR_OFF;
      if (row == 0) {
        for (byte i = 0; i < 7; ++i) {
          if (harmonyCur.alter[i]) return COLOR_RED;
        }
        return COLOR_OFF;
      }
      if (row == 7) return harmonyKeepAlter ? COLOR_YELLOW : COLOR_OFF;
      return harmonyCur.alter[row] ? COLOR_ORANGE : COLOR_BLUE;

    case HARM_COL_CHORD:
      if (!hepta) return COLOR_OFF;
      if (row == 7) return harmonyCur.chordSize == 3 ? COLOR_LIME : (harmonyCur.chordSize == 4 ? COLOR_YELLOW : COLOR_ORANGE);
      return harmonyCur.chordDeg == row ? COLOR_WHITE : COLOR_BLUE;

    case HARM_COL_MEMORY:
      if (!harmonyMemUsed[row]) return COLOR_OFF;
      return harmonyMemIsCurrent(row) ? COLOR_WHITE : COLOR_BLUE;
  }
  return COLOR_OFF;
}

// in the scale layout every pad is a scale tone, so only the lit chord's tones and altered notes light up
byte harmonyPlayColor(byte col, byte row) {
  byte note = harmonyNotes[col][row];
  if (note == HARM_NOTE_NONE) return COLOR_OFF;

  byte split = getSplitOf(col);
  short actual = note + Split[split].transposeOctave + Split[split].transposePitch;
  if (actual < 0 || actual > 127) return COLOR_OFF;

  byte pc = note % 12;
  signed char deg = harmonyDegreeOfPc[pc];
  if (deg < 0) return COLOR_OFF;

  boolean altered = (harmonyLen == 7 && harmonyCur.alter[deg] != 0);
  signed char role = harmonyLitRole[pc];
  if (role == 0) return COLOR_CYAN;
  if (role > 0 || altered) return altered ? COLOR_ORANGE : COLOR_GREEN;
  return COLOR_OFF;
}

void paintHarmonyCell(byte col, byte row) {
  byte color = harmonyIsZoneCol(col) ? harmonyZoneColor(col, row) : harmonyPlayColor(col, row);
  if (row == 0) {
    clearLed(col, row, LED_LAYER_LOWROW);
  }
  setLed(col, row, color, color == COLOR_OFF ? cellOff : cellOn, LED_LAYER_MAIN);
}

/******************************* Perform mode: left-hand chords *******************************/

const byte HARM_IONIAN[7] = {0, 2, 4, 5, 7, 9, 11};
const byte HARM_AEOLIAN[7] = {0, 2, 3, 5, 7, 8, 10};

void harmonySetPerform(boolean on) {
  if (harmonyPerform == on) return;
  harmonyChordStop();
  harmonyModArmed = 0;
  harmonyModHeld = 0;
  resetAllTouches();
  harmonyPerform = on;
  harmonyRecompute();                        // the melody lights go back to the tonic chord
}

short harmonyScaleSemis(const byte* sc, byte len, short step) {
  return sc[step % len] + 12 * (step / len);
}

void harmonySortNotes(byte* a, byte n) {
  for (byte i = 1; i < n; ++i) {
    byte v = a[i];
    signed char j = i - 1;
    while (j >= 0 && a[j] > v) {
      a[j + 1] = a[j];
      j--;
    }
    a[j + 1] = v;
  }
}

// chord tones as semitones above the chord root for degree deg; returns how many, 0 when it's not possible
byte harmonyChordTones(byte type, byte deg, byte mods, signed char* rel, byte& rootPc) {
  byte n = harmonyLen;
  if (deg >= n || deg >= 7) return 0;

  byte size = type == HARM_CHORD_SEVENTH ? 4 : (type == HARM_CHORD_SMART ? harmonyCur.chordSize : 3);
  byte count = 0;
  short ninth = 14;

  if (mods & HARM_MOD_VX) {
    // the dominant 7th that leads into this degree's chord
    rootPc = (harmonyCur.root + harmonyIv[deg] + 7) % 12;
    rel[count++] = 0;
    rel[count++] = 4;
    rel[count++] = 7;
    if (size >= 4) rel[count++] = 10;
    if (size >= 5) rel[count++] = 14;
  }
  else {
    const byte* sc = harmonyIv;
    byte len = n;
    if (mods & HARM_MOD_BRW) {
      // the same degree's chord from the parallel major or minor
      if (n != 7) return 0;
      sc = harmonyIv[2] == 4 ? HARM_AEOLIAN : HARM_IONIAN;
      len = 7;
    }
    rootPc = (harmonyCur.root + sc[deg]) % 12;
    for (byte j = 0; j < size && count < HARM_MAX_CHORD; ++j) {
      rel[count++] = harmonyScaleSemis(sc, len, deg + 2 * j) - sc[deg];
    }
    if (len == 7) ninth = harmonyScaleSemis(sc, len, deg + 8) - sc[deg];
  }

  if ((mods & HARM_MOD_SUS) && count > 1) rel[1] = 5;
  if (mods & HARM_MOD_ADD9) {
    boolean hasNinth = false;
    for (byte i = 0; i < count; ++i) {
      if (rel[i] >= 13 && rel[i] <= 15) hasNinth = true;
    }
    if (!hasNinth && count < HARM_MAX_CHORD) rel[count++] = ninth;
  }

  // ascending, without repeats
  for (byte i = 1; i < count; ++i) {
    signed char v = rel[i];
    signed char j = i - 1;
    while (j >= 0 && rel[j] > v) {
      rel[j + 1] = rel[j];
      j--;
    }
    rel[j + 1] = v;
  }
  byte unique = 0;
  for (byte i = 0; i < count; ++i) {
    if (unique == 0 || rel[i] != rel[unique - 1]) rel[unique++] = rel[i];
  }
  return unique;
}

void harmonyInvert(byte* a, byte n, byte k) {
  harmonySortNotes(a, n);
  for (byte i = 0; i < k && i < n; ++i) a[i] += 12;
  harmonySortNotes(a, n);
}

// Smart voicing: the inversion and octave whose notes move least from the previous chord
void harmonySmartVoicing(byte* notes, byte n) {
  byte cand[HARM_MAX_CHORD], best[HARM_MAX_CHORD];
  long bestCost = -1;
  for (byte k = 0; k < n; ++k) {
    for (signed char o = -1; o <= 1; ++o) {
      for (byte i = 0; i < n; ++i) cand[i] = notes[i];
      harmonyInvert(cand, n, k);
      boolean fits = true;
      long sum = 0;
      for (byte i = 0; i < n; ++i) {
        short v = cand[i] + 12 * o;
        if (v < 45 || v > 80) fits = false;
        cand[i] = v;
        sum += v;
      }
      if (!fits) continue;

      long cost = abs(sum * 10 / n - 620);   // stay near the middle of the board
      if (harmonyChordPrevCount == n) {
        for (byte i = 0; i < n; ++i) cost += 100L * abs((short)cand[i] - harmonyChordPrev[i]);
      }
      else if (harmonyChordPrevCount > 0) {
        for (byte i = 0; i < n; ++i) {
          short nearest = 127;
          for (byte j = 0; j < harmonyChordPrevCount; ++j) nearest = min(nearest, (short)abs((short)cand[i] - harmonyChordPrev[j]));
          cost += 100L * nearest;
        }
        for (byte j = 0; j < harmonyChordPrevCount; ++j) {
          short nearest = 127;
          for (byte i = 0; i < n; ++i) nearest = min(nearest, (short)abs((short)cand[i] - harmonyChordPrev[j]));
          cost += 100L * nearest;
        }
      }
      if (bestCost < 0 || cost < bestCost) {
        bestCost = cost;
        for (byte i = 0; i < n; ++i) best[i] = cand[i];
      }
    }
  }
  if (bestCost >= 0) {
    for (byte i = 0; i < n; ++i) notes[i] = best[i];
  }
}

// the notes of a chord pad: voiced chord plus a bass note; the inversion columns put that note in the bass
boolean harmonyBuildChord(byte type, byte deg, byte mods, byte* notes, byte& count, byte& bass, byte& rootPc, signed char* rel) {
  count = harmonyChordTones(type, deg, mods, rel, rootPc);
  if (count == 0) return false;

  for (byte i = 0; i < count; ++i) notes[i] = 48 + rootPc + rel[i];
  if (type == HARM_CHORD_INV1) harmonyInvert(notes, count, 1);
  else if (type == HARM_CHORD_INV2) harmonyInvert(notes, count, 2);
  else if (type == HARM_CHORD_SMART) harmonySmartVoicing(notes, count);
  else harmonySortNotes(notes, count);

  for (byte t = 0; t < 3 && notes[count - 1] > 80; ++t) {
    for (byte i = 0; i < count; ++i) notes[i] -= 12;
  }
  for (byte t = 0; t < 3 && notes[0] < 45; ++t) {
    for (byte i = 0; i < count; ++i) notes[i] += 12;
  }

  byte bassPc = (type == HARM_CHORD_INV1 || type == HARM_CHORD_INV2) ? notes[0] % 12 : rootPc;
  bass = 36 + bassPc;
  return true;
}

void harmonyChordSendOn() {
  if (harmonyPattern != HARM_PAT_SUSTAIN) {
    harmonyPatternStart();
    return;
  }

  harmonyChordSentCount = 0;
  harmonyChordSent[harmonyChordSentCount++] = harmonyChordBass;
  for (byte i = 0; i < harmonyChordCount; ++i) {
    boolean repeat = false;
    for (byte j = 0; j < harmonyChordSentCount; ++j) {
      if (harmonyChordSent[j] == harmonyChordNotes[i]) repeat = true;
    }
    if (!repeat) harmonyChordSent[harmonyChordSentCount++] = harmonyChordNotes[i];
  }
  for (byte i = 0; i < harmonyChordSentCount; ++i) {
    midiSendNoteOn(HARM_CHORD_SPLIT, harmonyChordSent[i], harmonyChordVelocity, HARM_CHORD_CHANNEL);
  }
}

void harmonyChordSendOff() {
  harmonyPatternStop();
  for (byte i = 0; i < harmonyChordSentCount; ++i) {
    midiSendNoteOff(HARM_CHORD_SPLIT, harmonyChordSent[i], HARM_CHORD_CHANNEL);
  }
  harmonyChordSentCount = 0;
}

void harmonyChordStop() {
  harmonyChordSendOff();
  harmonyChordType = -1;
  harmonyChordDeg = -1;
  harmonyChordLatched = false;
  harmonyChordPadCol = -1;
  harmonyChordPadRow = -1;
}

// the melody pads light the tones of the last chord the left hand played
void harmonyLightChord() {
  for (byte pc = 0; pc < 12; ++pc) {
    harmonyLitRole[pc] = -1;
  }
  for (byte i = 0; i < harmonyChordCount; ++i) {
    byte pc = (harmonyChordRootPc + harmonyChordRel[i] + 120) % 12;
    if (harmonyLitRole[pc] < 0) harmonyLitRole[pc] = i;
  }
}

// keep the voicing and its parts together in the chord state
boolean harmonyVoiceChord(byte type, byte deg, byte mods) {
  byte notes[HARM_MAX_CHORD], count, bass, rootPc;
  signed char rel[HARM_MAX_CHORD];
  if (!harmonyBuildChord(type, deg, mods, notes, count, bass, rootPc, rel)) return false;

  harmonyChordType = type;
  harmonyChordDeg = deg;
  harmonyChordMods = mods;
  harmonyChordCount = count;
  harmonyChordBass = bass;
  harmonyChordRootPc = rootPc;
  for (byte i = 0; i < count; ++i) {
    harmonyChordNotes[i] = notes[i];
    harmonyChordRel[i] = rel[i];
    harmonyChordPrev[i] = notes[i];
  }
  harmonyChordPrevCount = count;
  return true;
}

void harmonyChordPress(byte type, byte deg, byte col, byte row) {
  byte mods = harmonyModArmed | harmonyModHeld;
  harmonyModUsed |= harmonyModHeld;
  harmonyModArmed = 0;

  // tapping the latched chord again stops it
  if (harmonyChordLatched && harmonyChordType == type && harmonyChordDeg == deg && harmonyChordMods == mods) {
    harmonyChordStop();
    updateDisplay();
    return;
  }

  harmonyChordSendOff();
  if (!harmonyVoiceChord(type, deg, mods)) {
    harmonyChordStop();
    updateDisplay();
    return;
  }
  harmonyChordLatched = false;
  harmonyChordPadCol = col;
  harmonyChordPadRow = row;
  harmonyChordVelocity = sensorCell->velocity ? sensorCell->velocity : 96;
  harmonyChordLastY = 255;
  harmonyChordLastZ = 255;
  harmonyChordSendOn();
  harmonyLightChord();
  updateDisplay();
}

void harmonyChordRelease() {
  harmonyChordPadCol = -1;
  harmonyChordPadRow = -1;
  if (harmonyLatch) {
    harmonyChordLatched = true;
  }
  else {
    harmonyChordStop();
  }
  updateDisplay();
}

// a harmony change keeps the chord's degree and column; a chord on one channel can't glide per note,
// so it re-strikes in the new harmony unless held notes are set to Hold
void harmonyChordRetune() {
  if (!harmonyPerform || harmonyChordType < 0) return;

  byte oldNotes[HARM_MAX_CHORD + 1], oldCount = harmonyChordSentCount;
  for (byte i = 0; i < oldCount; ++i) oldNotes[i] = harmonyChordSent[i];

  if (!harmonyVoiceChord(harmonyChordType, harmonyChordDeg, harmonyChordMods)) {
    harmonyChordStop();
    return;
  }
  harmonyLightChord();
  if (harmonyRetune == HARM_RETUNE_HOLD) return;
  if (harmonyPattern != HARM_PAT_SUSTAIN) return;   // a pattern plays the new notes on its next hit

  // only re-strike when the notes actually changed
  boolean same = (oldCount == harmonyChordCount + 1);
  if (same) {
    byte check[HARM_MAX_CHORD + 1], n = 0;
    check[n++] = harmonyChordBass;
    for (byte i = 0; i < harmonyChordCount; ++i) check[n++] = harmonyChordNotes[i];
    for (byte i = 0; i < n && same; ++i) {
      if (check[i] != oldNotes[i]) same = false;
    }
  }
  if (!same) {
    harmonyChordSendOff();
    harmonyChordSendOn();
  }
}

void harmonyThumbDown(byte i) {
  if (i == HARM_THUMB_LATCH) {
    harmonyLatch = !harmonyLatch;
    if (!harmonyLatch && harmonyChordLatched) harmonyChordStop();
    updateDisplay();
    return;
  }

  byte bit = 1 << i;
  if (harmonyModArmed & bit) {
    harmonyModArmed &= ~bit;                 // tapping an armed modifier disarms it
  }
  else {
    harmonyModHeld |= bit;
    harmonyModUsed &= ~bit;
    harmonyModDownAt[i] = millis();
  }
  updateDisplay();
}

void harmonyThumbUp(byte i) {
  if (i == HARM_THUMB_LATCH) return;

  byte bit = 1 << i;
  if (!(harmonyModHeld & bit)) return;
  harmonyModHeld &= ~bit;

  // a quick tap arms the modifier for the next chord, a hold only applied while it was down
  if (!(harmonyModUsed & bit) && calcTimeDelta(millis(), harmonyModDownAt[i]) < HARM_MOD_TAP_MS) {
    harmonyModArmed |= bit;
  }
  harmonyModUsed &= ~bit;
  updateDisplay();
}

void harmonyPerformTouch(byte col, byte row) {
  byte type = col - 1;
  if (row == 0) {
    harmonyThumbDown(type);
  }
  else {
    harmonyChordPress(type, row - 1, col, row);
  }
}

// while the chord pad stays down, its Y goes out as CC74 and its pressure as channel pressure
void harmonyPerformHold(byte col, byte row) {
  if (row == 0 || col != harmonyChordPadCol || row != harmonyChordPadRow) return;

  byte y = sensorCell->calibratedY();
  if (y != harmonyChordLastY) {
    harmonyChordLastY = y;
    midiSendControlChange(74, y, HARM_CHORD_CHANNEL);
  }
  byte z = scale1016to127(sensorCell->pressureZ, true);
  if (z != harmonyChordLastZ) {
    harmonyChordLastZ = z;
    midiSendAfterTouch(z, HARM_CHORD_CHANNEL);
  }
}

void harmonyPerformRelease(byte col, byte row) {
  if (row == 0) {
    harmonyThumbUp(col - 1);
  }
  else if (col == harmonyChordPadCol && row == harmonyChordPadRow) {
    harmonyChordRelease();
  }
}

byte harmonyPerformZoneColor(byte col, byte row) {
  byte type = col - 1;
  if (row == 0) {
    if (type == HARM_THUMB_LATCH) return harmonyLatch ? COLOR_GREEN : COLOR_OFF;
    if ((harmonyModArmed | harmonyModHeld) & (1 << type)) return COLOR_WHITE;
    if (type == 1 && harmonyLen != 7) return COLOR_OFF;
    const byte thumbColors[4] = {COLOR_ORANGE, COLOR_PINK, COLOR_YELLOW, COLOR_YELLOW};
    return thumbColors[type];
  }

  byte deg = row - 1;
  if (deg >= harmonyLen) return COLOR_OFF;
  if (harmonyChordType == type && harmonyChordDeg == deg) return COLOR_WHITE;
  return type == HARM_CHORD_SMART ? COLOR_LIME : COLOR_BLUE;
}

/******************************* chord patterns *******************************/

void harmonySetPattern(byte pattern) {
  if (pattern == harmonyPattern) return;
  boolean sounding = harmonyPerform && harmonyChordType >= 0;
  if (sounding) harmonyChordSendOff();
  harmonyPattern = pattern;
  if (sounding) harmonyChordSendOn();
}

// pattern hits follow the pad's velocity
byte harmonyPatVelocity(short base) {
  short v = base * harmonyChordVelocity / 96;
  return constrain(v, 1, 127);
}

void harmonyPatVoiceEnd(byte i) {
  if (harmonyPatVoiceState[i] == HARM_PAT_SOUNDING) {
    midiSendNoteOff(HARM_CHORD_SPLIT, harmonyPatVoiceNote[i], HARM_CHORD_CHANNEL);
  }
  else if (harmonyPatVoiceState[i] == HARM_PAT_WAITING) {
    harmonyPatWaiting--;
  }
  harmonyPatVoiceState[i] = HARM_PAT_FREE;
}

// play a note for gate ticks of the 24PPQ clock, delayUs after now
void harmonyPatNote(byte note, short base, byte gate, unsigned long delayUs) {
  // a note that's still sounding ends before it starts again
  for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
    if (harmonyPatVoiceState[i] != HARM_PAT_FREE && harmonyPatVoiceNote[i] == note) harmonyPatVoiceEnd(i);
  }

  // take a free voice, or the one that would end first
  byte v = 0;
  for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
    if (harmonyPatVoiceState[i] == HARM_PAT_FREE) { v = i; break; }
    if ((long)(harmonyPatVoiceOff[i] - harmonyPatVoiceOff[v]) < 0) v = i;
  }
  harmonyPatVoiceEnd(v);

  harmonyPatVoiceNote[v] = note;
  harmonyPatVoiceVel[v] = harmonyPatVelocity(base);
  harmonyPatVoiceOff[v] = harmonyPatTicks + gate;
  if (delayUs == 0) {
    midiSendNoteOn(HARM_CHORD_SPLIT, note, harmonyPatVoiceVel[v], HARM_CHORD_CHANNEL);
    harmonyPatVoiceState[v] = HARM_PAT_SOUNDING;
  }
  else {
    harmonyPatVoiceDue[v] = micros() + delayUs;
    harmonyPatVoiceState[v] = HARM_PAT_WAITING;
    harmonyPatWaiting++;
  }
}

void harmonyPatHitChord(short base, byte gate) {
  for (byte i = 0; i < harmonyChordCount; ++i) {
    harmonyPatNote(harmonyChordNotes[i], base, gate, 0);
  }
}

void harmonyPatHitBass(short base, byte gate) {
  harmonyPatNote(harmonyChordBass, base, gate, 0);
}

void harmonyPatStrum(boolean up, short base, byte gate) {
  for (byte i = 0; i < harmonyChordCount; ++i) {
    byte n = up ? harmonyChordNotes[harmonyChordCount - 1 - i] : harmonyChordNotes[i];
    harmonyPatNote(n, base - 3 * i, gate, i * HARM_STRUM_GAP_US);
  }
}

// up through the chord and on to the octave above its lowest note
void harmonyPatArpNote(short base, byte gate) {
  if (harmonyChordCount == 0) return;
  byte k = harmonyPatArpIdx++ % (harmonyChordCount + 1);
  byte n = k < harmonyChordCount ? harmonyChordNotes[k] : harmonyChordNotes[0] + 12;
  harmonyPatNote(n, base + 10, gate, 0);
}

// one 16th-note step of the bar, p from 0 to 15; first is the hit that starts the chord
void harmonyPatHit(byte p, boolean first) {
  boolean quarter = (p % 4 == 0);
  boolean eighth = (p % 2 == 0);
  switch (harmonyPattern) {
    case HARM_PAT_PULSE:
      if (eighth || first) harmonyPatHitChord(p == 0 ? 92 : (quarter ? 78 : 64), 7);
      if (quarter || first) harmonyPatHitBass(90, 22);
      break;
    case HARM_PAT_STRUM:
      if (quarter || first) harmonyPatStrum(false, p == 0 ? 96 : 84, 23);
      else if (p == 6 || p == 14) harmonyPatStrum(true, 64, 12);
      if (p == 0 || p == 8 || first) harmonyPatHitBass(92, 46);
      break;
    case HARM_PAT_ARP:
      if (eighth || first) harmonyPatArpNote(p == 0 ? 90 : 76, 11);
      if (p == 0 || first) harmonyPatHitBass(86, 91);
      break;
  }
}

void harmonyPatternStart() {
  harmonyPatActive = true;
  harmonyPatBeat = 0;
  harmonyPatArpIdx = 0;
  harmonyPatStartTick = harmonyPatTicks;
  harmonyPatHit(harmonyPatClock / 6, true);
}

void harmonyPatternStop() {
  harmonyPatActive = false;
  for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
    harmonyPatVoiceEnd(i);
  }
}

// called on every step of the shared 24PPQ clock
void harmonyPatternSharedTick() {
  if (harmonyPatClockSource == HARM_CLOCK_INTERNAL && isSyncedToMidiClock()) return;
  harmonyPatternTick(clock24PPQ);
}

// one step of the clock the patterns follow; pos runs from 0 to 23 within each beat
void harmonyPatternTick(byte pos) {
  harmonyPatClock = pos;
  harmonyPatTicks++;
  harmonyPatLastTickUs = micros();
  if (pos == 0) harmonyPatBeat++;

  for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
    if (harmonyPatVoiceState[i] != HARM_PAT_FREE && (long)(harmonyPatTicks - harmonyPatVoiceOff[i]) >= 0) {
      harmonyPatVoiceEnd(i);
    }
  }

  if (!harmonyPatActive || pos % 6 != 0) return;
  if (harmonyPatTicks - harmonyPatStartTick < 4) return;   // too close after the hit the press played
  harmonyPatHit((harmonyPatBeat % 4) * 4 + pos / 6, false);
}

// called continuously: runs the patterns' own clock when they ignore MIDI clock,
// starts strum notes on time, and ends notes if the clock stops
void harmonyPatternService(unsigned long now) {
  if (harmonyPatClockSource == HARM_CLOCK_INTERNAL && isSyncedToMidiClock()) {
    unsigned long unit = INTERNAL_CLOCK_UNIT_BASE / FXD4_TO_INT(harmonyPatOwnTempo);
    if ((long)(now - harmonyPatOwnNextUs) >= 0) {
      harmonyPatOwnNextUs += unit;
      if ((long)(now - harmonyPatOwnNextUs) >= 0) harmonyPatOwnNextUs = now + unit;   // fell behind
      harmonyPatternTick((harmonyPatClock + 1) % 24);
    }
  }
  else {
    harmonyPatOwnTempo = fxd4CurrentTempo;   // without MIDI clock, this is the tempo setting
    harmonyPatOwnNextUs = now;
  }

  if (harmonyPatWaiting) {
    for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
      if (harmonyPatVoiceState[i] == HARM_PAT_WAITING && (long)(now - harmonyPatVoiceDue[i]) >= 0) {
        midiSendNoteOn(HARM_CHORD_SPLIT, harmonyPatVoiceNote[i], harmonyPatVoiceVel[i], HARM_CHORD_CHANNEL);
        harmonyPatVoiceState[i] = HARM_PAT_SOUNDING;
        harmonyPatWaiting--;
      }
    }
  }
  else if (harmonyPatActive && (long)(now - harmonyPatLastTickUs) > (long)HARM_PAT_STALL_US) {
    for (byte i = 0; i < HARM_PAT_VOICES; ++i) {
      if (harmonyPatVoiceState[i] == HARM_PAT_SOUNDING) harmonyPatVoiceEnd(i);
    }
  }
}
