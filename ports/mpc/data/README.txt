Crafty Synth on the MPC: this folder keeps your user presets and wavetables.

Put "Crafty Synth.user" here to use the user presets (U1-1 ... U16-8) and user wavetable banks (USER 1-32) you made
in the Crafty Synth plugin on your computer. On a Mac it is in
  ~/Library/Application Support/Twisted Genetics/Crafty Synth/Crafty Synth.user
Copy it here again whenever you want the MPC to have your latest ones. When you update the plugin, keep this folder
(or copy the file in again).

Your wavetables (0.15): put WAVs (Serum and Vital tables, or single cycles; sub-folders are fine) in the Wavetables
folder here. TABLE FILE (on the OSC page, under each oscillator) steps through them by name and plays the one
shown in place of its bank; 0 is the bank. Each file also gets a number for good in TableIndex.txt, the same file the Twisted Tracker's CRAFTY and the Mac plugin keep, so
copy Wavetables and TableIndex.txt together between them and the numbers match. RANDOM (page PRESET) picks a preset
and varies it; RND TABLES = KEEP leaves the banks and files as they are.
