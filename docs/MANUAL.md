# Crafty Synth manual (0.4)

Crafty Synth makes the M-VAVE FM-1 a monophonic wavetable synth after the Modal CRAFTsynth 2.0. This page covers the
FM-1's panel. For what each part of the engine does, the CRAFTsynth 2.0 manual's "Synthesis engine" chapter applies
almost word for word; the differences are listed at the end.

## The screen

- **Top band:** the preset (`3-1` is factory bank 3, slot 1; `U12-8` is user bank 12, slot 8) and its name; `*` means you've changed it since loading. On the
  right is the page you're on.
- **Second line:** the oscillator modifier in use; on the right, `ARP` / `SEQ`, `LATCH` and the keys' octave.
- **Middle:** a picture of the page that moves as you turn its knobs: both wavetables in 3D on the OSC pages (WT POS
  slides through them), the envelope, the LFO shape, the filter's response, the sub's wave, the distortion's curve and
  the delay's echoes, the reverb's tail, the chorus and phaser sweeps, the crusher's steps; the pages without a
  picture (OUTPUT, ARP, MIDI, KEYS) list their four values with bars. The knob you turn lights up below (0.16: no big
  number over the picture any more).
- **Bottom:** the page's four values, one over each of K1–K4.

## Knobs

| | |
|---|---|
| **PRESET** | Steps through the 64 factory presets (`1-1` … `8-8`), then the 128 user slots (`U1-1` … `U16-8`); each one loads as you turn. On the SAVE page it picks the user slot instead. |
| **SELECT** | Steps through the pages. |
| **ALGO** | The oscillator modifier (one of 16), from any page. |
| **K1–K4** | The four values on the page. A list (bank, modifier, mode) moves one choice per click. |

## Buttons and pages

| Button | Pages (press again to cycle) | K1 · K2 · K3 · K4 |
|---|---|---|
| **HOME** | OSC | WAVE 1 · WAVE 2 · MIX · OSC MOD (the modifier's amount) |
| | WAVES | BANK 1 · BANK 2 · MOD MODE · SPREAD |
| | OSC B | B MODE · B MOD · WAVE 2 · TUNE |
| | TUNE | TUNE (Wave 2, ±48 semitones) · FINE (Wave 2, ±100 cents) · OCTAVE · GLIDE |
| | SUB | SUB LVL · SUB OCT (-1 / -2) · SUB WAVE (SINE / TRI / SQUARE) · MIX |
| **EDIT** | FILTER | CUTOFF · RESO · MORPH (LP → BP → HP, or the type's own sweep) · TYPE |
| **ENV** | FILTER EG → AMP EG → MOD EG | ATTACK · DECAY · SUSTAIN · RELEASE |
| **LFO** | LFO 1 → LFO 2 | RATE · SHAPE · MODE (retrigger / free / single) · SYNC |
| **SEL** | MATRIX | SOURCE · DEST · DEPTH |
| **FX** | FX | DISTORT · DELAY (dry → wet) · TIME · FEEDBACK |
| | OUTPUT | DELAY SYNC · VOLUME · AMP EG (amount, bipolar) · FILT EG (bipolar) |
| | REVERB | MODE · SIZE · DECAY · REVERB (dry → wet) |
| | REVERB 2 | WARP · DENSITY · MOD · REVERB |
| | MOD FX | CHORUS · CH RATE · PHASER · PH RATE |
| | CRUSH | ERA · CRUSH · OTT · DISTORT |
| **ARP** | turns the arp on or off, and shows ARP | ARP · TEMPO · RATE · STEPS |
| **SEQ** | SEQ (the pattern) | STEPS · TEMPO · RATE · ARP |
| **GLO** | MIDI → KEYS → SETUP | MIDI CH · MIDI IN · CLOCK · BEND / KEYS · SCALE · ROOT · VELO IN / LOCAL · BREATH · ARP OUT · THEME |
| **SAVE** | SAVE | USER SLOT · TABLES (for RANDOM: NEW / KEEP) · LETTER · CHAR |
| (SELECT) | RANDOM (after SETUP) | DEPTH · TABLES (NEW / KEEP) · USER WT (ON / OFF) |
| **REC** | | LATCH on / off (on the RANDOM and SAVE pages: RANDOM) |
| **PLAY + REC** | | RANDOM, on any page (hold PLAY, press REC) |
| **PLAY** | | all notes off (on the SAVE page: INIT, a plain saw patch) |
| **OCT− / OCT+** | | the keys' octave, −3 … +3 |

## Held-button shortcuts (as on the CRAFTsynth)

- **Hold LFO, turn a knob:** modulates that value with LFO 1 (LFO 2 if you're on its page). The turn sets the depth;
  the screen shows e.g. `LFO 1 TO CUTOFF +12`. Release LFO and you stay on your page.
- **Hold SEL, turn a knob:** the same with the mod envelope.
- **Hold ENV, turn a knob** on an envelope page: all three envelopes change together.
- **Hold SEQ, press keys:** enters pattern steps. The first key is the pattern's root; each later key is stored as its
  distance from the root. **SEQ + REC** adds a rest; **SEQ + PLAY** clears the pattern.
- **OCT− + OCT+ held 5 seconds:** the FM-1's update mode, the same in every firmware.

## Playing

The 27 keys play F3 to G5, shifted by OCT. With **GLO → KEYS → SCALE**, the keys walk up the chosen scale from its
root instead (16 scales, Phrygian and Phrygian dominant among them), so you can't hit a wrong note.

Notes are **last-note priority**. **GLIDE** above the middle glides on every note; below the middle it glides only
when you play legato. Any glide setting other than OFF also makes the envelopes legato, as on the CRAFTsynth.

**LATCH** (REC) keeps notes sounding after you let go. The next chord you play replaces the latched one.

With **ARP on**, held notes play upward one per step. Give it a pattern (SEQ) and it plays the pattern from the last
note you hold: a note-triggered sequence. RATE runs from 1/4 to 1/32 (triplets too). It follows the patch's TEMPO, or
MIDI clock when one arrives (GLO → CLOCK: AUTO / INT / MIDI).

## The matrix

Each of the 8 sources has one slot: a destination and a bipolar depth. **Every patch value is a destination** (74 in
all; only the matrix's own destination choices aren't): besides the CRAFTsynth's 43, the wavetable banks, modifier and
B modes, tune, fine and octave, the LFOs' modes and sync, delay sync, the other five sources' depths, the arp-seq's
on/off, tempo and rate, the filter type, every reverb setting, the chorus and phaser rates and the crush era. A
stepped value (a bank, a type, a mode) steps through its choices as the source moves. At full depth an LFO sweeps a
value set at its middle across its whole range; the other sources sweep a value set at 0 across its whole range.
LFO 1, LFO 2 and the mod EG can't modulate their own settings, and no source its own depth. Mod wheel (CC 1), expression (CC 11), aftertouch and velocity come from MIDI
in; breath (CC 2) can stand in for any of the three (GLO → SETUP → BREATH).

## OSC B

OSC B's own modifier works alongside the 16 on OSC A. **B MODE**: OFF, FM (A's output bends B's pitch), PM (A pushes
B's phase), SYNC (A restarts B; B MOD shifts B up for the classic sweep), RING (B times A), FOLD (B folds over itself).
**B MOD** is the amount; it is also a matrix destination, so an LFO or the mod EG can sweep it.

## Sub oscillator

A sine, triangle or square one or two octaves under OSC A's note (its middle note: the spread doesn't move it), mixed
in before the filter, so the filter, drive and effects treat it as part of the sound. **HOME** to the fifth page,
**SUB**: SUB LVL (0 is off), SUB OCT, SUB WAVE; the screen draws the wave as tall as its level. All three are matrix
destinations. The basses (WARM BASS, SUB BASS, REESE, ROLLER, DREAD, DONK, WOBBLE, MIN REESE, NEURO, DUB SEQ) use it.

## Filter types

**TYPE** picks one of 12 filters; **MORPH** still sweeps each one:

- **SVF, MS-20, STEINER**: low pass → band pass → high pass (MS-20 screams, Steiner is rough).
- **LADDER, TB-303**: four mixes of the ladder's stages, Xpander-style; the 303 is the squelchy diode version.
- **SEM**: low pass → notch → high pass. **POLIVOKS**: gritty low pass → band pass.
- **FORMANT**: through the vowels; CUTOFF shifts them, RESO narrows them.
- **COMB +/-**: negative to positive feedback, tuned by CUTOFF.
- **PHASER**: 2 to 12 notches. **DUAL PEAK**: two resonant peaks pulling apart.
- **DECIMATOR**: a filter, then bit reduction from 16 bits down to 3.

## Effects

In order: filter → distortion → EraCrush → amp → phaser → delay → chorus → reverb → OTT.

- **REVERB** (Horizon): six modes. ROOM and HALL are plain; HALO shimmers; SWELL fades in; ABYSS and DUB run nearly
  endless. SIZE, DECAY (0.3–30 s), WARP (the tail's darkness and pitch drift), DENSITY (from echoes to smooth wash), MOD.
- **CHORUS** and **PHASER**, each with an amount and a rate.
- **ERA** picks a sampler (SP-1200, MPC60, S950, S1000, AMIGA) and **CRUSH** how hard; OFF bypasses it.
- **OTT**: three-band upward and downward compression, as the plugin.

Each effect costs nothing when its amount is 0. GLO → SETUP shows the CPU load; if it reaches 90%, ease off the
heaviest (reverb, then OTT).

## Presets and saving

There are 64 **factory presets** (`1-1` … `8-8`), which stay as they are, and 128 **user slots** (`U1-1` … `U16-8`).
**SAVE** opens the save page on the user slot you're on, or the first empty one if you came from a factory preset. Pick
the slot (PRESET or K1), and rename with K3 (which letter) and K4 (the character). Press **SAVE** again to write it.
Any other button cancels. An empty user slot loads as INIT; the editor can empty a slot. Presets saved with 0.1–0.3
are moved to U1-1 onward the first time 0.4 starts. Program change 0–63 loads the factory presets, 64–127 U1-1 … U8-8.

**RANDOM**: hold **PLAY** and press **REC** on any page, or press **REC** on the RANDOM page (SELECT, after SETUP)
or the SAVE page. It loads a random preset (never an empty user slot), varies it and names it **RND** and the preset's
name, so you can see it's new. The RANDOM page's knobs set how:
- **DEPTH**: low stays close to the preset (knob values pushed up to a quarter of their range, a third of the switches
  changed); higher pushes more of them anywhere in their range and changes more switches; at 100% nearly everything
  moves. Volume, octave, glide, the amp envelope's amount, the arp-seq, the delay's time and feedback and the reverb's
  mode, size and decay always stay as the preset has them, the amp sustain stays high enough to hear, and the matrix
  never gets a destination that would hold the sound open or move a bank.
- **TABLES**: NEW gives each oscillator a new bank, KEEP keeps yours.
- **USER WT**: ON lets a new bank be one of your user banks half the time (when you have some), OFF factory only.

The page shows which preset the last one came from. Press REC (or PLAY + REC) again for another; SAVE stores the one you
like.

## User wavetables

Banks 10 to 41 (USER 1 … USER 32) are yours. In the editor, open a WAV in **User wavetables** (Serum and Vital tables,
or any single cycle) and choose which five of its frames become the five waves. You hear the result straight away;
**Send to FM-1** stores it. Each user bank is shared by every patch that uses it. Until you fill one, it plays bank 1. 0.1–0.3's two user banks become USER 1 and USER 2.
The importer is the engine's own C (0.13), the same one the plugin, the MPC and the Twisted Tracker's CRAFTY use, so a
WAV makes the same waves on all of them. Frames longer than 4096 samples aren't read.

## MIDI

The CC numbers are the CRAFTsynth 2.0's (its manual's chart): cutoff 34, resonance 35, morph 33, wave 1 16, wave 2 17,
mix 18, osc mod 19, spread 20, mod mode 41, and so on. Program change loads a preset; pitch bend is ±BEND semitones;
the sustain pedal (CC 64) holds notes. Turning a knob on the FM-1 sends its CC, so a DAW can record your moves. The
keys send notes on MIDI CH. With ARP OUT set to ARP, the arp's notes go to MIDI out instead of the keys'. LOCAL OFF
keeps the keys off the internal synth.

## What differs from a CRAFTsynth 2.0

- The wavetables are Crafty Synth's own (same bank layout and character); 32 user banks are added.
- Added beyond the CRAFTsynth: OSC B's modifier, 12 filter types, reverb, chorus, phaser, EraCrush, OTT, and 128 user
  preset slots.
- The delay goes to about 0.74 s (the CRAFTsynth's is longer).
- The 8 touch pads and their scale system become the FM-1's 27 keys (chromatic, or scale mode).
- Mod destination 24 ("MEG Amount" in the CRAFTsynth's list) is VOLUME here: the mod EG's depth has its own
  destination (MEG DEP), so there is no duplicate.
- The arp-seq gains a RATE (the CRAFTsynth's is fixed to the clock). TEMPO is stored in each patch.
- No polychaining and no analogue sync jacks: the FM-1 has neither.
