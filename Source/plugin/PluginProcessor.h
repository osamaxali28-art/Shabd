#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

#include "engine/LiveSession.h"

class ShabdProcessor : public juce::AudioProcessor
{
public:
    ShabdProcessor();
    ~ShabdProcessor() override = default;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Shabd"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- Shabd ----
    shabd::LiveSession& session() { return live; }

    void loadModel (const juce::File& modelFile);
    static juce::File shabdFolder();      // Documents/Shabd
    static juce::File findDefaultModel(); // best .bin in Documents/Shabd/models

    // Writes <base>.srt .vtt .lrc .words.lrc .ttml .txt; returns the files written.
    juce::Array<juce::File> exportAll (const juce::File& base);

    juce::String title, artist;
    double playheadSeconds() const { return lastTime.load(); }

private:
    shabd::LiveSession live;
    std::vector<float> mono;
    std::atomic<double> lastTime { 0.0 };
    double freeRunSeconds = 0.0; // standalone app has no DAW transport
    double sampleRateHz = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ShabdProcessor)
};
