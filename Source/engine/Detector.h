#pragma once
// Detector: the rap-vocal speech recogniser. Wraps whisper.cpp and turns its
// output into word-timed Roman Urdu. Not thread-safe: use from one thread.
#include "../core/RomanUrdu.h"
#include "../core/Subtitles.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>

struct whisper_context;

namespace shabd
{
struct DetectorSettings
{
    std::string modelPath;
    std::string language = "ur"; // "ur", "hi", "pa" or "auto"
    int threads = 4;
    bool useGpu = true;

    int liveBeamSize = 3;        // quick pass while the track plays
    int accurateBeamSize = 8;    // final pass over the whole take

    float noSpeechThreshold = 0.6f;

    // Vocabulary hint. Whisper conditions on this text, which nudges it toward
    // rap vocabulary and Pakistani place names instead of formal news Urdu.
    std::string prompt = "یار، بھائی، فلو، بارز، سین، ریپ، بیٹ، مائیک، لاہور، کراچی، اسلام آباد، پنڈی۔";
};

class Detector
{
public:
    Detector();
    ~Detector();

    bool load (const DetectorSettings& settings, std::string& error);
    void unload();
    bool isLoaded() const { return ctx != nullptr; }
    const DetectorSettings& settings() const { return cfg; }

    void setLexicon (const std::string& tsv) { lexiconSize = roman.loadLexicon (tsv); }
    int lexiconEntries() const { return lexiconSize; }

    // Transcribe up to ~30 s of mono 16 kHz audio. Times are offset by `offsetSec`.
    std::vector<TimedWord> transcribe (const float* samples16k, size_t n, double offsetSec, bool accurate);

    // Transcribe audio of any length: splits at quiet points into <=28 s chunks.
    using Progress = std::function<void (float)>;
    std::vector<TimedWord> transcribeLong (const std::vector<float>& samples16k, double offsetSec,
                                           bool accurate, const Progress& progress = {},
                                           const std::atomic<bool>* cancel = nullptr);

    // Splits [0, n) into chunks at the quietest moments (exposed for tests).
    static std::vector<std::pair<size_t, size_t>> planChunks (const float* x, size_t n, double maxChunkSec = 28.0);

    // RMS of a region in dBFS.
    static float rmsDb (const float* x, size_t n);

private:
    whisper_context* ctx = nullptr;
    DetectorSettings cfg;
    RomanUrdu roman;
    int lexiconSize = 0;
};
} // namespace shabd
