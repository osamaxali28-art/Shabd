#include "TranscriptView.h"

namespace
{
constexpr float kPad = 14.0f;
constexpr float kTimeCol = 54.0f;
constexpr float kGap = 6.0f;
constexpr float kRowGap = 6.0f;
constexpr float kWordH = 24.0f;

juce::String clock (double s)
{
    const int t = (int) s;
    return juce::String::formatted ("%d:%02d", t / 60, t % 60);
}
} // namespace

TranscriptView::TranscriptView()
    : wordFont (juce::FontOptions (17.0f)),
      timeFont (juce::FontOptions (12.0f))
{
    setWantsKeyboardFocus (false);
}

void TranscriptView::setWords (const std::vector<shabd::TimedWord>& words)
{
    if (editor != nullptr) return; // don't yank the text out from under an edit
    source = words;
    layout();
    repaint();
}

void TranscriptView::setPlayhead (double s)
{
    if (std::abs (s - playhead) < 0.05) return;
    playhead = s;
    repaint();
}

void TranscriptView::resized()
{
    layout();
}

void TranscriptView::layout()
{
    rows.clear();
    const auto lines = shabd::buildLines (source);
    const float maxX = (float) getWidth() - kPad;
    float y = kPad;
    size_t index = 0; // words in `source` are sorted and non-empty, so lines map back in order

    for (auto& line : lines)
    {
        Row row;
        row.start = line.start;
        row.end = line.end;
        row.time = clock (line.start);
        row.y = y;

        float x = kPad + kTimeCol;
        float rowBottom = y + kWordH;
        for (auto& w : line.words)
        {
            juce::String text = juce::String::fromUTF8 (w.text.c_str());
            juce::GlyphArrangement ga;
            ga.addLineOfText (wordFont, text, 0.0f, 0.0f);
            const float wdt = ga.getBoundingBox (0, -1, true).getWidth() + 10.0f;
            if (x + wdt > maxX && x > kPad + kTimeCol)
            {
                x = kPad + kTimeCol;
                rowBottom += kWordH + 2.0f;
            }
            row.words.push_back ({ index++, { x, rowBottom - kWordH, wdt, kWordH }, text, w.needsReview() });
            x += wdt + kGap;
        }
        row.height = rowBottom - y;
        y = rowBottom + kRowGap;
        rows.push_back (std::move (row));
    }

    contentHeight = (int) (y + kPad);
    if (onLayoutChanged) onLayoutChanged();
}

void TranscriptView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff121417));

    if (rows.empty())
    {
        g.setColour (juce::Colours::white.withAlpha (0.4f));
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText ("Press play: Shabd writes the bars here as it hears them.\n"
                          "Put it on your lead vocal or vocal bus.",
                          getLocalBounds().reduced (24), juce::Justification::centred, 3);
        return;
    }

    for (auto& row : rows)
    {
        const bool current = playhead >= row.start && playhead < row.end;
        if (current)
        {
            g.setColour (accent().withAlpha (0.10f));
            g.fillRoundedRectangle ({ kPad * 0.5f, row.y - 3.0f, (float) getWidth() - kPad, row.height + 6.0f }, 6.0f);
        }

        g.setFont (timeFont);
        g.setColour (current ? accent() : juce::Colours::white.withAlpha (0.35f));
        g.drawText (row.time, juce::Rectangle<float> (kPad, row.y, kTimeCol - 8.0f, kWordH), juce::Justification::centredLeft);

        g.setFont (wordFont);
        for (auto& w : row.words)
        {
            if (w.review)
            {
                g.setColour (amber().withAlpha (0.16f));
                g.fillRoundedRectangle (w.box, 4.0f);
                g.setColour (amber());
            }
            else
            {
                g.setColour (juce::Colours::white.withAlpha (0.92f));
            }
            g.drawText (w.text, w.box, juce::Justification::centred, false);
        }
    }
}

void TranscriptView::mouseDoubleClick (const juce::MouseEvent& e)
{
    const auto p = e.position;
    for (auto& row : rows)
        for (auto& w : row.words)
            if (w.box.expanded (2.0f).contains (p))
            {
                editingIndex = w.index;
                editor = std::make_unique<juce::TextEditor>();
                editor->setFont (wordFont);
                editor->setText (w.text, false);
                editor->setJustification (juce::Justification::centred);
                editor->setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff23272e));
                editor->setColour (juce::TextEditor::outlineColourId, accent());
                editor->setColour (juce::TextEditor::textColourId, juce::Colours::white);
                editor->onReturnKey = [this] { commitEdit(); };
                editor->onEscapeKey = [this]
                {
                    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TranscriptView> (this)]
                    {
                        if (safe != nullptr) { safe->editor.reset(); safe->repaint(); }
                    });
                };
                editor->onFocusLost = [this] { commitEdit(); };
                addAndMakeVisible (*editor);
                editor->setBounds (w.box.expanded (24.0f, 3.0f).toNearestInt());
                editor->grabKeyboardFocus();
                editor->selectAll();
                return;
            }
}

void TranscriptView::commitEdit()
{
    if (editor == nullptr) return;
    const auto text = editor->getText().trim().toStdString();
    const auto index = editingIndex;
    // Defer destruction: we may be inside one of the editor's own callbacks.
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TranscriptView> (this), index, text]
    {
        if (safe == nullptr || safe->editor == nullptr) return;
        safe->editor.reset();
        if (safe->onEdit) safe->onEdit (index, text);
    });
}
