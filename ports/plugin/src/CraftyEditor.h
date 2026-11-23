// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth's plugin window: the web editor (web/editor, built into the plugin) in a WebView. It finds
// window.__JUCE__ and talks to the processor with the same MIDI it sends an FM-1 (web/editor/device.js), carried
// as "crafty-midi" events both ways. Copyright (C) 2026 Jamie (TwistedGenetics).
#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "CraftyProcessor.h"

class CraftyEditor : public juce::AudioProcessorEditor {
public:
    explicit CraftyEditor(CraftyProcessor &);
    ~CraftyEditor() override;
    void resized() override;
    void setShape(double w, double h);              // the web layout's size: the window keeps its aspect

private:
    CraftyProcessor &proc;
    juce::WebBrowserComponent web;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CraftyEditor)
};
