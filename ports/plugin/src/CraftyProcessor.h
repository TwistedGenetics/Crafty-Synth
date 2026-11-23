// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth as an AU / VST3 plugin: the FM-1's engine (src/host/cr_inst.c) behind a JUCE processor. Every patch
// value is a host parameter; the editor window is the web editor (web/editor), talking to this processor the way it
// talks to an FM-1 (src/host/cr_proto.c). User presets and wavetables live in one file per user, shared by every
// instance: the FM-1's user area, byte for byte. Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <vector>

extern "C" {
#include "cr_proto.h"
#include "cr_text.h"
#include "cr_tables.h"
#include "cr_wav.h"
}

class CraftyProcessor;

// the user area (src/core/cr_user.h) in RAM, loaded from and saved to the user's file; one per process, shared by
// every Crafty Synth in it (message thread)
class CraftyUserArea : private juce::Timer {
public:
    CraftyUserArea();
    ~CraftyUserArea() override;
    cr_store_t &store() { return *st; }
    static juce::File file();
    void changed();                                 // after a command wrote to it: saved soon, every instance told
    void add(CraftyProcessor *p) { procs.push_back(p); }
    void remove(CraftyProcessor *p) { procs.erase(std::remove(procs.begin(), procs.end(), p), procs.end()); }
private:
    void timerCallback() override;
    void save();
    std::unique_ptr<cr_store_t> st;
    std::vector<CraftyProcessor *> procs;
};

// 0.13: wavetable files, as the Twisted Tracker's /Crafty/Wavetables: every WAV under the Wavetables folder next to
// the user file gets a number for good in TableIndex.txt (src/host/cr_tables.c). One per process (message thread).
class CraftyTableFiles {
public:
    CraftyTableFiles();
    static juce::File folder();                     // .../Crafty Synth/Wavetables
    void scan(bool force);                          // walk the folder again (at most every 2 s unless forced)
    const cr_tables_t &tables() const { return *t; }
    juce::var list();                               // for the editor: [{p: path}, ...] in name order
private:
    std::unique_ptr<cr_tables_t> t;
    juce::uint32 lastScan = 0;
    bool loaded = false;
};

class CraftyProcessor : public juce::AudioProcessor, private juce::Timer {
public:
    CraftyProcessor();
    ~CraftyProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout &layouts) const override;
    void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor *createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override { return "Crafty Synth"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return CR_NPRESET; }
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String &) override {}

    void getStateInformation(juce::MemoryBlock &destData) override;
    void setStateInformation(const void *data, int sizeInBytes) override;

    // the editor's MIDI: CCs, notes and protocol frames, as it would send them to an FM-1 (message thread). Replies
    // and news go back through `toEditor`, set by the editor while it is open.
    void fromEditor(const std::vector<uint8_t> &frame);
    std::function<void(const std::vector<uint8_t> &)> toEditor;

    void userAreaChanged();                         // another instance changed the shared user area
    // 0.13: a wavetable file (a path under CraftyTableFiles::folder(), "" = the bank) in place of wave k's bank;
    // false when it can't be read (the bank plays). The editor's "crafty-tables" events (message thread).
    bool setTableFile(int k, const juce::String &path);
    void tablesFromEditor(const juce::var &v);
    std::function<void(const juce::var &)> toEditorTables;
    juce::String tableFile(int k) const { return tablePath[(size_t)(k & 1)]; }
    // the window: its size and the layout's shape (width / height), kept in the project
    int editorW = 1240, editorH = 860;
    double layoutAspect = 1.6;
    static constexpr int numParams = CR_NP;

private:
    void timerCallback() override;
    void syncParamsFromEngine();                   // the engine's patch to the host (message thread)
    void sendEvent(uint32_t kind);
    void renderEngineBlock();

    juce::SharedResourcePointer<CraftyUserArea> userArea;
    juce::SharedResourcePointer<CraftyTableFiles> tableFiles;
    std::array<juce::String, 2> tablePath;          // the wavetable files the waves play ("" = their banks)
    std::array<std::array<int16_t, 5 * CR_WT_N>, 2> tableWaves{};   // their waves (the instance builds from them)
    void sendTables();
    std::unique_ptr<cr_inst_t> inst;
    juce::CriticalSection lock;                     // the instance: the audio thread and the editor's commands
    std::array<juce::AudioParameterInt *, CR_NP> params{};
    std::array<std::atomic<int>, CR_NP> lastHost;   // each parameter as last seen from (or told to) the host
    std::array<std::atomic<uint32_t>, 4> toUi{};    // parameters the host changed, for the editor's knobs
    std::atomic<bool> patchNews{false};             // a new patch from inside the audio thread (program change)
    std::atomic<bool> engineNews{false};            // parameters changed by MIDI CC: tell the host

    // 44.1 kHz engine -> the host's rate (cubic Hermite); a ring of engine frames
    double hostRate = 44100.0, step = 1.0, phase = 0.0;
    float eng[2 * CR_INST_FRAMES]{};
    int engPos = CR_INST_FRAMES;
    float hist[4][2]{};                             // the last four engine frames (x[-1] .. x[2])
    double blockPpq = -1.0, blockBpm = 0.0;
    bool blockPlaying = false;
    int hostSampleInBlock = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CraftyProcessor)
};
