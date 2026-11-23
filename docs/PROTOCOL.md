# Crafty Synth: the editor protocol

**Parameters** need no protocol: each one is a MIDI CC (the CRAFTsynth 2.0's chart, `src/core/cr_patch.c`), on the
FM-1's MIDI channel, both ways.

**Everything else** goes in Felucca's editor frames, `F0 7D 46 4C cmd args… F7`, with 7-bit bytes. Binary data is
packed 7 to 8: a byte of high bits, then up to 7 low-7-bit bytes. The FM-1 answers each command with a frame carrying
the same `cmd`. Commands 1 (INFO) and 25 (PING) are Felucca's, which the flasher uses.

Protocol 2 (firmware 0.4). A **slot** is two bytes, low 7 bits then high 7 bits: 0–63 the factory presets, 64–191
the user slots (U1-1 … U16-8), 16383 (0x3FFF) the working patch.

| cmd | name | args | reply |
|---|---|---|---|
| 40 | INFO | – | name (ASCII, 0-terminated), protocol (2), factory presets (64), user slots (2 bytes: 128), current slot (2 bytes), dirty, user banks (32), then 32 bytes: each bank filled or not |
| 41 | PATCH GET | slot | slot, saved flag (a user slot: holds a patch; the working patch: dirty), the 128-byte patch packed. An empty user slot gives INIT |
| 42 | PATCH PUT | slot, the patch packed | slot, 0 ok / 1 flash error / 2 a factory slot (refused). The working patch plays now; a user slot is written to flash |
| 43 | SELECT | slot | slot (the preset is loaded) |
| 44 | GLOBALS GET | – | the 16-byte globals packed |
| 45 | GLOBALS PUT | the globals packed | 0 |
| 46 | WAVE PUT | user bank (0–31), chunk n, ≤256 bytes packed | bank, chunk. Into RAM at byte n × 256 of the 2,576-byte bank record |
| 47 | WAVE COMMIT | user bank | bank, 0 ok / 1 flash error / 2 the record is broken |
| 48 | EVENT | (sent by the FM-1, never asked for) | kind (1 loaded, 2 saved), current slot (2 bytes), dirty |
| 49 | WAVE GET | user bank, chunk | bank, chunk, ≤256 bytes packed (nothing if the bank is empty) |
| 50 | PRESET RESET | slot (a user slot) | slot, 0 ok: the slot is empty again |

## The patch (128 bytes, `cr_patch_t`)

| bytes | |
|---|---|
| 0–11 | name, ASCII, 0-terminated |
| 12–107 | the parameters, 0–127, in `src/core/cr_patch.h`'s order (82 in use) |
| 108 | pattern length (0–16) |
| 109 | magic 0xC5 |
| 110 | version 2 |
| 111 | the pattern's root note (where it was recorded) |
| 112–127 | the steps: 0 a rest, otherwise 64 + semitones from the root |

Version 1 patches (96 bytes: parameters at 12–75, then the same tail at 76–95) are upgraded by `cr_patch_from_v1()`:
the new parameters take their defaults.

## Flash

The user area is X0X's sample region, 0xA0000–0xDBFFF (240 KB): the 128 user presets at offset 0 (128 B each, 16 KB),
then the 32 user banks at 0x4000, one 4 KB sector each.

## A user bank (2,576 bytes)

Magic 0xC7, version 1, a name of 13 characters plus a 0, then 5 waves × 256 int16 samples, little-endian, peaking
near 26,000. The firmware band-limits them itself.
