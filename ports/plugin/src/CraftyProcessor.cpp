// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth as an AU / VST3 plugin (CraftyProcessor.h). Copyright (C) 2026 Jamie (TwistedGenetics).
#include "CraftyProcessor.h"
#include <cstring>
#include "cr_keys.h"

static_assert(sizeof CR_KEY / sizeof CR_KEY[0] == CR_NP, "cr_keys.h is stale: tools/gen_presets.py --keys");

#ifndef CRAFTY_VERSION
#define CRAFTY_VERSION "DEV"
#endif

// ------------------------------------------------------------------------------------------- the user area ---
CraftyUserArea::CraftyUserArea() : st(std::make_unique<cr_store_t>())
{
    cr_store_init(st.get(), "Crafty Synth plugin " CRAFTY_VERSION);
    juce::MemoryBlock mb;
    if (file().existsAsFile() && file().loadFileAsData(mb) && mb.getSize() == CR_USER_BYTES)
        std::memcpy(st->user, mb.getData(), CR_USER_BYTES);
}
CraftyUserArea::~CraftyUserArea()
{
    stopTimer();
    if (st->user_dirty)
        save();
}
juce::File CraftyUserArea::file()
{
#if JUCE_MAC
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory).getChildFile("Application Support");
#else
    auto dir = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#endif
    return dir.getChildFile("Twisted Genetics").getChildFile("Crafty Synth").getChildFile("Crafty Synth.user");
}
void CraftyUserArea::changed()
{
    for (auto *p : procs)
        p->userAreaChanged();
    startTimer(500);                                // a burst of commands (a wavetable's chunks) is one save
}
void CraftyUserArea::timerCallback()
{
    stopTimer();
    save();
}
void CraftyUserArea::save()
{
    if (!st->user_dirty)
        return;
    auto f = file();
    f.getParentDirectory().createDirectory();
    auto tmp = f.getSiblingFile("Crafty Synth.user.new");
    if (tmp.replaceWithData(st->user, CR_USER_BYTES) && tmp.moveFileTo(f))
        st->user_dirty = 0;
}

// --------------------------------------------------------------------------------------------- parameters ---
static juce::String valueText(int id, int v)
{
    cr_patch_t p;
    cr_patch_init(&p, "");
    p.p[id] = (uint8_t)juce::jlimit(0, 127, v);
    char b[48];
    cr_param_text(&p, (uint32_t)id, b, 1, nullptr, nullptr);
    return juce::String(b);
}

static juce::String paramName(int id)
{
    // the screen's label, with the page it sits on where the label alone is ambiguous (ATTACK x3, RATE x3 ...)
    static const char *const page[CR_NP] = {
        "", "", "OSC", "", "", "", "", "OSC", "OSC B", "OSC B", "", "", "", "FILTER", "FILTER", "", "FILT EG",
        "FILT EG", "FILT EG", "FILT EG", "AMP EG", "AMP EG", "AMP EG", "AMP EG", "", "MOD EG", "MOD EG", "MOD EG",
        "MOD EG", "LFO 1", "LFO 1", "LFO 1", "LFO 1", "LFO 2", "LFO 2", "LFO 2", "LFO 2", "", "", "DELAY", "DELAY",
        "DELAY", "", "LFO 1", "LFO 2", "MOD EG", "NOTE", "VELO", "MOD WHL", "EXPR", "AFTTCH", "LFO 1", "LFO 2",
        "MOD EG", "NOTE", "VELO", "MOD WHL", "EXPR", "AFTTCH", "", "", "ARP", "", "", "FILTER", "REVERB", "REVERB",
        "REVERB", "", "REVERB", "REVERB", "REVERB", "", "", "", "", "", "", ""};
    juce::String n(CR_P[id].name);
    juce::String pg(id < CR_NP ? page[id] : "");
    if (pg.isNotEmpty() && !n.startsWithIgnoreCase(pg))
        n = pg + " " + n;
    return n;
}
static_assert(CR_NP == 79, "paramName's pages: one per parameter");

CraftyProcessor::CraftyProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      inst(std::make_unique<cr_inst_t>())
{
    cr_inst_init(inst.get());
    cr_inst_set_user(inst.get(), userArea->store().user);
    (void)cr_inst_take_patch_changed(inst.get());   // the first patch: the parameters below start from it
    userArea->add(this);
    for (int i = 0; i < CR_NP; i++) {
        const juce::String key(CR_KEY[i]);       // CR_W1POS -> "W1POS": stable across versions (append only)
        auto attrs = juce::AudioParameterIntAttributes().withStringFromValueFunction(
            [i](int v, int) { return valueText(i, v); });
        params[(size_t)i] = new juce::AudioParameterInt(juce::ParameterID{key, 1}, paramName(i), 0, 127,
                                                        inst->p.p[i], attrs);
        addParameter(params[(size_t)i]);
        lastHost[(size_t)i] = inst->p.p[i];
    }
    startTimerHz(30);
}

CraftyProcessor::~CraftyProcessor()
{
    stopTimer();
    userArea->remove(this);
}

bool CraftyProcessor::isBusesLayoutSupported(const BusesLayout &layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void CraftyProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(lock);
    hostRate = sampleRate > 0 ? sampleRate : 44100.0;
    step = (double)CR_INST_RATE / hostRate;
    phase = 0.0;
    engPos = CR_INST_FRAMES;
    std::memset(hist, 0, sizeof hist);
}

// --------------------------------------------------------------------------------------------- the audio ---
void CraftyProcessor::renderEngineBlock()
{
    double ppq = blockPpq >= 0.0 ? blockPpq + (double)hostSampleInBlock / hostRate * blockBpm / 60.0 : -1.0;
    cr_inst_transport(inst.get(), ppq, (float)blockBpm, blockPlaying ? 1 : 0);
    cr_inst_render(inst.get(), eng, CR_INST_FRAMES);
    engPos = 0;
}

void CraftyProcessor::processBlock(juce::AudioBuffer<float> &buffer, juce::MidiBuffer &midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock sl(lock);
    const int n = buffer.getNumSamples();
    float *outL = buffer.getWritePointer(0);
    float *outR = buffer.getNumChannels() > 1 ? buffer.getWritePointer(1) : nullptr;

    // the host's moves (automation, its own controls) into the engine
    for (int i = 0; i < CR_NP; i++) {
        const int v = params[(size_t)i]->get();
        if (v != lastHost[(size_t)i].load()) {
            lastHost[(size_t)i] = v;
            if (inst->p.p[i] != v) {
                cr_inst_param(inst.get(), (uint32_t)i, (uint32_t)v);
                toUi[(size_t)i >> 5] |= 1u << (i & 31);
            }
        }
    }
    // where the host is
    blockPpq = -1.0;
    blockBpm = 0.0;
    blockPlaying = false;
    if (auto *ph = getPlayHead())
        if (auto pos = ph->getPosition()) {
            if (auto b = pos->getBpm())
                blockBpm = *b;
            if (auto q = pos->getPpqPosition())
                blockPpq = *q;
            blockPlaying = pos->getIsPlaying();
        }

    auto it = midi.cbegin();
    for (int s = 0; s < n; s++) {
        for (; it != midi.cend() && (*it).samplePosition <= s; ++it) {
            const auto m = (*it).getMessage();
            if (!m.isSysEx())
                cr_inst_midi(inst.get(), m.getRawData(), (uint32_t)m.getRawDataSize());
        }
        hostSampleInBlock = s;
        while (phase >= 1.0) {                      // the next engine frame into the history
            phase -= 1.0;
            if (engPos >= (int)CR_INST_FRAMES)
                renderEngineBlock();
            for (int k = 0; k < 3; k++) {
                hist[k][0] = hist[k + 1][0];
                hist[k][1] = hist[k + 1][1];
            }
            hist[3][0] = eng[engPos * 2];
            hist[3][1] = eng[engPos * 2 + 1];
            engPos++;
        }
        const float t = (float)phase;
        for (int c = 0; c < 2; c++) {               // cubic Hermite between hist[1] and hist[2]
            const float y0 = hist[0][c], y1 = hist[1][c], y2 = hist[2][c], y3 = hist[3][c];
            const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3;
            const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
            const float y = ((c3 * t + c2) * t + c1) * t + y1;
            if (c == 0)
                outL[s] = y;
            else if (outR)
                outR[s] = y;
        }
        phase += step;
    }
    for (; it != midi.cend(); ++it) {
        const auto m = (*it).getMessage();
        if (!m.isSysEx())
            cr_inst_midi(inst.get(), m.getRawData(), (uint32_t)m.getRawDataSize());
    }
    for (int c = 2; c < buffer.getNumChannels(); c++)
        buffer.clear(c, 0, n);

    // what MIDI changed from inside: the host and the editor are told on the message thread
    for (uint32_t id; (id = cr_inst_take_changed(inst.get())) != 0;) {
        lastHost[id - 1] = inst->p.p[id - 1];
        toUi[(id - 1) >> 5] |= 1u << ((id - 1) & 31);
        engineNews = true;
    }
    if (cr_inst_take_patch_changed(inst.get())) {
        for (int i = 0; i < CR_NP; i++)
            lastHost[(size_t)i] = inst->p.p[i];
        patchNews = true;
    }
}

// --------------------------------------------------------------------------------- the message thread ---
void CraftyProcessor::syncParamsFromEngine()
{
    cr_patch_t p;
    {
        const juce::ScopedLock sl(lock);
        p = inst->p;
        (void)cr_inst_take_patch_changed(inst.get());
    }
    for (int i = 0; i < CR_NP; i++) {
        lastHost[(size_t)i] = p.p[i];
        if (params[(size_t)i]->get() != p.p[i])
            params[(size_t)i]->setValueNotifyingHost(params[(size_t)i]->convertTo0to1((float)p.p[i]));
    }
}

void CraftyProcessor::sendEvent(uint32_t kind)
{
    if (!toEditor)
        return;
    uint8_t ev[10];
    {
        const juce::ScopedLock sl(lock);
        cr_proto_event(inst.get(), kind, ev);
    }
    toEditor(std::vector<uint8_t>(ev, ev + 10));
}

void CraftyProcessor::timerCallback()
{
    if (patchNews.exchange(false)) {
        syncParamsFromEngine();
        sendEvent(1);
        updateHostDisplay(ChangeDetails().withProgramChanged(true));
        for (auto &w : toUi)
            w = 0;                                  // the editor reads the whole patch
        engineNews = false;
        return;
    }
    if (engineNews.exchange(false))
        syncParamsFromEngine();
    if (!toEditor)
        return;
    for (int w = 0; w < 4; w++) {
        uint32_t bits = toUi[(size_t)w].exchange(0);
        for (int b = 0; bits && b < 32; b++)
            if (bits & (1u << b)) {
                const int id = w * 32 + b;
                uint8_t v;
                {
                    const juce::ScopedLock sl(lock);
                    v = inst->p.p[id];
                }
                toEditor({0xB0, CR_P[id].cc, v});
                bits &= ~(1u << b);
            }
    }
}

void CraftyProcessor::fromEditor(const std::vector<uint8_t> &f)
{
    if (f.empty())
        return;
    if (f[0] == 0xF0) {
        if (f.size() < 6 || f[1] != 0x7D || f[2] != 0x46 || f[3] != 0x4C || f.back() != 0xF7)
            return;
        const uint32_t cmd = f[4];
        std::vector<uint8_t> reply{0xF0, 0x7D, 0x46, 0x4C, (uint8_t)cmd};
        int ok;
        bool userDirty;
        {
            const juce::ScopedLock sl(lock);
            auto &st = userArea->store();
            const uint8_t before = st.user_dirty;
            st.user_dirty = 0;
            ok = cr_proto_cmd(&st, inst.get(), cmd, f.data() + 5, (uint32_t)(f.size() - 6),
                              [](void *ctx, uint32_t b) { static_cast<std::vector<uint8_t> *>(ctx)->push_back((uint8_t)(b & 0x7F)); },
                              &reply);
            userDirty = st.user_dirty != 0;
            st.user_dirty = (uint8_t)(before | st.user_dirty);
        }
        if (!ok)
            return;
        reply.push_back(0xF7);
        if (toEditor)
            toEditor(reply);
        if (userDirty)
            userArea->changed();
        if (cmd == CR_ED_PPUT || cmd == CR_ED_SELECT) {
            syncParamsFromEngine();
            if (cmd == CR_ED_SELECT)
                updateHostDisplay(ChangeDetails().withProgramChanged(true));
        }
        return;
    }
    {
        const juce::ScopedLock sl(lock);
        cr_inst_midi(inst.get(), f.data(), (uint32_t)f.size());
    }
    if ((f[0] & 0xF0) == 0xB0)
        syncParamsFromEngine();
}

void CraftyProcessor::userAreaChanged()
{
    const juce::ScopedLock sl(lock);
    cr_inst_user_changed(inst.get());
}

// ------------------------------------------------------------------------------------------- programs ---
int CraftyProcessor::getCurrentProgram()
{
    const juce::ScopedLock sl(lock);
    return inst->cur;
}
void CraftyProcessor::setCurrentProgram(int index)
{
    if (index < 0 || index >= CR_NPRESET)
        return;
    {
        const juce::ScopedLock sl(lock);
        cr_inst_all_off(inst.get());
        cr_inst_load(inst.get(), (uint32_t)index);
    }
    syncParamsFromEngine();
    sendEvent(1);
}
const juce::String CraftyProcessor::getProgramName(int index)
{
    if (index < 0 || index >= CR_NPRESET)
        return {};
    char label[8];
    cr_preset_label((uint32_t)index, label);
    cr_patch_t p;
    const int real = cr_preset_get(userArea->store().user, (uint32_t)index, &p);
    return juce::String(label) + " " + (real ? juce::String(p.name) : juce::String("(empty)"));
}

// --------------------------------------------------------------------------------------------- state ---
// "CRS1", the preset it came from (2 bytes), dirty, 0, then the patch (128 bytes) and the globals (16)
void CraftyProcessor::getStateInformation(juce::MemoryBlock &dest)
{
    const juce::ScopedLock sl(lock);
    uint8_t h[8] = {'C', 'R', 'S', '1', (uint8_t)(inst->cur & 0xFF), (uint8_t)(inst->cur >> 8), inst->dirty, 0};
    dest.append(h, sizeof h);
    dest.append(&inst->p, CR_PATCH_BYTES);
    dest.append(&inst->g, sizeof inst->g);
}
void CraftyProcessor::setStateInformation(const void *data, int size)
{
    const auto *d = static_cast<const uint8_t *>(data);
    if (size < 8 + (int)CR_PATCH_BYTES || std::memcmp(d, "CRS1", 4) != 0)
        return;
    cr_patch_t p;
    std::memcpy(&p, d + 8, CR_PATCH_BYTES);
    if (cr_patch_check(&p) != 0)
        return;
    {
        const juce::ScopedLock sl(lock);
        cr_inst_all_off(inst.get());
        cr_inst_use(inst.get(), &p);
        inst->cur = (uint16_t)((d[4] | d[5] << 8) % CR_NPRESET);
        inst->dirty = d[6];
        if (size >= 8 + (int)CR_PATCH_BYTES + (int)sizeof(cr_globals_t)) {
            cr_globals_t g;
            std::memcpy(&g, d + 8 + CR_PATCH_BYTES, sizeof g);
            if (cr_globals_check(&g) == 0)
                inst->g = g;
        }
    }
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        syncParamsFromEngine();
    else
        patchNews = true;                           // the timer tells the host and the editor
}

#if !CRAFTY_NO_EDITOR
#include "CraftyEditor.h"
juce::AudioProcessorEditor *CraftyProcessor::createEditor() { return new CraftyEditor(*this); }
bool CraftyProcessor::hasEditor() const { return true; }
#else
juce::AudioProcessorEditor *CraftyProcessor::createEditor() { return new juce::GenericAudioProcessorEditor(*this); }
bool CraftyProcessor::hasEditor() const { return true; }
#endif

juce::AudioProcessor *JUCE_CALLTYPE createPluginFilter() { return new CraftyProcessor(); }
