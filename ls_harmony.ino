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
const byte HARM_IONIAN[7] = {0, 2, 4, 5, 7, 9, 11};
const byte HARM_AEOLIAN[7] = {0, 2, 3, 5, 7, 8, 10};
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
#define HARM_PAGE_LEDTEST   1                // top pad of column 1 on the Harmony page opens the LED check
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
// styles: each one swaps the rules behind the lights and hints
#define HARM_STYLE_POP       0
#define HARM_STYLE_ROCK      1
#define HARM_STYLE_BLUES     2
#define HARM_STYLE_JAZZ      3
#define HARM_STYLE_SOUL      4
#define HARM_STYLE_CINEMATIC 5
#define HARM_STYLE_FLAMENCO  6
#define HARM_STYLE_FREE      7
#define HARM_STYLES          8
#define HARM_PAGE_STYLE      11
#define HARM_PAGE_TEMPLATE   13
#define HARM_AVOID_NONE      0
#define HARM_AVOID_ALL       1               // a note a half step above any chord tone
#define HARM_AVOID_ROOT      2               // a note a half step above the chord's root
#define HARM_FLAVOR_NONE     0
#define HARM_FLAVOR_BLUE     1               // b3 and b5 of the key over major chords
#define HARM_FLAVOR_ALTERED  2               // b9, #9, #11 and b13 over dominant chords
#define HARM_FLAVOR_SIGNATURE 3              // where the mode differs from plain major or minor
#define HARM_FLAVOR_FLAT2    4               // the b2 of the key

const byte HARM_STYLE_COLOR[HARM_STYLES] = {COLOR_CYAN, COLOR_RED, COLOR_BLUE, COLOR_MAGENTA, COLOR_ORANGE, COLOR_GREEN, COLOR_PINK, COLOR_YELLOW};
const byte HARM_STYLE_SIZE[HARM_STYLES] = {4, 3, 4, 4, 5, 4, 3, 0};             // the chord size a style starts with, 0 to keep it
const byte HARM_STYLE_AVOID[HARM_STYLES] = {HARM_AVOID_ALL, HARM_AVOID_ROOT, HARM_AVOID_NONE, HARM_AVOID_ALL,
                                            HARM_AVOID_ROOT, HARM_AVOID_NONE, HARM_AVOID_NONE, HARM_AVOID_NONE};
const byte HARM_STYLE_FLAVOR[HARM_STYLES] = {HARM_FLAVOR_NONE, HARM_FLAVOR_BLUE, HARM_FLAVOR_BLUE, HARM_FLAVOR_ALTERED,
                                             HARM_FLAVOR_NONE, HARM_FLAVOR_SIGNATURE, HARM_FLAVOR_FLAT2, HARM_FLAVOR_NONE};
// natural next moves, one bit per root motion in scale steps (bit 0, the same root, is always natural)
const byte HARM_STYLE_MOVES[HARM_STYLES] = {B0101011, B1011011, B1011001, B1101001, B1101001, B0100111, B1000011, B1111111};
// the thumb modifiers a style leans on stay bright with hints on: bit 0 V/x, 1 Borrow, 2 sus4, 3 add9
const byte HARM_STYLE_MODS[HARM_STYLES] = {B0001, B0010, B0001, B0011, B1010, B1100, B0000, B1111};

// progression templates: the style, the mode it sets (a rotation of the major scale), whether it raises
// the 7th, and the chord degrees in order
#define HARM_TEMPLATES      21
#define HARM_TPL_MAX_STEPS  9
const byte HARM_TPL_STYLE[HARM_TEMPLATES] = {0, 0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 5, 5, 5, 6, 6};
const byte HARM_TPL_MODE[HARM_TEMPLATES]  = {0, 0, 0, 4, 5, 0, 4, 4, 4, 0, 0, 0, 5, 0, 1, 0, 5, 3, 5, 2, 2};
const byte HARM_TPL_RAISE7[HARM_TEMPLATES] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0};
const byte HARM_TPL_LEN[HARM_TEMPLATES]   = {4, 4, 4, 3, 3, 4, 7, 9, 7, 3, 4, 5, 3, 4, 2, 3, 4, 2, 4, 4, 2};
const byte HARM_TPL_STEPS[HARM_TEMPLATES][HARM_TPL_MAX_STEPS] = {
  {0, 4, 5, 3}, {5, 3, 0, 4}, {0, 5, 3, 4},                                       // Pop: I-V-vi-IV, vi-IV-I-V, I-vi-IV-V
  {0, 6, 3}, {0, 5, 6}, {0, 3, 4, 3},                                             // Rock: I-bVII-IV, i-bVI-bVII, I-IV-V-IV
  {0, 3, 0, 4, 3, 0, 4}, {0, 3, 0, 3, 0, 4, 3, 0, 4}, {0, 4, 3, 0, 4, 0, 4},     // Blues: 12-bar, quick-change 12-bar, 8-bar
  {1, 4, 0}, {0, 5, 1, 4}, {2, 5, 1, 4, 0}, {1, 4, 0},                            // Jazz: ii-V-I, I-vi-ii-V, iii-vi-ii-V-I, minor ii-V-i
  {3, 2, 1, 0}, {0, 3}, {1, 4, 0},                                                // Soul: IV-iii-ii-I, i-iv in Dorian, ii-V-I
  {0, 5, 2, 6}, {0, 1}, {0, 6, 5, 6},                                             // Cinematic: i-bVI-bIII-bVII, I-II in Lydian, i-bVII-bVI-bVII
  {3, 2, 1, 0}, {0, 1}                                                            // Flamenco: Andalusian iv-bIII-bII-I, I-bII
};

byte harmonyStyle = HARM_STYLE_POP;
signed char harmonyTemplate = -1;            // the template in use, -1 when off
byte harmonyTplIdx = 0;                      // the template's next step

// lights and hints
#define HARM_LIGHTS_GUIDE   0                // chord root, chord tones, color tones, and notes to pass through
#define HARM_LIGHTS_CHORD   1                // chord tones and altered notes only
#define HARM_LIGHTS_DEGREES 2                // one hue per scale degree
#define HARM_LIGHTS_NOTES   3                // one hue per note name, around the circle of fifths
#define HARM_LIGHTS_COUNT   4
#define HARM_PAGE_LIGHTS    15
#define HARM_PAGE_HINTS     17
#define HARM_CHANGED_MS     1400             // how long notes that a harmony change brings in pulse
#define HARM_FN_HOME        0
#define HARM_FN_AWAY        1
#define HARM_FN_TENSION     2
#define HARM_FN_PULL        3                // the strongest pull home
#define HARM_LIGHT(color, disp)  ((byte)(((disp) << 4) | (color)))   // a color with a cell display, 0 meaning cellOn

byte harmonyLights = HARM_LIGHTS_GUIDE;
boolean harmonyHints = true;
unsigned short harmonyCurSet = 0;            // pitch classes of the current scale, one bit each
unsigned short harmonyChangedPcs = 0;        // pitch classes the last harmony change brought in
unsigned long harmonyChangedUntil = 0;
const byte HARM_DEG_HUES[7] = {COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_GREEN, COLOR_TEAL, COLOR_BLUE, COLOR_VIOLET};
const byte HARM_FIFTHS_COLOR[12] = {COLOR_RED, COLOR_AZURE, COLOR_YELLOW, COLOR_VIOLET, COLOR_GREEN, COLOR_PINK,
                                    COLOR_CYAN, COLOR_ORANGE, COLOR_BLUE, COLOR_LIME, COLOR_MAGENTA, COLOR_TEAL};
const byte HARM_FN_COLOR[4] = {COLOR_AZURE, COLOR_GREEN, COLOR_ORANGE, COLOR_RED};
const byte HARM_CHANGE_COLOR[4] = {COLOR_WHITE, COLOR_GREEN, COLOR_YELLOW, COLOR_ORANGE};

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

  harmonyLoadSaved();

  if (harmonyEnabled) {
    Global.splitActive = false;
  }
  harmonyRecompute();
  harmonySaveSync();
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
  unsigned short set = 0;
  for (byte i = 0; i < harmonyLen; ++i) {
    harmonyDegreeOfPc[(harmonyCur.root + harmonyIv[i]) % 12] = i;
    set |= 1 << ((harmonyCur.root + harmonyIv[i]) % 12);
  }
  if (harmonyCurSet != 0 && set != harmonyCurSet) {
    harmonyChangedPcs = set & ~harmonyCurSet;
    harmonyChangedUntil = millis() + HARM_CHANGED_MS;
  }
  harmonyCurSet = set;

  // the lit chord: the focused chord, or the tonic chord
  if (hepta) {
    byte deg = harmonyCur.chordDeg >= 0 ? harmonyCur.chordDeg : 0;
    signed char styled[5];
    byte n = harmonyStyledRel(deg, harmonyCur.chordSize, styled);
    for (byte j = 0; j < (n ? n : harmonyCur.chordSize); ++j) {
      byte pc = n ? (harmonyCur.root + harmonyIv[deg] + styled[j]) % 12 : (harmonyCur.root + harmonyIv[(deg + 2 * j) % 7]) % 12;
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
  harmonyRefreshPlayedLights();
  updateDisplay();
}

// the "Same" note lights of held notes move with the layout when the harmony changes
void harmonyRefreshPlayedLights() {
  for (byte col = HARM_FIRST_PLAY; col < NUMCOLS; ++col) {
    for (byte row = 0; row < NUMROWS; ++row) {
      if (Split[getSplitOf(col)].playedTouchMode == playedSame) setLed(col, row, COLOR_OFF, cellOff, LED_LAYER_PLAYED);
    }
  }
  for (byte col = HARM_FIRST_PLAY; col < NUMCOLS; ++col) {
    for (byte row = 0; row < NUMROWS; ++row) {
      TouchInfo& c = cell(col, row);
      byte split = getSplitOf(col);
      if (c.touched == touchedCell && c.note >= 0 && Split[split].playedTouchMode == playedSame) {
        harmonyHighlightNote(split, c.note, true);
      }
    }
  }
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
  else if (harmonyTemplate >= 0) {
    harmonyTemplateAdvance(row);
    harmonyCur.chordDeg = row;
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

// the stored memory the next PREV or NEXT press would recall, -1 when none is stored
signed char harmonyMemoryStepTarget(signed char dir) {
  signed char current = -1;
  for (byte i = 0; i < HARM_MEMORIES; ++i) {
    if (harmonyMemIsCurrent(i)) {
      current = i;
      break;
    }
  }
  for (byte n = 1; n <= HARM_MEMORIES; ++n) {
    signed char slot = (current < 0) ? (dir > 0 ? n - 1 : HARM_MEMORIES - n)
                                     : (current + dir * n + HARM_MEMORIES * 2) % HARM_MEMORIES;
    if (harmonyMemUsed[slot]) return slot;
  }
  return -1;
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

  // LED check
  setLed(HARM_PAGE_LEDTEST, 7, COLOR_VIOLET, cellOn);

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

  // style: pop, rock, blues, jazz, soul, cinematic, flamenco, free
  for (byte i = 0; i < HARM_STYLES; ++i) {
    setLed(HARM_PAGE_STYLE, 7 - i, harmonyStyle == i ? COLOR_WHITE : HARM_STYLE_COLOR[i], cellOn);
  }

  // template: off, then the style's templates
  setLed(HARM_PAGE_TEMPLATE, 7, harmonyTemplate < 0 ? COLOR_WHITE : COLOR_RED, cellOn);
  byte shown = 0;
  for (byte t = 0; t < HARM_TEMPLATES && shown < 6; ++t) {
    if (HARM_TPL_STYLE[t] != harmonyStyle) continue;
    setLed(HARM_PAGE_TEMPLATE, 6 - shown, harmonyTemplate == t ? COLOR_WHITE : COLOR_BLUE, cellOn);
    shown++;
  }

  // lights: guide, chord tones, degrees, note names
  for (byte i = 0; i < HARM_LIGHTS_COUNT; ++i) {
    setLed(HARM_PAGE_LIGHTS, 7 - i, harmonyLights == i ? COLOR_WHITE : COLOR_GREEN, cellOn);
  }

  // hints: on, off
  setLed(HARM_PAGE_HINTS, 7, harmonyHints ? COLOR_WHITE : COLOR_YELLOW, cellOn);
  setLed(HARM_PAGE_HINTS, 6, harmonyHints ? COLOR_YELLOW : COLOR_WHITE, cellOn);

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
  else if (col == HARM_PAGE_STYLE) {
    harmonySetStyle(7 - row);
  }
  else if (col == HARM_PAGE_TEMPLATE && row == 7) {
    harmonyTemplate = -1;
  }
  else if (col == HARM_PAGE_TEMPLATE) {
    byte shown = 0;
    for (byte t = 0; t < HARM_TEMPLATES; ++t) {
      if (HARM_TPL_STYLE[t] != harmonyStyle) continue;
      if (6 - shown == row) {
        harmonyApplyTemplate(t);
        break;
      }
      shown++;
    }
  }
  else if (col == HARM_PAGE_LIGHTS && row >= 8 - HARM_LIGHTS_COUNT) {
    harmonyLights = 7 - row;
  }
  else if (col == HARM_PAGE_HINTS && (row == 7 || row == 6)) {
    harmonyHints = (row == 7);
  }
  else if (col == HARM_PAGE_LEDTEST && row == 7) {
    setDisplayMode(displayHarmonyLedTest);
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

  if (displayMode == displayHarmonyLedTest) {
    setDisplayMode(displayHarmony);         // back to the Harmony page
    updateDisplay();
  }
  else if (displayMode == displayHarmony) {
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

// Blues plays I, IV and V as dominant 7ths, Flamenco plays the tonic as a major chord with a b9;
// returns how many tones went into rel, 0 when the style leaves this chord alone
byte harmonyStyledRel(byte deg, byte size, signed char* rel) {
  if (harmonyLen != 7) return 0;
  const signed char blues[5] = {0, 4, 7, 10, 14};
  const signed char flamenco[5] = {0, 4, 7, 10, 13};
  const signed char* src = 0;
  if (harmonyStyle == HARM_STYLE_BLUES && (deg == 0 || deg == 3 || deg == 4)) src = blues;
  else if (harmonyStyle == HARM_STYLE_FLAMENCO && deg == 0) src = flamenco;
  if (!src) return 0;
  byte n = size < 3 ? 3 : (size > 5 ? 5 : size);
  for (byte i = 0; i < n; ++i) rel[i] = src[i];
  return n;
}

// pitch classes of a harmony, one bit each
unsigned short harmonyScaleSet(byte root, byte family, byte mode, const signed char* alter) {
  byte iv[12];
  byte len = harmonyModeIntervals(family, mode, iv);
  unsigned short set = 0;
  for (byte i = 0; i < len; ++i) {
    short a = (len == 7 && alter) ? alter[i] : 0;
    set |= 1 << ((root + iv[i] + a + 24) % 12);
  }
  return set;
}

byte harmonyBitCount(unsigned short x) {
  byte n = 0;
  while (x) {
    n += x & 1;
    x >>= 1;
  }
  return n;
}

// a pad that changes the harmony shows how many notes would change: none (white), 1 (green), 2 (yellow), 3+ (orange)
byte harmonyChangeLight(unsigned short target) {
  byte n = harmonyBitCount(target & ~harmonyCurSet);
  return HARM_LIGHT(HARM_CHANGE_COLOR[n > 3 ? 3 : n], n >= 4 ? cellDimHalf : 0);
}

byte harmonyRootLight(byte pc) {
  return harmonyChangeLight(harmonyScaleSet(pc, harmonyCur.family, harmonyCur.mode, harmonyCur.alter));
}

// the scale REL and PAR would switch to, 0 when they can't
unsigned short harmonyRelativeSet() {
  if (harmonyCur.family == HARM_SYMMETRIC) {
    if (harmonyCur.mode == 0) return harmonyScaleSet((harmonyCur.root + 9) % 12, HARM_SYMMETRIC, 1, 0);
    if (harmonyCur.mode == 1) return harmonyScaleSet((harmonyCur.root + 3) % 12, HARM_SYMMETRIC, 0, 0);
    return 0;
  }
  byte base[12];
  harmonyModeIntervals(harmonyCur.family, harmonyCur.mode, base);
  byte target = (base[2] == 4) ? 5 : 2;
  return harmonyScaleSet((harmonyCur.root + base[target]) % 12, harmonyCur.family, (harmonyCur.mode + target) % 7, 0);
}

unsigned short harmonyParallelSet() {
  if (harmonyCur.family == HARM_SYMMETRIC && harmonyCur.mode <= 1) {
    return harmonyScaleSet(harmonyCur.root, HARM_SYMMETRIC, 1 - harmonyCur.mode, 0);
  }
  boolean major = (harmonyHasInterval(4) && !harmonyHasInterval(3)) || (harmonyLen == 7 && harmonyIv[2] == 4);
  return harmonyScaleSet(harmonyCur.root, 0, major ? 5 : 0, 0);
}

byte harmonyActionLight(unsigned short target) {
  if (!harmonyHints || target == 0) return COLOR_YELLOW;
  return harmonyChangeLight(target);
}

byte harmonyKeyLight(byte pc) {
  if (pc == harmonyCur.root) return COLOR_CYAN;
  if (harmonyHints) return harmonyRootLight(pc);
  return harmonyDegreeOfPc[pc] >= 0 ? COLOR_GREEN : COLOR_OFF;
}

// the pitch classes of the chord on a scale degree, with size notes, in the style's chord qualities
unsigned short harmonyDegreePcs(byte deg, byte size) {
  unsigned short pcs = 0;
  signed char styled[5];
  byte n = harmonyStyledRel(deg, size, styled);
  if (n) {
    for (byte j = 0; j < n; ++j) pcs |= 1 << ((harmonyCur.root + harmonyIv[deg] + styled[j]) % 12);
    return pcs;
  }
  for (byte j = 0; j < size; ++j) {
    pcs |= 1 << ((harmonyCur.root + harmonyIv[(deg + 2 * j) % 7]) % 12);
  }
  return pcs;
}

// the job of a chord, judged by its notes: it pulls hardest home when it holds the leading tone and the note
// a tritone above it (V7, vii), it's tension with the leading tone alone, away when it holds the 4th degree,
// and home otherwise; modes without a leading tone treat v and bVII as their tension chords
#define HARM_FN_FREE        4                // Free judges nothing: every chord is just a chord

byte harmonyChordFunction(byte deg, unsigned short pcs) {
  if (harmonyLen != 7) return HARM_FN_HOME;
  if (harmonyStyle == HARM_STYLE_FREE) return HARM_FN_FREE;
  if (deg == 0) return HARM_FN_HOME;
  if (harmonyStyle == HARM_STYLE_BLUES) return deg == 4 ? HARM_FN_PULL : HARM_FN_AWAY;
  if (harmonyStyle == HARM_STYLE_FLAMENCO) return deg == 1 ? HARM_FN_PULL : (deg == 4 ? HARM_FN_TENSION : HARM_FN_AWAY);
  if (harmonyStyle == HARM_STYLE_CINEMATIC) {
    // judged by how many notes it shares with the home triad
    unsigned short home = harmonyDegreePcs(0, 3);
    byte shared = harmonyBitCount(pcs & home);
    return shared >= 2 ? HARM_FN_HOME : (shared == 1 ? HARM_FN_AWAY : HARM_FN_TENSION);
  }
  if (harmonyStyle == HARM_STYLE_ROCK && deg != 4) return (deg == 1 || deg == 3 || deg == 6) ? HARM_FN_AWAY : HARM_FN_HOME;
  byte lt = (harmonyCur.root + 11) % 12;
  byte fourth = (harmonyCur.root + harmonyIv[3]) % 12;
  if (harmonyDegreeOfPc[lt] >= 0) {
    byte tt = (lt + 6) % 12;
    if ((pcs >> lt & 1) && harmonyDegreeOfPc[tt] >= 0 && (pcs >> tt & 1)) return HARM_FN_PULL;
    if (pcs >> lt & 1) return HARM_FN_TENSION;
  }
  else if (deg == 4 || deg == 6) {
    return HARM_FN_TENSION;
  }
  return (pcs >> fourth & 1) ? HARM_FN_AWAY : HARM_FN_HOME;
}

boolean harmonyNaturalMove(byte from, byte to) {
  byte st = (to - from + 7) % 7;
  return (HARM_STYLE_MOVES[harmonyStyle] >> st) & 1;
}

// the mode's signature notes: where it differs from plain major or minor
unsigned short harmonySignaturePcs() {
  if (harmonyLen != 7) return 0;
  const byte* ref = harmonyIv[2] == 4 ? HARM_IONIAN : HARM_AEOLIAN;
  unsigned short pcs = 0;
  for (byte i = 0; i < 7; ++i) {
    if (harmonyIv[i] != ref[i]) pcs |= 1 << ((harmonyCur.root + harmonyIv[i]) % 12);
  }
  return pcs;
}

// whether the style wants to go from the chord on cur to the one on deg next
boolean harmonyPulseTarget(byte cur, unsigned short curPcs, byte deg) {
  switch (harmonyStyle) {
    case HARM_STYLE_FREE: return false;
    case HARM_STYLE_JAZZ:
    case HARM_STYLE_SOUL: return deg == (cur + 3) % 7;
    case HARM_STYLE_ROCK: return deg == 0 && (cur == 3 || cur == 4 || cur == 6);
    case HARM_STYLE_BLUES: return (cur == 4 && (deg == 3 || deg == 0)) || (cur == 3 && deg == 0);
    case HARM_STYLE_FLAMENCO: return (cur == 1 || cur == 2 || cur == 3) && deg == (cur + 6) % 7;
    case HARM_STYLE_CINEMATIC: {
      if (deg == cur) return false;
      unsigned short sig = harmonySignaturePcs();
      return (sig >> ((harmonyCur.root + harmonyIv[deg]) % 12) & 1) || (sig >> ((harmonyCur.root + harmonyIv[(deg + 2) % 7]) % 12) & 1);
    }
  }
  byte fn = harmonyChordFunction(cur, curPcs);
  return deg == 0 && (fn == HARM_FN_TENSION || fn == HARM_FN_PULL);
}

// the degree a template wants next, -1 when no template is on
signed char harmonyTemplateNext() {
  if (harmonyTemplate < 0) return -1;
  return HARM_TPL_STEPS[harmonyTemplate][harmonyTplIdx % HARM_TPL_LEN[harmonyTemplate]];
}

void harmonyTemplateAdvance(byte deg) {
  if (harmonyTemplate >= 0 && deg == harmonyTemplateNext()) {
    harmonyTplIdx = (harmonyTplIdx + 1) % HARM_TPL_LEN[harmonyTemplate];
  }
}

// a chord pad's hint: its function's color, dim when it's not a natural next move for the style, and
// pulsing where the style (or the template) wants to go next; pullsInto is the degree a sounding V/x or
// substitute chord leads into, -1 otherwise
byte harmonyChordHintLight(byte deg, unsigned short pcs, byte cur, unsigned short curPcs, signed char pullsInto) {
  byte fn = harmonyChordFunction(deg, pcs);
  signed char next = harmonyTemplateNext();
  boolean pulse = next >= 0 ? deg == next : (pullsInto >= 0 ? deg == pullsInto : harmonyPulseTarget(cur, curPcs, deg));
  byte disp = pulse ? cellFocusPulse : (harmonyNaturalMove(cur, deg) ? 0 : cellDimHalf);
  return HARM_LIGHT(fn == HARM_FN_FREE ? COLOR_BLUE : HARM_FN_COLOR[fn], disp);
}

byte harmonyZoneColor(byte col, byte row) {
  if (harmonyPerform && col < HARM_COL_MEMORY) return harmonyPerformZoneColor(col, row);

  boolean hepta = (harmonyLen == 7);
  switch (col) {
    case HARM_COL_KEYACC:
      if (row == 2) return harmonyActionLight(harmonyRelativeSet());
      if (row == 6) return harmonyActionLight(harmonyParallelSet());
      if (HARM_ACCIDENTAL_PC[row] < 0) return harmonyHints ? harmonyRootLight((harmonyCur.root + 5) % 12) : COLOR_YELLOW;
      return harmonyKeyLight(HARM_ACCIDENTAL_PC[row]);

    case HARM_COL_KEYNAT:
      if (row == 7) return harmonyHints ? harmonyRootLight((harmonyCur.root + 7) % 12) : COLOR_YELLOW;
      return harmonyKeyLight(HARM_NATURAL_PC[row]);

    case HARM_COL_MODE: {
      if (row == 7) return HARM_FAMILY_COLOR[harmonyCur.family];
      byte m = harmonyModeOrder[harmonyCur.family][6 - row];
      if (m == harmonyCur.mode) return COLOR_WHITE;
      if (harmonyHints) {
        return harmonyChangeLight(harmonyScaleSet(harmonyCur.root, harmonyCur.family, m, harmonyKeepAlter ? harmonyCur.alter : 0));
      }
      return HARM_FAMILY_COLOR[harmonyCur.family];
    }

    case HARM_COL_ALTER:
      if (!hepta) return COLOR_OFF;
      if (row == 0) {
        for (byte i = 0; i < 7; ++i) {
          if (harmonyCur.alter[i]) return COLOR_RED;
        }
        return COLOR_OFF;
      }
      if (row == 7) return harmonyKeepAlter ? COLOR_YELLOW : COLOR_OFF;
      if (harmonyCur.alter[row]) return (harmonyHints && harmonyCur.alter[row] > 0) ? COLOR_MAGENTA : COLOR_ORANGE;
      return COLOR_BLUE;

    case HARM_COL_CHORD: {
      if (!hepta) return COLOR_OFF;
      if (row == 7) return harmonyCur.chordSize == 3 ? COLOR_LIME : (harmonyCur.chordSize == 4 ? COLOR_YELLOW : COLOR_ORANGE);
      byte cur = harmonyCur.chordDeg >= 0 ? harmonyCur.chordDeg : 0;
      boolean tplNext = (harmonyTemplateNext() == row);
      if (harmonyCur.chordDeg == row || (harmonyHints && row == cur)) return HARM_LIGHT(COLOR_WHITE, tplNext ? cellFocusPulse : 0);
      if (!harmonyHints) return HARM_LIGHT(COLOR_BLUE, tplNext ? cellFocusPulse : 0);
      return harmonyChordHintLight(row, harmonyDegreePcs(row, harmonyCur.chordSize), cur, harmonyDegreePcs(cur, harmonyCur.chordSize), -1);
    }

    case HARM_COL_MEMORY: {
      if (!harmonyMemUsed[row]) return COLOR_OFF;
      if (harmonyMemIsCurrent(row)) return COLOR_WHITE;
      if (!harmonyHints) return COLOR_BLUE;
      HarmonyState& m = harmonyMem[row];
      byte light = harmonyChangeLight(harmonyScaleSet(m.root, m.family, m.mode, m.alter));
      if (harmonyMemoryStepTarget(1) == row) light = HARM_LIGHT(light & 0x0F, cellFocusPulse);   // the next NEXT press
      return light;
    }
  }
  return COLOR_OFF;
}

// the style's flavor notes: blue notes over major chords (Rock, Blues), altered notes on dominants (Jazz),
// the mode's signature notes (Cinematic), the b2 (Flamenco)
boolean harmonyFlavorPc(byte pc) {
  signed char litRoot = -1;
  for (byte i = 0; i < 12; ++i) {
    if (harmonyLitRole[i] == 0) litRoot = i;
  }
  if (litRoot < 0) return false;
  byte rk = (pc - harmonyCur.root + 12) % 12;
  byte rc = (pc - litRoot + 12) % 12;
  boolean major = harmonyLitRole[(litRoot + 4) % 12] >= 0;
  switch (HARM_STYLE_FLAVOR[harmonyStyle]) {
    case HARM_FLAVOR_BLUE: return major && (rk == 3 || rk == 6);
    case HARM_FLAVOR_ALTERED: return major && harmonyLitRole[(litRoot + 10) % 12] >= 0 && (rc == 1 || rc == 3 || rc == 6 || rc == 8);
    case HARM_FLAVOR_SIGNATURE: return (harmonySignaturePcs() >> pc) & 1;
    case HARM_FLAVOR_FLAT2: return rk == 1;
  }
  return false;
}

// the playing surface, by the Lights setting
byte harmonyPlayColor(byte col, byte row) {
  byte note = harmonyNotes[col][row];
  if (note == HARM_NOTE_NONE) return COLOR_OFF;

  byte split = getSplitOf(col);
  short actual = note + Split[split].transposeOctave + Split[split].transposePitch;
  if (actual < 0 || actual > 127) return COLOR_OFF;

  byte pc = note % 12;
  signed char deg = harmonyDegreeOfPc[pc];
  if (deg < 0) return COLOR_OFF;

  signed char role = harmonyLitRole[pc];
  boolean inChord = (role >= 0);
  byte light;
  switch (harmonyLights) {
    case HARM_LIGHTS_GUIDE: {
      if (role == 0) { light = COLOR_CYAN; break; }
      if (inChord) { light = COLOR_GREEN; break; }
      if (harmonyStyle == HARM_STYLE_FREE) { light = COLOR_OFF; break; }
      if (harmonyFlavorPc(pc)) { light = COLOR_VIOLET; break; }
      // a note a half step above a chord tone (or the root, by style) is tense when held: pass through it
      byte below = (pc + 11) % 12;
      boolean avoid = HARM_STYLE_AVOID[harmonyStyle] == HARM_AVOID_ALL ? harmonyLitRole[below] >= 0
                    : HARM_STYLE_AVOID[harmonyStyle] == HARM_AVOID_ROOT ? harmonyLitRole[below] == 0 : false;
      light = avoid ? HARM_LIGHT(COLOR_RED, cellDimHalf) : COLOR_AZURE;
      break;
    }
    case HARM_LIGHTS_DEGREES:
      light = HARM_LIGHT(HARM_DEG_HUES[deg % 7], inChord ? 0 : cellDimHalf);
      break;
    case HARM_LIGHTS_NOTES:
      light = HARM_LIGHT(HARM_FIFTHS_COLOR[pc], inChord ? 0 : cellDimHalf);
      break;
    default: {
      // in the scale layout every pad is a scale tone, so only the lit chord's tones and altered notes light up
      boolean altered = (harmonyLen == 7 && harmonyCur.alter[deg] != 0);
      if (role == 0) light = COLOR_CYAN;
      else if (inChord || altered) light = altered ? COLOR_ORANGE : COLOR_GREEN;
      else light = COLOR_OFF;
    }
  }

  // notes the last harmony change brought in pulse for a moment
  if ((harmonyChangedPcs >> pc & 1) && (light & 0x0F) != COLOR_OFF) {
    light = HARM_LIGHT(light & 0x0F, cellSlowPulse);
  }
  return light;
}

void paintHarmonyCell(byte col, byte row) {
  byte light = harmonyIsZoneCol(col) ? harmonyZoneColor(col, row) : harmonyPlayColor(col, row);
  byte color = light & 0x0F;
  byte disp = light >> 4;
  if (row == 0) {
    clearLed(col, row, LED_LAYER_LOWROW);
  }
  setLed(col, row, color, color == COLOR_OFF ? cellOff : (disp ? (CellDisplay)disp : cellOn), LED_LAYER_MAIN);
}

// called continuously: ends the pulse of newly brought-in notes
void harmonyLightsService() {
  if (!harmonyActive() || displayMode != displayNormal) return;
  if (harmonyChangedPcs && (long)(millis() - harmonyChangedUntil) >= 0) {
    harmonyChangedPcs = 0;
    updateDisplay();
  }
}

// the stock "Same" note lights find a note's pads with the chromatic layout's math; in harmony mode
// light the playing pads that really play the note instead, and never the harmony zone
boolean harmonyHighlightNote(byte split, byte notenum, boolean on) {
  if (!harmonyActive()) return false;
  for (byte col = HARM_FIRST_PLAY; col < NUMCOLS; ++col) {
    for (byte row = 0; row < NUMROWS; ++row) {
      byte note = harmonyNotes[col][row];
      if (note == HARM_NOTE_NONE) continue;
      if (note + Split[split].transposeOctave + Split[split].transposePitch != notenum) continue;
      if (on) setLed(col, row, Split[split].colorPlayed, cellOn, LED_LAYER_PLAYED);
      else setLed(col, row, COLOR_OFF, cellOff, LED_LAYER_PLAYED);
    }
  }
  return true;
}

/******************************* Perform mode: left-hand chords *******************************/


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

  boolean sub = (mods & HARM_MOD_BRW) && harmonyStyle == HARM_STYLE_JAZZ;
  if ((mods & HARM_MOD_VX) || sub) {
    // the dominant 7th that leads into this degree's chord; in Jazz, Borrow plays its tritone substitute
    rootPc = (harmonyCur.root + harmonyIv[deg] + (sub && !(mods & HARM_MOD_VX) ? 1 : 7)) % 12;
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
    if (!(mods & HARM_MOD_BRW)) {
      byte styled = harmonyStyledRel(deg, size, rel);
      if (styled) count = styled;
    }
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
  if (!(mods & (HARM_MOD_VX | HARM_MOD_BRW))) harmonyTemplateAdvance(deg);
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

unsigned short harmonyChordPcs(byte type, byte deg, byte mods) {
  signed char rel[HARM_MAX_CHORD];
  byte rootPc;
  byte count = harmonyChordTones(type, deg, mods, rel, rootPc);
  unsigned short pcs = 0;
  for (byte i = 0; i < count; ++i) {
    pcs |= 1 << ((rootPc + rel[i] + 120) % 12);
  }
  return pcs;
}

byte harmonyPerformZoneColor(byte col, byte row) {
  byte type = col - 1;
  byte mods = harmonyModArmed | harmonyModHeld;
  if (row == 0) {
    if (type == HARM_THUMB_LATCH) return harmonyLatch ? COLOR_GREEN : COLOR_OFF;
    if (mods & (1 << type)) return COLOR_WHITE;
    if (type == 1 && harmonyLen != 7) return COLOR_OFF;
    const byte thumbColors[4] = {COLOR_ORANGE, COLOR_PINK, COLOR_YELLOW, COLOR_YELLOW};
    // the modifiers the style leans on stay bright
    return HARM_LIGHT(thumbColors[type], (harmonyHints && !(HARM_STYLE_MODS[harmonyStyle] >> type & 1)) ? cellDimHalf : 0);
  }

  byte deg = row - 1;
  if (deg >= harmonyLen) return COLOR_OFF;
  if (harmonyChordType == type && harmonyChordDeg == deg) return COLOR_WHITE;
  byte plain = type == HARM_CHORD_SMART ? COLOR_LIME : COLOR_BLUE;
  if (!harmonyHints) return HARM_LIGHT(plain, harmonyTemplateNext() == deg ? cellFocusPulse : 0);

  unsigned short pcs = harmonyChordPcs(type, deg, mods);
  if (pcs == 0) return COLOR_OFF;

  if (harmonyLen != 7) return plain;
  // a dominant (or in Jazz a tritone substitute) that pulls into this row
  if ((mods & HARM_MOD_VX) || ((mods & HARM_MOD_BRW) && harmonyStyle == HARM_STYLE_JAZZ)) return COLOR_RED;

  byte cur = harmonyChordType >= 0 ? harmonyChordDeg : 0;
  unsigned short curPcs = harmonyChordType >= 0 ? harmonyChordPcs(harmonyChordType, harmonyChordDeg, harmonyChordMods)
                                                 : harmonyDegreePcs(0, 3);
  signed char pullsInto = -1;
  if (harmonyChordType >= 0 && ((harmonyChordMods & HARM_MOD_VX) || ((harmonyChordMods & HARM_MOD_BRW) && harmonyStyle == HARM_STYLE_JAZZ))) {
    pullsInto = harmonyChordDeg;
  }
  return harmonyChordHintLight(deg, pcs, cur, curPcs, pullsInto);
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

/******************************* LED check *******************************/

// every color, from red around the hue circle to white, one column apart;
// rows from the top: full brightness, half, a quarter, and off
void paintHarmonyLedTest() {
  clearDisplay();
  const byte colors[13] = {COLOR_RED, COLOR_ORANGE, COLOR_YELLOW, COLOR_LIME, COLOR_GREEN, COLOR_TEAL, COLOR_CYAN,
                           COLOR_AZURE, COLOR_BLUE, COLOR_VIOLET, COLOR_MAGENTA, COLOR_PINK, COLOR_WHITE};
  for (byte i = 0; i < 13; ++i) {
    byte col = 1 + 2 * i;
    if (col >= NUMCOLS) break;
    setLed(col, 7, colors[i], cellOn);
    setLed(col, 6, colors[i], cellOn);
    setLed(col, 5, colors[i], cellDimHalf);
    setLed(col, 4, colors[i], cellDimHalf);
    setLed(col, 3, colors[i], cellDimQuarter);
    setLed(col, 2, colors[i], cellDimQuarter);
  }
}

/******************************* styles and templates *******************************/

void harmonySetStyle(byte style) {
  if (style >= HARM_STYLES) return;
  harmonyStyle = style;
  harmonyTemplate = -1;
  harmonyTplIdx = 0;
  if (HARM_STYLE_SIZE[style]) harmonyCur.chordSize = HARM_STYLE_SIZE[style];
  harmonyChanged();
}

// a template sets its mode in the current key and lights the way through its chords
void harmonyApplyTemplate(byte t) {
  if (t >= HARM_TEMPLATES) return;
  harmonyTemplate = t;
  harmonyTplIdx = 0;
  harmonyCur.family = 0;
  harmonyCur.mode = HARM_TPL_MODE[t];
  harmonyClearAlter();
  if (HARM_TPL_RAISE7[t]) harmonyCur.alter[6] = 1;
  harmonyCur.chordDeg = -1;
  harmonyChanged();
}

/******************************* companion app *******************************/

// A private SysEx protocol over USB MIDI, so an app on the computer can mirror the harmony:
//   F0 7D 48 42 <version> <command> <data...> F7
// 7D is MIDI's manufacturer ID for non-commercial use, 48 42 is "HB", and every data byte is 0-127.
// The LinnStrument sends STATE whenever the harmony or a setting changes, and MEMORIES whenever a
// memory changes; REQUEST from the app makes it send both right away. COMPANION.md has the byte layout.
#define HARM_SX_ID          0x7D
#define HARM_SX_H           0x48
#define HARM_SX_B           0x42
#define HARM_SX_VERSION     1
#define HARM_SX_STATE       0x01
#define HARM_SX_MEMORIES    0x02
#define HARM_SX_FRAME       0x03
#define HARM_SX_REQUEST     0x10
#define HARM_SX_HEADER      5
#define HARM_SX_STATE_LEN   34
#define HARM_SX_MEM_SLOT    13
#define HARM_SX_MEM_LEN     (HARM_MEMORIES * HARM_SX_MEM_SLOT)
#define HARM_SX_STATE_MS    20               // how often to look for state changes
#define HARM_SX_MEM_MS      100
#define HARM_SX_FRAME_LEN   (2 + MAXCOLS * MAXROWS)
#define HARM_SX_FRAME_MS    50

byte harmonySxState[HARM_SX_STATE_LEN];      // what was sent last
byte harmonySxMem[HARM_SX_MEM_LEN];
boolean harmonySxForceState = true;
boolean harmonySxForceMem = true;
unsigned long harmonySxStateAt = 0;
unsigned long harmonySxMemAt = 0;
byte harmonySxFrame[HARM_SX_FRAME_LEN];      // the last LED frame sent
boolean harmonySxForceFrame = true;
unsigned long harmonySxFrameAt = 0;

void harmonySxBuildState(byte* d) {
  byte i = 0;
  d[i++] = harmonyEnabled ? 1 : 0;
  d[i++] = harmonyPerform ? 1 : 0;
  d[i++] = displayMode == displayHarmony ? 1 : 0;
  d[i++] = harmonyCur.root;
  d[i++] = harmonyCur.family;
  d[i++] = harmonyCur.mode;
  for (byte k = 0; k < 7; ++k) {
    d[i++] = harmonyCur.alter[k] + 2;
  }
  d[i++] = harmonyCur.chordDeg + 1;
  d[i++] = harmonyCur.chordSize;
  d[i++] = harmonyKeepAlter ? 1 : 0;
  d[i++] = harmonyRowIdx;
  d[i++] = harmonyRetune;
  d[i++] = harmonyStyle;
  d[i++] = harmonyTemplate + 1;
  d[i++] = harmonyTplIdx;
  d[i++] = harmonyLights;
  d[i++] = harmonyHints ? 1 : 0;
  d[i++] = harmonyPattern;
  d[i++] = harmonyPatClockSource;
  d[i++] = harmonyLatch ? 1 : 0;
  d[i++] = harmonyChordType + 1;
  d[i++] = harmonyChordType >= 0 ? harmonyChordDeg : 0;
  d[i++] = harmonyChordMods & 0x7F;
  d[i++] = (harmonyModArmed | harmonyModHeld) & 0x7F;
  d[i++] = Split[getSplitOf(HARM_FIRST_PLAY)].transposeOctave / 12 + 5;
  byte used = 0;
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    if (harmonyMemUsed[m]) used |= 1 << m;
  }
  d[i++] = used & 0x7F;
  d[i++] = used >> 7;
  d[i++] = displayMode;
}

// settings screens go out as the LEDs themselves, so the app shows exactly what the LinnStrument shows;
// not in normal play (the state covers that, without traffic while you play) or during sleep and animations
boolean harmonySxSendsFrames() {
  return displayMode != displayNormal && displayMode != displaySleep && displayMode != displayAnimation;
}

void harmonySxBuildFrame(byte* d) {
  byte i = 0;
  d[i++] = NUMCOLS;
  d[i++] = NUMROWS;
  for (byte row = 0; row < NUMROWS; ++row) {
    for (byte col = 0; col < NUMCOLS; ++col) {
      d[i++] = getLedForMirror(col, row);
    }
  }
}

void harmonySxBuildMemories(byte* d) {
  byte i = 0;
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    HarmonyState& s = harmonyMem[m];
    d[i++] = harmonyMemUsed[m] ? 1 : 0;
    d[i++] = s.root;
    d[i++] = s.family;
    d[i++] = s.mode;
    for (byte k = 0; k < 7; ++k) {
      d[i++] = s.alter[k] + 2;
    }
    d[i++] = s.chordDeg + 1;
    d[i++] = s.chordSize;
  }
}

void harmonySxSend(byte command, const byte* data, byte len) {
  byte msg[HARM_SX_HEADER + HARM_SX_FRAME_LEN];
  msg[0] = HARM_SX_ID;
  msg[1] = HARM_SX_H;
  msg[2] = HARM_SX_B;
  msg[3] = HARM_SX_VERSION;
  msg[4] = command;
  memcpy(&msg[HARM_SX_HEADER], data, len);
  midiSendSysExMessage(msg, HARM_SX_HEADER + len);
}

// called continuously: sends the state or the memories when they changed, one message at a time
void harmonyCompanionService() {
  if (!harmonyReady || Device.serialMode || isMidiUsingDIN() || !midiSysExIdle()) return;

  unsigned long now = millis();
  if (harmonySxForceState || calcTimeDelta(now, harmonySxStateAt) >= HARM_SX_STATE_MS) {
    harmonySxStateAt = now;
    byte d[HARM_SX_STATE_LEN];
    harmonySxBuildState(d);
    if (harmonySxForceState || memcmp(d, harmonySxState, HARM_SX_STATE_LEN) != 0) {
      if (d[HARM_SX_STATE_LEN - 1] != harmonySxState[HARM_SX_STATE_LEN - 1]) harmonySxForceFrame = true;   // a new screen
      memcpy(harmonySxState, d, HARM_SX_STATE_LEN);
      harmonySxForceState = false;
      harmonySxSend(HARM_SX_STATE, d, HARM_SX_STATE_LEN);
      return;
    }
  }
  if (harmonySxForceMem || calcTimeDelta(now, harmonySxMemAt) >= HARM_SX_MEM_MS) {
    harmonySxMemAt = now;
    byte d[HARM_SX_MEM_LEN];
    harmonySxBuildMemories(d);
    if (harmonySxForceMem || memcmp(d, harmonySxMem, HARM_SX_MEM_LEN) != 0) {
      memcpy(harmonySxMem, d, HARM_SX_MEM_LEN);
      harmonySxForceMem = false;
      harmonySxSend(HARM_SX_MEMORIES, d, HARM_SX_MEM_LEN);
      return;
    }
  }
  if (harmonySxSendsFrames() && (harmonySxForceFrame || calcTimeDelta(now, harmonySxFrameAt) >= HARM_SX_FRAME_MS)) {
    harmonySxFrameAt = now;
    byte d[HARM_SX_FRAME_LEN];
    harmonySxBuildFrame(d);
    byte len = 2 + NUMCOLS * NUMROWS;
    if (harmonySxForceFrame || memcmp(d, harmonySxFrame, len) != 0) {
      memcpy(harmonySxFrame, d, len);
      harmonySxForceFrame = false;
      harmonySxSend(HARM_SX_FRAME, d, len);
    }
  }
}

// incoming SysEx, without F0 and F7
void harmonyHandleSysEx(const byte* d, short len) {
  if (len < HARM_SX_HEADER || d[0] != HARM_SX_ID || d[1] != HARM_SX_H || d[2] != HARM_SX_B) return;
  if (d[4] == HARM_SX_REQUEST) {
    harmonySxForceState = true;
    harmonySxForceMem = true;
    harmonySxForceFrame = true;
  }
}

/******************************* saving across power-off *******************************/

// The harmony, the memories and the Harmony page settings live in their own block, outside the factory
// settings, so the factory settings keep their size and layout. The block rotates through 16 flash pages
// at the end of the storage bank: each save goes to the next page with a higher sequence number, and
// loading picks the newest page that checks out, so a save cut short by a power loss leaves the previous
// one in place, and the wear is spread over 16 pages. Saves wait until nothing has changed for a few
// seconds, no pad is touched and no pattern is playing, since writing flash pauses the LinnStrument for
// a few milliseconds.
//
// Each page holds a record: "HBS1", a 4-byte sequence number, the format version, the payload length,
// the payload, and a CRC-32 of everything before it.
#define HARM_FLASH_BASE     0x3F000          // the last 4 KB of the storage bank, far past the projects and settings
#define HARM_FLASH_SLOTS    16
#define HARM_FLASH_PAGE     256
#define HARM_SAVE_VERSION   1
#define HARM_SAVE_STATE     12               // one harmony: root, family, mode, 7 alterations, chord degree, chord size
#define HARM_SAVE_SETTINGS  13
#define HARM_SAVE_PAYLOAD   (HARM_SAVE_STATE + 1 + HARM_MEMORIES * HARM_SAVE_STATE + HARM_SAVE_SETTINGS)
#define HARM_SAVE_HEADER    10               // magic, sequence, version, length
#define HARM_SAVE_RECORD    (HARM_SAVE_HEADER + HARM_SAVE_PAYLOAD + 4)
#define HARM_SAVE_CHECK_MS  500
#define HARM_SAVE_QUIET_MS  3000

byte harmonySaved[HARM_SAVE_PAYLOAD];        // what the newest saved record holds
byte harmonySavePending[HARM_SAVE_PAYLOAD];  // the payload last seen, to tell when changes have settled
uint32_t harmonySaveSeq = 0;                 // the newest saved record's sequence number
signed char harmonySaveSlot = -1;            // and its page, -1 when nothing is saved
unsigned long harmonySaveCheckAt = 0;
unsigned long harmonySaveChangedAt = 0;

void harmonyPutState(byte* d, byte root, byte family, byte mode, const signed char* alter, signed char chordDeg, byte chordSize) {
  d[0] = root;
  d[1] = family;
  d[2] = mode;
  for (byte k = 0; k < 7; ++k) {
    d[3 + k] = alter[k] + 2;
  }
  d[10] = chordDeg + 1;
  d[11] = chordSize;
}

boolean harmonyStateValid(const byte* d) {
  if (d[0] > 11 || d[1] >= HARM_FAMILIES || d[2] > 6 || d[10] > 7 || d[11] < 3 || d[11] > 5) return false;
  for (byte k = 0; k < 7; ++k) {
    if (d[3 + k] > 4) return false;
  }
  return true;
}

void harmonySaveBuild(byte* d) {
  harmonyPutState(d, harmonyCur.root, harmonyCur.family, harmonyCur.mode, harmonyCur.alter, harmonyCur.chordDeg, harmonyCur.chordSize);
  byte i = HARM_SAVE_STATE;
  byte used = 0;
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    if (harmonyMemUsed[m]) used |= 1 << m;
  }
  d[i++] = used;
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    HarmonyState& s = harmonyMem[m];
    harmonyPutState(&d[i], s.root, s.family, s.mode, s.alter, s.chordDeg, s.chordSize);
    i += HARM_SAVE_STATE;
  }
  d[i++] = harmonyEnabled ? 1 : 0;
  d[i++] = harmonyPerform ? 1 : 0;
  d[i++] = harmonyLatch ? 1 : 0;
  d[i++] = harmonyKeepAlter ? 1 : 0;
  d[i++] = harmonyRowIdx;
  d[i++] = harmonyRetune;
  d[i++] = harmonyStyle;
  d[i++] = harmonyTemplate + 1;
  d[i++] = harmonyTplIdx;
  d[i++] = harmonyLights;
  d[i++] = harmonyHints ? 1 : 0;
  d[i++] = harmonyPattern;
  d[i++] = harmonyPatClockSource;
}

// every value in range, so a damaged or foreign record is never applied
boolean harmonySavePayloadValid(const byte* d) {
  if (!harmonyStateValid(d)) return false;
  byte i = HARM_SAVE_STATE + 1;
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    if ((d[HARM_SAVE_STATE] >> m & 1) && !harmonyStateValid(&d[i])) return false;
    i += HARM_SAVE_STATE;
  }
  const byte* t = &d[i];
  if (t[0] > 1 || t[1] > 1 || t[2] > 1 || t[3] > 1 || t[4] > 3 || t[5] > 2 || t[6] >= HARM_STYLES) return false;
  if (t[7] > HARM_TEMPLATES || t[9] >= HARM_LIGHTS_COUNT || t[10] > 1 || t[11] >= HARM_PAT_COUNT || t[12] > 1) return false;
  if (t[7] > 0 && (HARM_TPL_STYLE[t[7] - 1] != t[6] || t[8] >= HARM_TPL_LEN[t[7] - 1])) return false;
  return true;
}

void harmonyGetState(const byte* d, byte* root, byte* family, byte* mode, signed char* alter, signed char* chordDeg, byte* chordSize) {
  *root = d[0];
  *family = d[1];
  *mode = d[2];
  for (byte k = 0; k < 7; ++k) {
    alter[k] = d[3 + k] - 2;
  }
  *chordDeg = d[10] - 1;
  *chordSize = d[11];
}

void harmonySaveApply(const byte* d) {
  harmonyGetState(d, &harmonyCur.root, &harmonyCur.family, &harmonyCur.mode, harmonyCur.alter, &harmonyCur.chordDeg, &harmonyCur.chordSize);
  byte i = HARM_SAVE_STATE;
  byte used = d[i++];
  for (byte m = 0; m < HARM_MEMORIES; ++m) {
    harmonyMemUsed[m] = (used >> m) & 1;
    if (harmonyMemUsed[m]) {
      HarmonyState& s = harmonyMem[m];
      harmonyGetState(&d[i], &s.root, &s.family, &s.mode, s.alter, &s.chordDeg, &s.chordSize);
    }
    i += HARM_SAVE_STATE;
  }
  harmonyEnabled = d[i++];
  harmonyPerform = d[i++];
  harmonyLatch = d[i++];
  harmonyKeepAlter = d[i++];
  harmonyRowIdx = d[i++];
  harmonyRetune = d[i++];
  harmonyStyle = d[i++];
  harmonyTemplate = (signed char)d[i++] - 1;
  harmonyTplIdx = d[i++];
  harmonyLights = d[i++];
  harmonyHints = d[i++];
  harmonyPattern = d[i++];
  harmonyPatClockSource = d[i++];
}

// a record's sequence number when it checks out, or 0
uint32_t harmonyRecordSeq(const byte* r) {
  if (r[0] != 'H' || r[1] != 'B' || r[2] != 'S' || r[3] != '1') return 0;
  if (r[8] != HARM_SAVE_VERSION || r[9] != HARM_SAVE_PAYLOAD) return 0;
  uint32_t crc;
  memcpy(&crc, &r[HARM_SAVE_HEADER + HARM_SAVE_PAYLOAD], 4);
  if (crc != crc_byte_array((uint8_t*)r, HARM_SAVE_HEADER + HARM_SAVE_PAYLOAD)) return 0;
  if (!harmonySavePayloadValid(&r[HARM_SAVE_HEADER])) return 0;
  uint32_t seq;
  memcpy(&seq, &r[4], 4);
  return seq;
}

const byte* harmonySlotRecord(byte slot) {
  return dueFlashStorage.readAddress(HARM_FLASH_BASE + slot * HARM_FLASH_PAGE);
}

// the newest record that checks out
signed char harmonyNewestSlot(uint32_t* seq) {
  signed char best = -1;
  *seq = 0;
  for (byte slot = 0; slot < HARM_FLASH_SLOTS; ++slot) {
    uint32_t s = harmonyRecordSeq(harmonySlotRecord(slot));
    if (s > *seq) {
      *seq = s;
      best = slot;
    }
  }
  return best;
}

// at power-up: the newest saved record, if there is one
void harmonyLoadSaved() {
  harmonySaveSlot = harmonyNewestSlot(&harmonySaveSeq);
  if (harmonySaveSlot >= 0) {
    harmonySaveApply(harmonySlotRecord(harmonySaveSlot) + HARM_SAVE_HEADER);
  }
}

// what's in memory now counts as saved, until it changes
void harmonySaveSync() {
  harmonySaveBuild(harmonySaved);
  memcpy(harmonySavePending, harmonySaved, HARM_SAVE_PAYLOAD);
}

// write a record to the next page, with a sequence number above every other
void harmonyWriteRecord(const byte* payload) {
  byte r[HARM_SAVE_RECORD];
  uint32_t seq = harmonySaveSeq + 1;
  r[0] = 'H'; r[1] = 'B'; r[2] = 'S'; r[3] = '1';
  memcpy(&r[4], &seq, 4);
  r[8] = HARM_SAVE_VERSION;
  r[9] = HARM_SAVE_PAYLOAD;
  memcpy(&r[HARM_SAVE_HEADER], payload, HARM_SAVE_PAYLOAD);
  uint32_t crc = crc_byte_array(r, HARM_SAVE_HEADER + HARM_SAVE_PAYLOAD);
  memcpy(&r[HARM_SAVE_HEADER + HARM_SAVE_PAYLOAD], &crc, 4);

  byte slot = (harmonySaveSlot + 1) % HARM_FLASH_SLOTS;
  dueFlashStorage.write(HARM_FLASH_BASE + slot * HARM_FLASH_PAGE, r, HARM_SAVE_RECORD);
  if (harmonyRecordSeq(harmonySlotRecord(slot)) == seq) {
    harmonySaveSlot = slot;
    harmonySaveSeq = seq;
  }
  // even if the page didn't read back right, don't try again until something changes: retrying every
  // half second would wear the flash out
  memcpy(harmonySaved, payload, HARM_SAVE_PAYLOAD);
}

// called continuously: saves once changes have settled and nothing is being played
void harmonySaveService() {
  if (!harmonyReady || Device.serialMode) return;
  unsigned long now = millis();
  if (calcTimeDelta(now, harmonySaveCheckAt) < HARM_SAVE_CHECK_MS) return;
  harmonySaveCheckAt = now;

  byte d[HARM_SAVE_PAYLOAD];
  harmonySaveBuild(d);
  if (memcmp(d, harmonySavePending, HARM_SAVE_PAYLOAD) != 0) {
    memcpy(harmonySavePending, d, HARM_SAVE_PAYLOAD);
    harmonySaveChangedAt = now;
    return;
  }
  if (memcmp(d, harmonySaved, HARM_SAVE_PAYLOAD) == 0) return;
  if (calcTimeDelta(now, harmonySaveChangedAt) < HARM_SAVE_QUIET_MS) return;
  if (cellsTouched > 0 || harmonyPatActive) return;
  harmonyWriteRecord(d);
}

// for the flash helper: the newest saved record, false when there is none
boolean harmonySavedRecord(byte* r) {
  if (harmonySaveSlot < 0) return false;
  memcpy(r, harmonySlotRecord(harmonySaveSlot), HARM_SAVE_RECORD);
  return true;
}

// for the flash helper: a record from a backup becomes the newest saved one, and takes effect
boolean harmonyRestoreRecord(const byte* r, int32_t len) {
  if (len != HARM_SAVE_RECORD || harmonyRecordSeq(r) == 0) return false;
  harmonySaveSlot = harmonyNewestSlot(&harmonySaveSeq);
  harmonyWriteRecord(r + HARM_SAVE_HEADER);
  harmonySaveApply(r + HARM_SAVE_HEADER);
  harmonySaveSync();
  harmonyRecompute();
  return true;
}

