// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth's plugin, without a host or a window: sound at the host's rates (in tune), host parameters both
// ways, programs, state, the editor's protocol and the shared user file. Copyright (C) 2026 Jamie (TwistedGenetics).
#include "../src/CraftyProcessor.h"
#include <cstdio>
#include <cstdlib>
#include <cmath>

static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { std::printf("FAIL " __VA_ARGS__); std::printf("\n"); fails++; } } while (0)

static void pump()                                  // let the processor's timer run (host and editor news)
{
    for (int i = 0; i < 4; i++)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(40);
}

// one second at rate: the peak and the rising zero crossings of the left channel
static void play(CraftyProcessor &p, double rate, int note, float &peak, int &crossings, double &secs)
{
    p.prepareToPlay(rate, 512);
    juce::AudioBuffer<float> buf(2, 512);
    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, note, (juce::uint8)100), 0);
    peak = 0.f;
    crossings = 0;
    float prev = 0.f;
    const int blocks = (int)(rate / 512.0);
    secs = blocks * 512.0 / rate;
    for (int b = 0; b < blocks + 20; b++) {
        buf.clear();
        p.processBlock(buf, midi);
        midi.clear();
        if (b < 20)
            continue;                               // the attack
        for (int s = 0; s < 512; s++) {
            float x = buf.getSample(0, s);
            peak = std::max(peak, std::abs(x));
            if (prev < 0.f && x >= 0.f)
                crossings++;
            prev = x;
        }
    }
    midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
    buf.clear();
    p.processBlock(buf, midi);
}

int main()
{
    auto tmp = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("crafty-plugin-test");
    tmp.deleteRecursively();
    tmp.createDirectory();
    setenv("HOME", tmp.getFullPathName().toRawUTF8(), 1);
    juce::ScopedJuceInitialiser_GUI juceInit;
    {
        CraftyProcessor p;
        juce::Array<juce::RangedAudioParameter *> ps;
        for (auto *q : p.getParameters())
            ps.add(dynamic_cast<juce::RangedAudioParameter *>(q));
        CHECK(ps.size() == CR_NP, "parameters: %d", ps.size());
        juce::StringArray names;
        for (auto *q : ps)
            names.add(q->getName(64));
        names.removeDuplicates(false);
        CHECK(names.size() == CR_NP, "parameter names aren't unique (%d of %d)", names.size(), CR_NP);
        CHECK(ps[CR_CUT]->getCurrentValueAsText().isNotEmpty(), "no value text");
        std::printf("plugin: %d parameters, e.g. '%s' = %s, '%s' = %s\n", ps.size(), ps[CR_FTYPE]->getName(32).toRawUTF8(),
                    ps[CR_FTYPE]->getCurrentValueAsText().toRawUTF8(), ps[CR_DTIME]->getName(32).toRawUTF8(),
                    ps[CR_DTIME]->getCurrentValueAsText().toRawUTF8());

        // in tune at every rate: A4 = 440 Hz (INIT SAW: one rising crossing a cycle)
        for (double rate : {44100.0, 48000.0, 96000.0}) {
            float pk;
            int zc;
            double secs;
            play(p, rate, 69, pk, zc, secs);
            const double hz = zc / secs;
            std::printf("plugin: %.0f Hz: peak %.2f, A4 at %.1f Hz\n", rate, (double)pk, hz);
            CHECK(pk > 0.05f && pk < 2.f, "%.0f Hz: peak %.3f", rate, (double)pk);
            CHECK(std::abs(hz - 440.0) <= 2.5, "%.0f Hz: A4 at %.1f Hz", rate, hz);
        }

        // the host moves a parameter: the engine follows, and the editor is told (a CC)
        std::vector<std::vector<uint8_t>> toEd;
        p.toEditor = [&](const std::vector<uint8_t> &f) { toEd.push_back(f); };
        ps[CR_CUT]->setValueNotifyingHost(ps[CR_CUT]->convertTo0to1(33.f));
        {
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer m;
            p.processBlock(buf, m);
        }
        pump();
        bool sawCC = false;
        for (auto &f : toEd)
            sawCC |= f.size() == 3 && f[0] == 0xB0 && f[1] == CR_P[CR_CUT].cc && f[2] == 33;
        CHECK(sawCC, "the editor wasn't told about CUTOFF");
        p.layoutAspect = 1.62;                       // the window's size and shape go with the project
        p.editorW = 1500;
        p.editorH = 980;
        juce::MemoryBlock state;
        p.getStateInformation(state);
        CHECK(state.getSize() >= 8 + CR_PATCH_BYTES && ((const uint8_t *)state.getData())[8 + 12 + CR_CUT] == 33,
              "CUTOFF didn't reach the engine");

        // the editor moves one (a CC): the host is told
        p.fromEditor({0xB0, (uint8_t)CR_P[CR_RES].cc, 77});
        CHECK((int)std::lround(ps[CR_RES]->convertFrom0to1(ps[CR_RES]->getValue())) == 77, "the host's RESO didn't follow the editor");

        // the protocol: INFO, then save the working patch to U1-1 and read the program list
        toEd.clear();
        p.fromEditor({0xF0, 0x7D, 0x46, 0x4C, CR_ED_INFO, 0xF7});
        CHECK(toEd.size() == 1 && toEd[0].size() > 20 && toEd[0][4] == CR_ED_INFO, "INFO: no reply");
        if (!toEd.empty())
            std::printf("plugin: INFO says '%s'\n", (const char *)toEd[0].data() + 5);
        {
            cr_patch_t pt;
            std::memcpy(&pt, (const uint8_t *)state.getData() + 8, CR_PATCH_BYTES);
            std::strcpy(pt.name, "MY SAW");
            std::vector<uint8_t> f{0xF0, 0x7D, 0x46, 0x4C, CR_ED_PPUT, 64, 0};
            const auto *b = (const uint8_t *)&pt;
            for (uint32_t i = 0; i < CR_PATCH_BYTES; i += 7) {
                uint32_t m = 0, k = std::min(7u, CR_PATCH_BYTES - i);
                for (uint32_t j = 0; j < k; j++)
                    m |= (uint32_t)(b[i + j] >> 7) << j;
                f.push_back((uint8_t)m);
                for (uint32_t j = 0; j < k; j++)
                    f.push_back(b[i + j] & 0x7F);
            }
            f.push_back(0xF7);
            toEd.clear();
            p.fromEditor(f);
            CHECK(toEd.size() == 1 && toEd[0][7] == 0, "PPUT U1-1 failed");
        }
        CHECK(p.getProgramName(64) == "U1-1 MY SAW", "program 64: '%s'", p.getProgramName(64).toRawUTF8());
        CHECK(p.getProgramName(3).startsWith("1-4 "), "program 3: '%s'", p.getProgramName(3).toRawUTF8());
        CHECK(p.getProgramName(100).endsWith("(empty)"), "program 100: '%s'", p.getProgramName(100).toRawUTF8());

        // a program from the host: the parameters follow
        p.setCurrentProgram(11);
        CHECK(p.getCurrentProgram() == 11, "program 11 not current");
        {
            CraftyProcessor q;                      // another instance shares the user area
            CHECK(q.getProgramName(64) == "U1-1 MY SAW", "a second instance doesn't see U1-1");
            q.setStateInformation(state.getData(), (int)state.getSize());
            pump();
            CHECK(std::abs(q.layoutAspect - 1.62) < 0.002 && q.editorW == 1500 && q.editorH == 980,
                  "the window didn't come back: %.3f %dx%d", q.layoutAspect, q.editorW, q.editorH);
            auto *qc = dynamic_cast<juce::RangedAudioParameter *>(q.getParameters()[CR_CUT]);
            CHECK((int)std::lround(qc->convertFrom0to1(qc->getValue())) == 33,
                  "state didn't restore CUTOFF");
        }
    }
    pump();
    auto user = CraftyUserArea::file();
    CHECK(user.existsAsFile() && user.getSize() == CR_USER_BYTES, "the user file wasn't written: %s",
          user.getFullPathName().toRawUTF8());
    {
        CraftyProcessor p;                          // a new session reads it back
        CHECK(p.getProgramName(64) == "U1-1 MY SAW", "U1-1 not read back from the file");
    }
    tmp.deleteRecursively();
    if (fails)
        return 1;
    std::printf("ok   plugin\n");
    return 0;
}
