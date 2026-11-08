#pragma once
// LiveSession: everything between the audio thread and the Detector.
//
//  audio thread --(lock-free FIFO)--> worker thread
//                                       - resample to 16 kHz
//                                       - write onto the song timeline (loops / punch-ins overwrite)
//                                       - live pass: transcribe a few seconds behind the playhead
//                                       - final pass: re-transcribe the whole take with a wider beam
//
// Words are kept on the DAW timeline, so exported captions line up with the song
// no matter where playback started.
#include "Detector.h"
#include "Resampler.h"

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace shabd
{
class LiveSession
{
public:
    enum class State { NoModel, LoadingModel, Ready, Listening, Transcribing, Finalising, Error };

    LiveSession();
    ~LiveSession();

    // ---- setup (any non-audio thread) ----
    void setLexicon (const std::string& tsv);
    void loadModel (const DetectorSettings& settings); // loads on the worker thread
    void prepare (double hostSampleRate);              // from prepareToPlay

    // ---- audio thread (real-time safe: no locks, no allocation) ----
    void pushBlock (const float* mono, int numSamples, double timelineSeconds);

    // ---- UI thread ----
    std::vector<TimedWord> words() const;
    void setWords (std::vector<TimedWord> w);   // restore from saved state
    void editWord (size_t index, const std::string& newText);
    uint64_t revision() const { return rev.load(); }

    void clear();
    void runFinalPass();
    void cancelFinalPass() { cancelFinal = true; }
    void setLiveEnabled (bool b) { liveEnabled = b; }
    bool isLiveEnabled() const { return liveEnabled.load(); }

    State state() const { return st.load(); }
    std::string message() const;
    float progress() const { return prog.load(); }
    double capturedSeconds() const { return captured.load(); }
    bool droppedAudio() const { return overflow.load(); }
    std::string modelPath() const;

    static constexpr double kMaxTakeSeconds = 20.0 * 60.0;

private:
    // ---- FIFO ----
    static constexpr int kBlock = 1024;
    static constexpr size_t kRing = 2048; // ~43 s at 48 kHz
    struct Block
    {
        double time = 0.0;
        double rate = 0.0;
        int n = 0;
        float data[kBlock];
    };
    std::unique_ptr<Block[]> ring;
    std::atomic<size_t> writeIdx { 0 }, readIdx { 0 };
    std::atomic<double> hostRate { 44100.0 };
    std::atomic<bool> overflow { false };

    // ---- worker ----
    std::thread worker;
    std::atomic<bool> quit { false };
    std::mutex wakeMutex;
    std::condition_variable wake;
    void run();
    void drainFifo();
    void livePass();
    void finalPass();
    void replaceWords (double from, double to, const std::vector<TimedWord>& add);

    Detector detector;
    StreamResampler resampler;

    // Timeline audio at 16 kHz (worker thread only).
    std::vector<float> timeline;
    std::vector<uint8_t> covered; // one flag per 10 ms
    size_t writePos = 0;
    double expectedNext = -1.0, runStart = 0.0, frontier = 0.0, cursor = 0.0;
    double lastAudioWall = 0.0;
    void writeTimeline (const std::vector<float>& x);
    void resetTimeline();

    // Requests from other threads.
    std::mutex reqMutex;
    std::optional<DetectorSettings> pendingModel;
    std::optional<std::string> pendingLexicon;
    std::atomic<bool> wantClear { false }, wantFinal { false }, cancelFinal { false };
    std::atomic<bool> liveEnabled { true };

    // Results.
    mutable std::mutex wordsMutex;
    std::vector<TimedWord> committed;
    std::atomic<uint64_t> rev { 0 };

    std::atomic<State> st { State::NoModel };
    std::atomic<float> prog { 0.0f };
    std::atomic<double> captured { 0.0 };
    mutable std::mutex msgMutex;
    std::string msg, loadedModelPath;
    void setState (State s, const std::string& m = {});
};
} // namespace shabd
