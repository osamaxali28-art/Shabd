#include "Detector.h"

#include <whisper.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace shabd
{
namespace
{
constexpr double kRate = 16000.0;

struct RawWord
{
    double t0 = 0.0, t1 = 0.0;
    std::string text;
    float prob = 1.0f;
};

bool isNoise (const std::string& s)
{
    // Whisper non-speech tags: [Music], (applause), ♪ ...
    bool anyLetter = false;
    for (unsigned char c : s)
    {
        if (c == '[' || c == ']') return true;
        if (c >= 0x80 || std::isalnum (c)) anyLetter = true;
    }
    if (s.find ("\xE2\x99\xAA") != std::string::npos) return true; // ♪
    return ! anyLetter;
}

void quietLogger (enum ggml_log_level, const char*, void*) {}
} // namespace

Detector::Detector() = default;
Detector::~Detector() { unload(); }

bool Detector::load (const DetectorSettings& s, std::string& error)
{
    unload();
    cfg = s;
    whisper_log_set (quietLogger, nullptr);

    auto cparams = whisper_context_default_params();
    cparams.use_gpu = s.useGpu;
    cparams.flash_attn = s.useGpu;

    ctx = whisper_init_from_file_with_params (s.modelPath.c_str(), cparams);
    if (ctx == nullptr)
    {
        error = "Could not load model: " + s.modelPath;
        return false;
    }
    return true;
}

void Detector::unload()
{
    if (ctx != nullptr) whisper_free (ctx);
    ctx = nullptr;
}

float Detector::rmsDb (const float* x, size_t n)
{
    if (n == 0) return -120.0f;
    double sum = 0.0;
    for (size_t i = 0; i < n; ++i) sum += (double) x[i] * x[i];
    const double rms = std::sqrt (sum / (double) n);
    return (float) (20.0 * std::log10 (std::max (rms, 1e-6)));
}

std::vector<TimedWord> Detector::transcribe (const float* samples, size_t n, double offsetSec, bool accurate)
{
    std::vector<TimedWord> result;
    if (ctx == nullptr || n == 0) return result;

    // Whisper wants at least ~1 s; pad short phrases with silence.
    std::vector<float> padded;
    const size_t minLen = (size_t) (1.1 * kRate);
    if (n < minLen)
    {
        padded.assign (samples, samples + n);
        padded.resize (minLen, 0.0f);
        samples = padded.data();
        n = padded.size();
    }

    auto p = whisper_full_default_params (WHISPER_SAMPLING_BEAM_SEARCH);
    p.n_threads = cfg.threads;
    p.beam_search.beam_size = accurate ? cfg.accurateBeamSize : cfg.liveBeamSize;
    p.language = cfg.language.c_str();
    p.detect_language = false;
    p.translate = false;

    // Each window is independent: stops one bad guess snowballing into a
    // repeated hallucination, a common Whisper failure on dense rap verses.
    p.no_context = true;
    p.initial_prompt = cfg.prompt.empty() ? nullptr : cfg.prompt.c_str();
    p.carry_initial_prompt = true;

    // Word-level output: one segment per word, with timestamps.
    p.token_timestamps = true;
    p.max_len = 1;
    p.split_on_word = true;

    p.suppress_blank = true;
    p.suppress_nst = true; // no "[Music]" tokens
    p.temperature = 0.0f;
    p.temperature_inc = 0.2f; // fall back to sampling if the beam is unsure
    p.no_speech_thold = cfg.noSpeechThreshold;

    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;

    if (whisper_full (ctx, p, samples, (int) n) != 0) return result;

    const auto eot = whisper_token_eot (ctx);
    std::vector<RawWord> raw;
    const int segs = whisper_full_n_segments (ctx);
    for (int s = 0; s < segs; ++s)
    {
        RawWord w;
        w.text = whisper_full_get_segment_text (ctx, s);
        if (w.text.empty() || isNoise (w.text)) continue;

        w.t0 = offsetSec + (double) whisper_full_get_segment_t0 (ctx, s) / 100.0;
        w.t1 = offsetSec + (double) whisper_full_get_segment_t1 (ctx, s) / 100.0;

        float psum = 0.0f;
        int pn = 0;
        const int nt = whisper_full_n_tokens (ctx, s);
        for (int t = 0; t < nt; ++t)
        {
            if (whisper_full_get_token_id (ctx, s, t) >= eot) continue; // special / timestamp tokens
            psum += whisper_full_get_token_p (ctx, s, t);
            ++pn;
        }
        w.prob = pn > 0 ? psum / (float) pn : 0.0f;

        // Hallucination guard: the same word more than 4 times in a row is
        // almost always Whisper looping, not an ad-lib.
        int repeats = 0;
        for (auto it = raw.rbegin(); it != raw.rend() && it->text == w.text; ++it) ++repeats;
        if (repeats >= 4) continue;

        raw.push_back (w);
    }

    // Convert to Roman Urdu with full sentence context.
    std::vector<std::string> texts;
    texts.reserve (raw.size());
    for (auto& r : raw) texts.push_back (r.text);
    const auto converted = roman.convertWords (texts);

    // Several output words from one input word share its time span evenly.
    std::map<int, int> perSource, seenSource;
    for (auto& c : converted) ++perSource[c.source];

    for (auto& c : converted)
    {
        const auto& first = raw[(size_t) c.source];
        const auto& last = raw[(size_t) (c.source + c.sourceCount - 1)];
        TimedWord tw;
        tw.text = c.text;
        tw.guessed = c.guessed;
        tw.start = first.t0;
        tw.end = last.t1;
        tw.prob = std::min (first.prob, last.prob);

        const int parts = perSource[c.source];
        if (parts > 1)
        {
            const int k = seenSource[c.source]++;
            const double span = (tw.end - tw.start) / parts;
            tw.start = first.t0 + span * k;
            tw.end = tw.start + span;
        }
        result.push_back (tw);
    }
    return result;
}

std::vector<std::pair<size_t, size_t>> Detector::planChunks (const float* x, size_t n, double maxChunkSec)
{
    std::vector<std::pair<size_t, size_t>> chunks;
    const size_t maxLen = (size_t) (maxChunkSec * kRate);
    const size_t searchLen = (size_t) (4.0 * kRate);
    const size_t frame = (size_t) (0.1 * kRate);

    size_t start = 0;
    while (start < n)
    {
        if (n - start <= maxLen)
        {
            chunks.emplace_back (start, n);
            break;
        }
        const size_t target = start + maxLen;
        size_t best = target;
        float bestDb = 1e9f;
        for (size_t f = target - searchLen; f + frame <= target; f += frame / 2)
        {
            const float db = rmsDb (x + f, frame);
            if (db < bestDb) { bestDb = db; best = f + frame / 2; }
        }
        chunks.emplace_back (start, best);
        start = best;
    }
    return chunks;
}

std::vector<TimedWord> Detector::transcribeLong (const std::vector<float>& audio, double offsetSec, bool accurate,
                                                 const Progress& progress, const std::atomic<bool>* cancel)
{
    std::vector<TimedWord> all;
    const auto chunks = planChunks (audio.data(), audio.size());
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        if (cancel != nullptr && cancel->load()) break;
        const auto [a, b] = chunks[i];
        if (rmsDb (audio.data() + a, b - a) > -55.0f) // skip silent stretches
        {
            auto words = transcribe (audio.data() + a, b - a, offsetSec + (double) a / kRate, accurate);
            all.insert (all.end(), words.begin(), words.end());
        }
        if (progress) progress ((float) (i + 1) / (float) chunks.size());
    }
    return all;
}
} // namespace shabd
