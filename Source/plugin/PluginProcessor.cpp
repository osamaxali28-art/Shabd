#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "core/LexiconData.h"

ShabdProcessor::ShabdProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    // Your own spellings win: Documents/Shabd/roman_urdu.tsv overrides the built-in list.
    const auto userLexicon = shabdFolder().getChildFile ("roman_urdu.tsv");
    if (userLexicon.existsAsFile())
        live.setLexicon (userLexicon.loadFileAsString().toStdString());
    else
        live.setLexicon (shabd::shabdLexiconTsv());

    if (auto model = findDefaultModel(); model.existsAsFile())
        loadModel (model);
}

juce::File ShabdProcessor::shabdFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Shabd");
}

juce::File ShabdProcessor::findDefaultModel()
{
    const auto dir = shabdFolder().getChildFile ("models");
    auto files = dir.findChildFiles (juce::File::findFiles, false, "*.bin");
    if (files.isEmpty()) return {};

    // Prefer a model fine-tuned on your vocals, then large-v3-turbo, then anything.
    auto score = [] (const juce::File& f)
    {
        const auto n = f.getFileName().toLowerCase();
        if (n.contains ("shabd")) return 3;
        if (n.contains ("large-v3-turbo")) return 2;
        if (n.contains ("large")) return 1;
        return 0;
    };
    std::sort (files.begin(), files.end(), [&] (const juce::File& a, const juce::File& b) { return score (a) > score (b); });
    return files.getFirst();
}

void ShabdProcessor::loadModel (const juce::File& modelFile)
{
    shabd::DetectorSettings s;
    s.modelPath = modelFile.getFullPathName().toStdString();
    // Leave a couple of cores for the DAW.
    s.threads = juce::jmax (1, juce::SystemStats::getNumCpus() - 2);
    live.loadModel (s);
}

void ShabdProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sampleRateHz = sampleRate;
    mono.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    live.prepare (sampleRate);
}

bool ShabdProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo()) return false;
    return layouts.getMainInputChannelSet() == out;
}

void ShabdProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = getTotalNumInputChannels();
    if (n == 0 || chans == 0) return;

    // Where are we on the song timeline?
    double t = 0.0;
    bool rolling = false;
    if (wrapperType == wrapperType_Standalone)
    {
        t = freeRunSeconds; // standalone: always listening (e.g. a mic)
        rolling = true;
        freeRunSeconds += n / sampleRateHz;
    }
    else if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            rolling = pos->getIsPlaying() || pos->getIsRecording();
            if (auto secs = pos->getTimeInSeconds()) t = *secs;
            else if (auto samples = pos->getTimeInSamples()) t = (double) *samples / sampleRateHz;
            else rolling = false;
        }
    }

    // Audio passes through untouched; Shabd only listens.
    if (! rolling) return;
    lastTime = t;

    for (int offset = 0; offset < n;)
    {
        const int len = juce::jmin ((int) mono.size(), n - offset);
        const float gain = 1.0f / (float) chans;
        for (int i = 0; i < len; ++i)
        {
            float s = 0.0f;
            for (int c = 0; c < chans; ++c) s += buffer.getReadPointer (c)[offset + i];
            mono[(size_t) i] = s * gain;
        }
        live.pushBlock (mono.data(), len, t + offset / sampleRateHz);
        offset += len;
    }
}

juce::AudioProcessorEditor* ShabdProcessor::createEditor()
{
    return new ShabdEditor (*this);
}

// ------------------------------------------------------------------ state

void ShabdProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::XmlElement root ("Shabd");
    root.setAttribute ("version", 1);
    root.setAttribute ("model", juce::String (live.modelPath()));
    root.setAttribute ("title", title);
    root.setAttribute ("artist", artist);
    root.setAttribute ("live", live.isLiveEnabled());

    auto* wordsXml = root.createNewChildElement ("Words");
    for (auto& w : live.words())
    {
        auto* e = wordsXml->createNewChildElement ("W");
        e->setAttribute ("s", w.start);
        e->setAttribute ("e", w.end);
        e->setAttribute ("p", (double) w.prob);
        e->setAttribute ("g", w.guessed);
        e->setAttribute ("t", juce::String::fromUTF8 (w.text.c_str()));
    }
    copyXmlToBinary (root, destData);
}

void ShabdProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto root = getXmlFromBinary (data, sizeInBytes);
    if (root == nullptr || ! root->hasTagName ("Shabd")) return;

    title = root->getStringAttribute ("title");
    artist = root->getStringAttribute ("artist");
    live.setLiveEnabled (root->getBoolAttribute ("live", true));

    std::vector<shabd::TimedWord> words;
    if (auto* wordsXml = root->getChildByName ("Words"))
        for (auto* e : wordsXml->getChildIterator())
        {
            shabd::TimedWord w;
            w.start = e->getDoubleAttribute ("s");
            w.end = e->getDoubleAttribute ("e");
            w.prob = (float) e->getDoubleAttribute ("p", 1.0);
            w.guessed = e->getBoolAttribute ("g");
            w.text = e->getStringAttribute ("t").toStdString();
            if (! w.text.empty()) words.push_back (w);
        }
    live.setWords (std::move (words));

    const juce::File model (root->getStringAttribute ("model"));
    if (model.existsAsFile() && model.getFullPathName().toStdString() != live.modelPath())
        loadModel (model);
}

// ------------------------------------------------------------------ export

juce::Array<juce::File> ShabdProcessor::exportAll (const juce::File& base)
{
    const auto lines = shabd::buildLines (live.words());
    shabd::ExportInfo info;
    info.title = title.toStdString();
    info.artist = artist.toStdString();

    const auto dir = base.getParentDirectory();
    const auto name = base.getFileNameWithoutExtension();
    juce::Array<juce::File> written;

    auto write = [&] (const juce::String& ext, const std::string& text)
    {
        const auto f = dir.getChildFile (name + ext);
        if (f.replaceWithText (juce::String::fromUTF8 (text.c_str()), false, false, "\n"))
            written.add (f);
    };

    write (".srt", shabd::toSRT (lines));          // YouTube, Premiere, CapCut, most players
    write (".vtt", shabd::toVTT (lines));          // YouTube, web players
    write (".lrc", shabd::toLRC (lines, info));    // Spotify / Musixmatch, Apple Music, lyric sites
    write (".words.lrc", shabd::toLRC (lines, info, true)); // word-by-word karaoke timing
    write (".ttml", shabd::toTTML (lines, info));  // Apple Music / distributor time-synced lyrics
    write (".txt", shabd::toPlainLyrics (lines));  // plain lyrics for DistroKid, Genius, descriptions
    return written;
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ShabdProcessor();
}
