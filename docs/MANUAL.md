# Crafty Synth manual (0.3)

Crafty Synth makes the M-VAVE FM-1 a monophonic wavetable synth after the Modal CRAFTsynth 2.0. This page covers the
FM-1's panel. For what each part of the engine does, the CRAFTsynth 2.0 manual's "Synthesis engine" chapter applies
almost word for word; the differences are listed at the end.

## The screen

- **Top band:** the preset (`3-1` is bank 3, slot 1) and its name; `*` means you've changed it since loading. On the
  right is the page you're on.
- **Second line:** the oscillator modifier in use; on the right, `ARP` / `SEQ`, `LATCH` and the keys' octave.
- **Middle:** a picture of the page: both waves on the OSC pages, the envelope, the LFO shape, or the filter's
  response. Turn a knob and its name and value show here in large type for a moment.
- **Bottom:** the page's four values, one over each of K1–K4.

## Knobs

| | |
|---|---|
| **PRESET** | Steps through the 64 presets (`1-1` … `8-8`); each one loads as you turn. On the SAVE page it picks the slot instead. |
| **SELECT** | Steps through the pages. |
| **ALGO** | The oscillator modifier (one of 16), from any page. |
| **K1–K4** | The four values on the page. A list (bank, modifier, mode) moves one choice per click. |

## Buttons and pages

| Button | Pages (press again to cycle) | K1 · K2 · K3 · K4 |
|---|---|---|
| **HOME** | OSC | WAVE 1 · WAVE 2 · MIX · OSC MOD (the modifier's amount) |
| | WAVES | BANK 1 · BANK 2 · MOD MODE · SPREAD |
| | TUNE | TUNE (Wave 2, ±48 semitones) · FINE (Wave 2, ±100 cents) · OCTAVE · GLIDE |
| **EDIT** | FILTER | CUTOFF · RESO · MORPH (LP → BP → HP) · FILT EG (bipolar) |
| **ENV** | FILTER EG → AMP EG → MOD EG | ATTACK · DECAY · SUSTAIN · RELEASE |
| **LFO** | LFO 1 → LFO 2 | RATE · SHAPE · MODE (retrigger / free / single) · SYNC |
| **SEL** | MATRIX | SOURCE · DEST · DEPTH |
| **FX** | FX | DISTORT · DELAY (dry → wet) · TIME · FEEDBACK |
| | OUTPUT | DELAY SYNC · VOLUME · AMP EG (amount, bipolar) · GLIDE |
| **ARP** | turns the arp on or off, and shows ARP | ARP · TEMPO · RATE · STEPS |
| **SEQ** | SEQ (the pattern) | STEPS · TEMPO · RATE · ARP |
| **GLO** | MIDI → KEYS → SETUP | MIDI CH · MIDI IN · CLOCK · BEND / KEYS · SCALE · ROOT · VELO IN / LOCAL · BREATH · ARP OUT · THEME |
| **SAVE** | SAVE | SLOT · – · LETTER · CHAR |
| **REC** | | LATCH on / off |
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

Each of the 8 sources has one slot: a destination and a bipolar depth. At full depth an LFO sweeps a value set at its
middle across its whole range; the other sources sweep a value set at 0 across its whole range. LFO 1, LFO 2 and the
mod EG can't modulate their own settings. Mod wheel (CC 1), expression (CC 11), aftertouch and velocity come from MIDI
in; breath (CC 2) can stand in for any of the three (GLO → SETUP → BREATH).

## Presets and saving

There are 64 slots. Until you save over a slot it holds its factory preset. **SAVE** opens the save page: pick the slot
(PRESET or K1), and rename with K3 (which letter) and K4 (the character). Press **SAVE** again to write it. Any other
button cancels. From the editor you can put a slot back to its factory preset.

## User wavetables

Banks 10 and 11 (USER 1 and USER 2) are yours. In the editor, open a WAV in **User wavetables** (Serum and Vital tables,
or any single cycle) and choose which five of its frames become the five waves. You hear the result straight away;
**Send to FM-1** stores it. Each user bank is shared by every patch that uses it. Until you fill one, it plays bank 1.

## MIDI

The CC numbers are the CRAFTsynth 2.0's (its manual's chart): cutoff 34, resonance 35, morph 33, wave 1 16, wave 2 17,
mix 18, osc mod 19, spread 20, mod mode 41, and so on. Program change loads a preset; pitch bend is ±BEND semitones;
the sustain pedal (CC 64) holds notes. Turning a knob on the FM-1 sends its CC, so a DAW can record your moves. The
keys send notes on MIDI CH. With ARP OUT set to ARP, the arp's notes go to MIDI out instead of the keys'. LOCAL OFF
keeps the keys off the internal synth.

## What differs from a CRAFTsynth 2.0

- The wavetables are Crafty Synth's own (same bank layout and character); two user banks are added.
- The 8 touch pads and their scale system become the FM-1's 27 keys (chromatic, or scale mode).
- Mod destination 24 ("MEG Amount" in the CRAFTsynth's list) is VOLUME here: the mod EG's depth has its own
  destination (MEG DEP), so there is no duplicate.
- The arp-seq gains a RATE (the CRAFTsynth's is fixed to the clock). TEMPO is stored in each patch.
- No polychaining and no analogue sync jacks: the FM-1 has neither.
