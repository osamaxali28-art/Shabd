#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

#include "core/Subtitles.h"

#include <functional>

// Shows the transcript bar by bar with timestamps. Words Shabd isn't sure
// about are amber; double-click any word to fix it.
class TranscriptView : public juce::Component
{
public:
    TranscriptView();

    void setWords (const std::vector<shabd::TimedWord>& words);
    void setPlayhead (double seconds);
    int idealHeight() const { return contentHeight; }

    // Called with (word index, new text). Empty text deletes the word.
    std::function<void (size_t, const std::string&)> onEdit;
    // Called after layout so the parent can resize / scroll.
    std::function<void()> onLayoutChanged;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

    static juce::Colour accent() { return juce::Colour (0xff19c37d); }
    static juce::Colour amber()  { return juce::Colour (0xfff5a623); }

private:
    struct WordBox
    {
        size_t index;
        juce::Rectangle<float> box;
        juce::String text;
        bool review;
    };
    struct Row
    {
        double start, end;
        juce::String time;
        float y, height;
        std::vector<WordBox> words;
    };

    std::vector<shabd::TimedWord> source;
    std::vector<Row> rows;
    double playhead = -1.0;
    int contentHeight = 0;

    juce::Font wordFont, timeFont;
    std::unique_ptr<juce::TextEditor> editor;
    size_t editingIndex = 0;

    void layout();
    void commitEdit();
};
