#include "LiveSession.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace shabd
{
namespace
{
constexpr double kRate = StreamResampler::kTargetRate;
constexpr size_t kFrame = 160; // 10 ms at 16 kHz

// Live pass tuning for rap: dense words, short pauses between bars.
constexpr double kLiveWindow = 6.0;      // transcribe at least every 6 s
constexpr double kMinPending = 1.0;      // or after a bar + pause of >= 1 s of audio
constexpr double kOverlap = 1.0;         // re-hear the last second for context
constexpr double kHoldBack = 1.0;        // don't commit words this close to the live edge
constexpr double kPauseDb = -45.0;       // trailing 0.4 s below this = pause between bars
constexpr double kSilenceDb = -50.0;     // whole region below this = nothing to hear
constexpr double kIdleSeconds = 0.6;     // no audio for this long = transport stopped

double nowSeconds()
{
    using namespace std::chrono;
    return duration<double> (steady_clock::now().time_since_epoch()).count();
}
} // namespace

LiveSession::LiveSession()
    : ring (new Block[kRing])
{
    worker = std::thread ([this] { run(); });
}

LiveSession::~LiveSession()
{
    quit = true;
    cancelFinal = true;
    wake.notify_all();
    if (worker.joinable()) worker.join();
}

void LiveSession::setState (State s, const std::string& m)
{
    {
        std::lock_guard<std::mutex> l (msgMutex);
        msg = m;
    }
    st = s;
}

std::string LiveSession::message() const
{
    std::lock_guard<std::mutex> l (msgMutex);
    return msg;
}

std::string LiveSession::modelPath() const
{
    std::lock_guard<std::mutex> l (msgMutex);
    return loadedModelPath;
}

void LiveSession::setLexicon (const std::string& tsv)
{
    std::lock_guard<std::mutex> l (reqMutex);
    pendingLexicon = tsv;
    wake.notify_all();
}

void LiveSession::loadModel (const DetectorSettings& s)
{
    {
        std::lock_guard<std::mutex> l (reqMutex);
        pendingModel = s;
    }
    setState (State::LoadingModel, "Loading model...");
    wake.notify_all();
}

void LiveSession::prepare (double sr)
{
    hostRate = sr;
}

// ---------------------------------------------------------------- audio thread

void LiveSession::pushBlock (const float* mono, int n, double t)
{
    const double sr = hostRate.load();
    int offset = 0;
    while (offset < n)
    {
        const size_t w = writeIdx.load (std::memory_order_relaxed);
        const size_t next = (w + 1) % kRing;
        if (next == readIdx.load (std::memory_order_acquire))
        {
            overflow = true; // worker fell behind; drop rather than block the audio thread
            return;
        }
        auto& b = ring[w];
        b.n = std::min (kBlock, n - offset);
        b.time = t + offset / sr;
        b.rate = sr;
        std::copy (mono + offset, mono + offset + b.n, b.data);
        writeIdx.store (next, std::memory_order_release);
        offset += b.n;
    }
}

// ---------------------------------------------------------------- UI thread

std::vector<TimedWord> LiveSession::words() const
{
    std::lock_guard<std::mutex> l (wordsMutex);
    return committed;
}

void LiveSession::setWords (std::vector<TimedWord> w)
{
    std::sort (w.begin(), w.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    std::lock_guard<std::mutex> l (wordsMutex);
    committed = std::move (w);
    ++rev;
}

void LiveSession::editWord (size_t index, const std::string& text)
{
    std::lock_guard<std::mutex> l (wordsMutex);
    if (index >= committed.size()) return;
    if (text.empty())
        committed.erase (committed.begin() + (long) index);
    else
    {
        committed[index].text = text;
        committed[index].guessed = false;
        committed[index].prob = 1.0f; // a human checked it
    }
    ++rev;
}

void LiveSession::clear()
{
    {
        std::lock_guard<std::mutex> l (wordsMutex);
        committed.clear();
        ++rev;
    }
    wantClear = true;
    wake.notify_all();
}

void LiveSession::runFinalPass()
{
    cancelFinal = false;
    wantFinal = true;
    wake.notify_all();
}

// ---------------------------------------------------------------- worker thread

void LiveSession::resetTimeline()
{
    timeline.clear();
    covered.clear();
    writePos = 0;
    expectedNext = -1.0;
    runStart = frontier = cursor = 0.0;
    captured = 0.0;
}

void LiveSession::writeTimeline (const std::vector<float>& x)
{
    const size_t maxLen = (size_t) (kMaxTakeSeconds * kRate);
    if (writePos >= maxLen || x.empty()) return;

    const size_t n = std::min (x.size(), maxLen - writePos);
    if (timeline.size() < writePos + n)
    {
        timeline.resize (writePos + n, 0.0f);
        covered.resize ((writePos + n) / kFrame + 1, 0);
    }
    std::copy (x.begin(), x.begin() + (long) n, timeline.begin() + (long) writePos);
    for (size_t f = writePos / kFrame; f <= (writePos + n - 1) / kFrame; ++f) covered[f] = 1;

    writePos += n;
    frontier = (double) writePos / kRate;

    size_t frames = 0;
    for (auto c : covered) frames += c;
    captured = (double) frames * kFrame / kRate;
}

void LiveSession::drainFifo()
{
    std::vector<float> tmp;
    while (readIdx.load (std::memory_order_relaxed) != writeIdx.load (std::memory_order_acquire))
    {
        const size_t r = readIdx.load (std::memory_order_relaxed);
        const auto& b = ring[r];

        // A jump in time (loop, punch-in, playhead moved) starts a new run.
        const bool jump = expectedNext < 0.0 || std::abs (b.time - expectedNext) > 0.02;
        if (jump || std::abs (resampler.sourceRate() - b.rate) > 0.5)
        {
            resampler.reset (b.rate);
            writePos = (size_t) std::llround (std::max (0.0, b.time) * kRate);
            runStart = (double) writePos / kRate;
            if (jump) cursor = runStart; // start hearing from here; a retake replaces old words
        }

        tmp.clear();
        resampler.process (b.data, b.n, tmp);
        writeTimeline (tmp);
        expectedNext = b.time + b.n / b.rate;
        lastAudioWall = nowSeconds();

        readIdx.store ((r + 1) % kRing, std::memory_order_release);
    }
}

void LiveSession::replaceWords (double from, double to, const std::vector<TimedWord>& add)
{
    std::lock_guard<std::mutex> l (wordsMutex);
    committed.erase (std::remove_if (committed.begin(), committed.end(),
                                     [&] (const TimedWord& w) { return w.start >= from && w.start < to; }),
                     committed.end());
    committed.insert (committed.end(), add.begin(), add.end());
    std::stable_sort (committed.begin(), committed.end(), [] (auto& a, auto& b) { return a.start < b.start; });
    ++rev;
}

void LiveSession::livePass()
{
    if (! detector.isLoaded() || ! liveEnabled) return;

    const double pending = frontier - cursor;
    if (pending < kMinPending) return;

    const auto idx = [this] (double s)
    {
        return std::min (timeline.size(), (size_t) std::llround (std::max (0.0, s) * kRate));
    };
    const bool idle = nowSeconds() - lastAudioWall > kIdleSeconds;
    const bool pause = Detector::rmsDb (timeline.data() + idx (frontier - 0.4), idx (0.4)) < kPauseDb;

    if (pending < kLiveWindow && ! pause && ! idle) return;

    const double from = cursor;
    const double to = frontier;

    // Nothing but silence / bleed: skip ahead.
    if (Detector::rmsDb (timeline.data() + idx (from), idx (to) - idx (from)) < kSilenceDb)
    {
        cursor = to;
        return;
    }

    setState (State::Transcribing, "Listening...");
    const double winStart = std::max (runStart, from - kOverlap);
    auto heard = detector.transcribe (timeline.data() + idx (winStart), idx (to) - idx (winStart), winStart, false);

    // Commit everything if the rapper paused; otherwise hold back the last
    // second, since a word at the live edge may be cut in half.
    const double commitUntil = (pause || idle) ? to : to - kHoldBack;

    std::vector<TimedWord> keep;
    double straddle = -1.0;
    for (auto& w : heard)
    {
        if (w.start < from - 0.05) continue; // already committed on the previous pass
        if (w.end <= commitUntil + 0.05) keep.push_back (w);
        else if (w.start < commitUntil && straddle < 0.0) straddle = w.start;
    }

    const double newCursor = straddle >= 0.0 ? std::max (from, straddle) : std::max (from, commitUntil);
    replaceWords (from - 0.05, newCursor, keep);
    cursor = newCursor;
    setState (State::Listening, "Listening");
}

void LiveSession::finalPass()
{
    if (! detector.isLoaded())
    {
        setState (State::Error, "Load a model first");
        return;
    }
    if (timeline.empty())
    {
        setState (State::Ready, "Nothing captured yet: play the vocal through Shabd first");
        return;
    }

    setState (State::Finalising, "Final pass...");
    prog = 0.0f;

    // Find captured regions on the timeline.
    std::vector<std::pair<size_t, size_t>> regions;
    for (size_t f = 0; f < covered.size();)
    {
        if (! covered[f]) { ++f; continue; }
        size_t g = f;
        while (g < covered.size() && covered[g]) ++g;
        regions.emplace_back (f * kFrame, std::min (g * kFrame, timeline.size()));
        f = g;
    }

    size_t total = 0, done = 0;
    for (auto& r : regions) total += r.second - r.first;

    std::vector<TimedWord> all;
    for (auto& [a, b] : regions)
    {
        if (cancelFinal) break;
        std::vector<float> region (timeline.begin() + (long) a, timeline.begin() + (long) b);
        const size_t len = b - a;
        auto words = detector.transcribeLong (region, (double) a / kRate, true,
                                              [&] (float p) { prog = (float) (done + p * len) / (float) std::max<size_t> (total, 1); },
                                              &cancelFinal);
        all.insert (all.end(), words.begin(), words.end());
        done += len;
    }

    if (cancelFinal)
    {
        setState (State::Ready, "Final pass cancelled");
        return;
    }

    // The final pass replaces the live transcript. Hand edits belong after it,
    // which is why the UI warns before re-running it on an edited transcript.
    {
        std::lock_guard<std::mutex> l (wordsMutex);
        committed = std::move (all);
        ++rev;
    }
    cursor = frontier;
    prog = 1.0f;
    setState (State::Ready, "Final pass done");
}

void LiveSession::run()
{
    while (! quit)
    {
        {
            std::unique_lock<std::mutex> l (wakeMutex);
            wake.wait_for (l, std::chrono::milliseconds (25));
        }
        if (quit) break;

        std::optional<DetectorSettings> model;
        std::optional<std::string> lex;
        {
            std::lock_guard<std::mutex> l (reqMutex);
            std::swap (model, pendingModel);
            std::swap (lex, pendingLexicon);
        }

        if (lex) detector.setLexicon (*lex);

        if (model)
        {
            std::string err;
            if (detector.load (*model, err))
            {
                {
                    std::lock_guard<std::mutex> l (msgMutex);
                    loadedModelPath = model->modelPath;
                }
                setState (State::Ready, "Model ready");
            }
            else
                setState (State::Error, err);
        }

        if (wantClear.exchange (false))
        {
            // Throw away anything still in the FIFO from before the clear.
            readIdx.store (writeIdx.load());
            resetTimeline();
        }

        drainFifo();

        if (wantFinal.exchange (false))
        {
            finalPass();
            continue;
        }

        livePass();

        if (detector.isLoaded() && st.load() == State::Transcribing)
            setState (State::Listening, "Listening");
    }
}
} // namespace shabd
