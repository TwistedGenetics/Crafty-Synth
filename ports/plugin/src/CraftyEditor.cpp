// SPDX-License-Identifier: GPL-3.0-only
// Crafty Synth's plugin window (CraftyEditor.h). Copyright (C) 2026 Jamie (TwistedGenetics).
#include "CraftyEditor.h"
#include "BinaryData.h"

namespace {
const char *mimeOf(const juce::String &path)
{
    if (path.endsWith(".html")) return "text/html";
    if (path.endsWith(".js") || path.endsWith(".mjs")) return "text/javascript";
    if (path.endsWith(".css")) return "text/css";
    if (path.endsWith(".wasm")) return "application/wasm";
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".svg")) return "image/svg+xml";
    if (path.endsWith(".json")) return "application/json";
    return "application/octet-stream";
}

// the web editor's files, as built into the plugin (juce_add_binary_data keeps their file names)
std::optional<juce::WebBrowserComponent::Resource> resource(const juce::String &url)
{
    auto path = url.upToFirstOccurrenceOf("?", false, false).upToFirstOccurrenceOf("#", false, false);
    path = path.trimCharactersAtStart("/");
    if (path.isEmpty())
        path = "index.html";
    for (int i = 0; i < BinaryData::namedResourceListSize; i++) {
        const char *name = BinaryData::namedResourceList[i];
        if (path == BinaryData::getNamedResourceOriginalFilename(name)) {
            int size = 0;
            const char *d = BinaryData::getNamedResource(name, size);
            std::vector<std::byte> bytes((size_t)size);
            std::memcpy(bytes.data(), d, (size_t)size);
            return juce::WebBrowserComponent::Resource{std::move(bytes), mimeOf(path)};
        }
    }
    return std::nullopt;
}
} // namespace

CraftyEditor::CraftyEditor(CraftyProcessor &p)
    : AudioProcessorEditor(p), proc(p),
      web(juce::WebBrowserComponent::Options{}
              .withBackend(juce::WebBrowserComponent::Options::Backend::webview2)
              .withWinWebView2Options(juce::WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(
                  juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("CraftySynthWebView")))
              .withNativeIntegrationEnabled()
              .withKeepPageLoadedWhenBrowserIsHidden()
              .withResourceProvider(resource)
              .withEventListener("crafty-midi", [this](const juce::var &v) {
                  std::vector<uint8_t> f;
                  if (auto *a = v.getArray())
                      for (auto &x : *a)
                          f.push_back((uint8_t)(int)x);
                  proc.fromEditor(f);
              }))
{
    addAndMakeVisible(web);
    proc.toEditor = [this](const std::vector<uint8_t> &f) {
        juce::Array<juce::var> a;
        for (auto b : f)
            a.add((int)b);
        web.emitEventIfBrowserIsVisible("crafty-midi", juce::var(a));
    };
    setResizable(true, true);
    setResizeLimits(760, 520, 2400, 1800);
    setSize(1240, 860);
    web.goToURL(juce::WebBrowserComponent::getResourceProviderRoot());
}

CraftyEditor::~CraftyEditor() { proc.toEditor = nullptr; }

void CraftyEditor::resized() { web.setBounds(getLocalBounds()); }
