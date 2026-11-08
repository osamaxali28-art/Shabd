#pragma once
// Caption building + export formats: SRT, WebVTT (YouTube), LRC / enhanced LRC
// (Spotify via Musixmatch, Apple Music, most lyric sites), TTML (Apple Music /
// Spotify distributor uploads) and plain lyrics text.
#include <string>
#include <vector>

namespace shabd
{
struct TimedWord
{
    double start = 0.0; // seconds on the song timeline
    double end = 0.0;
    std::string text;   // Roman Urdu
    float prob = 1.0f;  // model confidence 0..1
    bool guessed = false;

    bool needsReview() const { return guessed || prob < 0.5f; }
};

struct CaptionLine
{
    double start = 0.0;
    double end = 0.0;
    std::vector<TimedWord> words;

    std::string text() const;
};

struct LineRules
{
    double maxGap = 0.35;      // pause that starts a new line (seconds)
    size_t maxChars = 42;      // YouTube / Netflix-style limit per line
    double maxDuration = 6.0;  // longest a single line stays on screen
    bool capitaliseLineStart = true;
};

std::vector<CaptionLine> buildLines (const std::vector<TimedWord>& words, const LineRules& rules = {});

struct ExportInfo
{
    std::string title;
    std::string artist;
    std::string language = "ur-Latn"; // BCP-47: Urdu in Latin script
};

std::string toSRT (const std::vector<CaptionLine>& lines);
std::string toVTT (const std::vector<CaptionLine>& lines, bool wordTimings = false);
std::string toLRC (const std::vector<CaptionLine>& lines, const ExportInfo& info, bool enhanced = false);
std::string toTTML (const std::vector<CaptionLine>& lines, const ExportInfo& info);
std::string toPlainLyrics (const std::vector<CaptionLine>& lines);

// Time formatting helpers (exposed for tests).
std::string formatSrtTime (double seconds); // 00:01:02,345
std::string formatVttTime (double seconds); // 00:01:02.345
std::string formatLrcTime (double seconds); // 01:02.34
} // namespace shabd
