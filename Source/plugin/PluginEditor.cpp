#include "PluginEditor.h"

namespace
{
const juce::Colour kBg (0xff0d0f12);
const juce::Colour kPanel (0xff1a1d22);
const juce::Colour kText (0xffe8eaed);
const juce::Colour kDim (0xff8a9099);

juce::String clock (double s)
{
    const int t = (int) s;
    return juce::String::formatted ("%d:%02d", t / 60, t % 60);
}
} // namespace

ShabdEditor::ShabdEditor (ShabdProcessor& p)
    : AudioProcessorEditor (p), proc (p)
{
    for (auto* b : { &modelButton, &liveButton, &finalButton, &clearButton, &exportButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }

    liveButton.setClickingTogglesState (true);
    liveButton.setToggleState (proc.session().isLiveEnabled(), juce::dontSendNotification);
    liveButton.setTooltip ("Transcribe while the track plays");
    liveButton.onClick = [this] { proc.session().setLiveEnabled (liveButton.getToggleState()); };

    modelButton.onClick = [this] { chooseModel(); };
    exportButton.onClick = [this] { exportCaptions(); };

    finalButton.setTooltip ("Re-listen to the whole take with the slower, more accurate settings");
    finalButton.onClick = [this]
    {
        auto& s = proc.session();
        if (s.state() == shabd::LiveSession::State::Finalising) { s.cancelFinalPass(); return; }
        if (s.words().empty() || confirm (finalButton, "Replace text?"))
            s.runFinalPass();
    };

    clearButton.onClick = [this]
    {
        if (confirm (clearButton, "Sure?")) proc.session().clear();
    };

    styleField (titleField, "Song title");
    styleField (artistField, "Artist");
    titleField.setText (proc.title, false);
    artistField.setText (proc.artist.isEmpty() ? juce::String ("Moji") : proc.artist, false);
    proc.artist = artistField.getText();
    titleField.onTextChange = [this] { proc.title = titleField.getText(); };
    artistField.onTextChange = [this] { proc.artist = artistField.getText(); };
    addAndMakeVisible (titleField);
    addAndMakeVisible (artistField);

    for (auto* l : { &status, &footer })
    {
        l->setColour (juce::Label::textColourId, kDim);
        l->setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (*l);
    }
    status.setJustificationType (juce::Justification::centredRight);

    transcript.onEdit = [this] (size_t i, const std::string& text) { proc.session().editWord (i, text); };
    transcript.onLayoutChanged = [this]
    {
        const int h = juce::jmax (transcript.idealHeight(), viewport.getHeight());
        if (transcript.getHeight() != h) transcript.setSize (transcript.getWidth(), h);
    };
    viewport.setViewedComponent (&transcript, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    setResizable (true, true);
    setResizeLimits (520, 360, 1600, 1200);
    setSize (760, 520);
    startTimerHz (10);
}

ShabdEditor::~ShabdEditor()
{
    stopTimer();
}

void ShabdEditor::styleButton (juce::TextButton& b)
{
    b.setColour (juce::TextButton::buttonColourId, kPanel);
    b.setColour (juce::TextButton::buttonOnColourId, TranscriptView::accent().withAlpha (0.85f));
    b.setColour (juce::TextButton::textColourOffId, kText);
    b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
}

void ShabdEditor::styleField (juce::TextEditor& f, const juce::String& hint)
{
    f.setTextToShowWhenEmpty (hint, kDim);
    f.setColour (juce::TextEditor::backgroundColourId, kPanel);
    f.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
    f.setColour (juce::TextEditor::focusedOutlineColourId, TranscriptView::accent());
    f.setColour (juce::TextEditor::textColourId, kText);
    f.setFont (juce::FontOptions (14.0f));
}

bool ShabdEditor::confirm (juce::TextButton& b, const juce::String& prompt)
{
    const auto now = juce::Time::getMillisecondCounter();
    if (armedButton == &b && now - armedAt < 3000)
    {
        armedButton = nullptr;
        return true;
    }
    armedButton = &b;
    armedAt = now;
    b.setButtonText (prompt);
    return false;
}

void ShabdEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SHABD", 16, 10, 120, 32, juce::Justification::centredLeft);
    g.setColour (TranscriptView::accent());
    g.fillEllipse (100.0f, 22.0f, 7.0f, 7.0f);
}

void ShabdEditor::resized()
{
    auto r = getLocalBounds().reduced (12, 8);

    auto top = r.removeFromTop (36);
    top.removeFromLeft (110);
    status.setBounds (top);

    r.removeFromTop (6);
    auto bar = r.removeFromTop (30);
    for (auto* b : { &modelButton, &liveButton, &finalButton, &clearButton })
    {
        b->setBounds (bar.removeFromLeft (b == &finalButton ? 92 : 70));
        bar.removeFromLeft (6);
    }
    exportButton.setBounds (bar.removeFromRight (80));
    bar.removeFromRight (8);
    artistField.setBounds (bar.removeFromRight (juce::jmin (130, bar.getWidth() / 2)));
    bar.removeFromRight (6);
    titleField.setBounds (bar);

    footer.setBounds (r.removeFromBottom (24));
    r.removeFromBottom (4);
    viewport.setBounds (r);
    transcript.setSize (r.getWidth() - viewport.getScrollBarThickness(), juce::jmax (transcript.idealHeight(), r.getHeight()));
}

void ShabdEditor::timerCallback()
{
    auto& s = proc.session();
    using State = shabd::LiveSession::State;

    // Reset two-click confirm buttons after 3 s.
    if (armedButton != nullptr && juce::Time::getMillisecondCounter() - armedAt >= 3000) armedButton = nullptr;
    if (armedButton != &clearButton) clearButton.setButtonText ("Clear");

    juce::String text;
    switch (s.state())
    {
        case State::NoModel:      text = "No model loaded: click Model"; break;
        case State::LoadingModel: text = "Loading model..."; break;
        case State::Ready:        text = juce::String (s.message()); break;
        case State::Listening:    text = s.isLiveEnabled() ? "Listening" : "Live off"; break;
        case State::Transcribing: text = "Hearing bars..."; break;
        case State::Finalising:   text = "Final pass " + juce::String ((int) (s.progress() * 100.0f)) + "%"; break;
        case State::Error:        text = juce::String (s.message()); break;
    }
    if (s.droppedAudio()) text << "  |  CPU busy, some audio skipped";
    if (notice.isNotEmpty() && juce::Time::getMillisecondCounter() - noticeAt < 4000) text = notice;
    status.setText (text, juce::dontSendNotification);
    status.setColour (juce::Label::textColourId, s.state() == State::Error ? juce::Colours::orangered : kDim);

    if (s.state() == State::Finalising) finalButton.setButtonText ("Cancel");
    else if (armedButton != &finalButton) finalButton.setButtonText ("Final pass");

    modelButton.setTooltip (s.modelPath().empty() ? juce::String ("Choose a Whisper model (.bin)")
                                                  : juce::String (s.modelPath()));

    if (s.revision() != shownRevision)
    {
        shownRevision = s.revision();
        const auto words = s.words();
        const bool atBottom = viewport.getViewPositionY() + viewport.getHeight() >= transcript.getHeight() - 40;
        transcript.setWords (words);
        if (atBottom) viewport.setViewPosition (0, juce::jmax (0, transcript.getHeight() - viewport.getHeight()));

        int review = 0;
        for (auto& w : words)
            if (w.needsReview()) ++review;
        footer.setText (juce::String ((int) words.size()) + " words   "
                            + (review > 0 ? juce::String (review) + " to check (amber)   " : juce::String())
                            + clock (s.capturedSeconds()) + " heard   |   double-click a word to fix it",
                        juce::dontSendNotification);
    }
    transcript.setPlayhead (proc.playheadSeconds());
}

void ShabdEditor::chooseModel()
{
    const auto dir = ShabdProcessor::shabdFolder().getChildFile ("models");
    dir.createDirectory();
    chooser = std::make_unique<juce::FileChooser> ("Choose a Whisper model", dir, "*.bin");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f.existsAsFile()) proc.loadModel (f);
                          });
}

void ShabdEditor::exportCaptions()
{
    const auto dir = ShabdProcessor::shabdFolder().getChildFile ("exports");
    dir.createDirectory();
    const auto name = proc.title.isNotEmpty() ? juce::File::createLegalFileName (proc.title) : juce::String ("Shabd");
    chooser = std::make_unique<juce::FileChooser> ("Export captions (writes SRT, VTT, LRC, TTML, TXT)",
                                                   dir.getChildFile (name + ".srt"), "*.srt");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              const auto f = fc.getResult();
                              if (f == juce::File()) return;
                              const auto written = proc.exportAll (f);
                              notice = "Exported " + juce::String (written.size()) + " files to " + f.getParentDirectory().getFileName();
                              noticeAt = juce::Time::getMillisecondCounter();
                              if (! written.isEmpty()) written.getFirst().revealToUser();
                          });
}
