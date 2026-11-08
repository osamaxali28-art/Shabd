#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "TranscriptView.h"

class ShabdEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ShabdEditor (ShabdProcessor&);
    ~ShabdEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    ShabdProcessor& proc;

    juce::TextButton modelButton { "Model" }, liveButton { "Live" }, finalButton { "Final pass" },
        clearButton { "Clear" }, exportButton { "Export" };
    juce::TextEditor titleField, artistField;
    juce::Label status, footer;
    juce::Viewport viewport;
    TranscriptView transcript;

    std::unique_ptr<juce::FileChooser> chooser;
    uint64_t shownRevision = ~0ull;
    juce::uint32 armedAt = 0;
    juce::TextButton* armedButton = nullptr;
    juce::String notice;           // short message shown for a few seconds
    juce::uint32 noticeAt = 0;

    void timerCallback() override;
    bool confirm (juce::TextButton& b, const juce::String& prompt); // two-click confirm
    void chooseModel();
    void exportCaptions();
    void styleButton (juce::TextButton& b);
    void styleField (juce::TextEditor& f, const juce::String& hint);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShabdEditor)
};
