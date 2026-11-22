# Crafty Synth: the editor protocol

**Parameters** need no protocol: each one is a MIDI CC (the CRAFTsynth 2.0's chart, `src/core/cr_patch.c`), on the
FM-1's MIDI channel, both ways.

**Everything else** goes in Felucca's editor frames, `F0 7D 46 4C cmd args… F7`, with 7-bit bytes. Binary data is
packed 7 to 8: a byte of high bits, then up to 7 low-7-bit bytes. The FM-1 answers each command with a frame carrying
the same `cmd`. Commands 1 (INFO) and 25 (PING) are Felucca's, which the flasher uses.

| cmd | name | args | reply |
|---|---|---|---|
| 40 | INFO | – | name (ASCII, 0-terminated), protocol (1), presets (64), current preset, dirty, user bank 1 filled, user bank 2 filled |
| 41 | PATCH GET | slot (0–63, or 127 the working patch) | slot, saved-or-dirty flag, the 96-byte patch packed |
| 42 | PATCH PUT | slot (or 127), the patch packed | slot, 0 ok / 1 flash error. 127: it plays now. A slot: written to flash |
| 43 | SELECT | slot | slot (the preset is loaded) |
| 44 | GLOBALS GET | – | the 16-byte globals packed |
| 45 | GLOBALS PUT | the globals packed | 0 |
| 46 | WAVE PUT | user bank (0/1), chunk n, ≤256 bytes packed | bank, chunk. Into RAM at byte n × 256 of the 2,576-byte bank record |
| 47 | WAVE COMMIT | user bank | bank, 0 ok / 1 flash error / 2 the record is broken (RAM reloaded from flash) |
| 48 | EVENT | (sent by the FM-1, never asked for) | kind (1 loaded, 2 saved), current preset, dirty |
| 49 | WAVE GET | user bank, chunk | bank, chunk, ≤256 bytes packed |
| 50 | PRESET RESET | slot | slot, 0 ok: the slot is its factory preset again |

## The patch (96 bytes, `cr_patch_t`)

| bytes | |
|---|---|
| 0–11 | name, ASCII, 0-terminated |
| 12–75 | the parameters, 0–127, in `src/core/cr_patch.h`'s order (62 in use) |
| 76 | pattern length (0–16) |
| 77 | magic 0xC5 |
| 78 | version 1 |
| 79 | the pattern's root note (where it was recorded) |
| 80–95 | the steps: 0 a rest, otherwise 64 + semitones from the root |

## A user bank (2,576 bytes)

Magic 0xC7, version 1, a name of 13 characters plus a 0, then 5 waves × 256 int16 samples, little-endian, peaking
near 26,000. The firmware band-limits them itself.
