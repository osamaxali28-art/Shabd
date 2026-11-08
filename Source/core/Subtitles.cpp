#include "Subtitles.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace shabd
{
namespace
{
long long toMillis (double s) { return (long long) std::llround (std::max (0.0, s) * 1000.0); }

std::string xmlEscape (const std::string& s)
{
    std::string o;
    for (char c : s)
    {
        switch (c)
        {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            default: o += c;
        }
    }
    return o;
}

void capitaliseFirst (std::string& s)
{
    for (auto& c : s)
    {
        if (std::isalpha ((unsigned char) c)) { c = (char) std::toupper ((unsigned char) c); return; }
        if (c != '(' && c != '"' && c != '\'') return;
    }
}

constexpr double kVerseBreak = 2.5; // pause that becomes a blank line in plain lyrics
} // namespace

std::string CaptionLine::text() const
{
    std::string s;
    for (auto& w : words)
    {
        if (! s.empty()) s += ' ';
        s += w.text;
    }
    return s;
}

std::vector<CaptionLine> buildLines (const std::vector<TimedWord>& input, const LineRules& rules)
{
    std::vector<TimedWord> words;
    words.reserve (input.size());
    for (auto& w : input)
        if (! w.text.empty()) words.push_back (w);

    std::stable_sort (words.begin(), words.end(), [] (const TimedWord& a, const TimedWord& b) { return a.start < b.start; });

    std::vector<CaptionLine> lines;
    CaptionLine cur;
    size_t curChars = 0;

    auto flush = [&]
    {
        if (cur.words.empty()) return;
        cur.start = cur.words.front().start;
        cur.end = cur.words.back().end;
        lines.push_back (cur);
        cur = {};
        curChars = 0;
    };

    for (auto& w : words)
    {
        if (! cur.words.empty())
        {
            const double gap = w.start - cur.words.back().end;
            const size_t newChars = curChars + 1 + w.text.size();
            const double newDur = w.end - cur.words.front().start;
            if (gap > rules.maxGap || newChars > rules.maxChars || newDur > rules.maxDuration)
                flush();
        }
        curChars += (cur.words.empty() ? 0 : 1) + w.text.size();
        cur.words.push_back (w);
    }
    flush();

    // Tidy timings: no overlaps, a small tail so short lines are readable.
    for (size_t i = 0; i < lines.size(); ++i)
    {
        auto& l = lines[i];
        double end = std::max (l.end, l.start + 0.4) + 0.25;
        if (i + 1 < lines.size()) end = std::min (end, lines[i + 1].start);
        l.end = std::max (end, l.start + 0.1);

        if (rules.capitaliseLineStart && ! l.words.empty())
            capitaliseFirst (l.words.front().text);
    }
    return lines;
}

std::string formatSrtTime (double seconds)
{
    const auto ms = toMillis (seconds);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%02lld:%02lld:%02lld,%03lld",
                   ms / 3600000, (ms / 60000) % 60, (ms / 1000) % 60, ms % 1000);
    return buf;
}

std::string formatVttTime (double seconds)
{
    auto s = formatSrtTime (seconds);
    s[8] = '.';
    return s;
}

std::string formatLrcTime (double seconds)
{
    const auto cs = (long long) std::llround (std::max (0.0, seconds) * 100.0);
    char buf[32];
    std::snprintf (buf, sizeof (buf), "%02lld:%02lld.%02lld", cs / 6000, (cs / 100) % 60, cs % 100);
    return buf;
}

std::string toSRT (const std::vector<CaptionLine>& lines)
{
    std::string out;
    int n = 1;
    for (auto& l : lines)
    {
        out += std::to_string (n++) + "\n";
        out += formatSrtTime (l.start) + " --> " + formatSrtTime (l.end) + "\n";
        out += l.text() + "\n\n";
    }
    return out;
}

std::string toVTT (const std::vector<CaptionLine>& lines, bool wordTimings)
{
    std::string out = "WEBVTT\nKind: captions\nLanguage: ur\n\n";
    for (auto& l : lines)
    {
        out += formatVttTime (l.start) + " --> " + formatVttTime (l.end) + "\n";
        if (wordTimings)
        {
            for (size_t i = 0; i < l.words.size(); ++i)
            {
                if (i > 0) out += "<" + formatVttTime (l.words[i].start) + "><c> " + l.words[i].text + "</c>";
                else out += l.words[i].text;
            }
            out += "\n\n";
        }
        else
        {
            out += l.text() + "\n\n";
        }
    }
    return out;
}

std::string toLRC (const std::vector<CaptionLine>& lines, const ExportInfo& info, bool enhanced)
{
    std::string out;
    if (! info.title.empty())  out += "[ti:" + info.title + "]\n";
    if (! info.artist.empty()) out += "[ar:" + info.artist + "]\n";
    out += "[re:Shabd]\n";

    for (auto& l : lines)
    {
        out += "[" + formatLrcTime (l.start) + "]";
        if (enhanced)
        {
            for (size_t i = 0; i < l.words.size(); ++i)
            {
                out += "<" + formatLrcTime (l.words[i].start) + "> " + l.words[i].text;
                if (i + 1 == l.words.size()) out += " <" + formatLrcTime (l.words[i].end) + ">";
            }
        }
        else
        {
            out += l.text();
        }
        out += "\n";
    }
    return out;
}

std::string toTTML (const std::vector<CaptionLine>& lines, const ExportInfo& info)
{
    auto t = [] (double s) { return formatVttTime (s); };
    std::string out;
    out += "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    out += "<tt xmlns=\"http://www.w3.org/ns/ttml\" xmlns:ttm=\"http://www.w3.org/ns/ttml#metadata\" "
           "xmlns:itunes=\"http://music.apple.com/lyric-ttml-internal\" itunes:timing=\"Word\" xml:lang=\""
         + xmlEscape (info.language) + "\">\n";
    out += "  <head>\n    <metadata>\n";
    if (! info.title.empty())  out += "      <ttm:title>" + xmlEscape (info.title) + "</ttm:title>\n";
    if (! info.artist.empty()) out += "      <ttm:agent type=\"person\" xml:id=\"v1\"><ttm:name type=\"full\">" + xmlEscape (info.artist) + "</ttm:name></ttm:agent>\n";
    out += "    </metadata>\n  </head>\n  <body>\n";

    if (! lines.empty())
    {
        out += "    <div begin=\"" + t (lines.front().start) + "\" end=\"" + t (lines.back().end) + "\">\n";
        for (auto& l : lines)
        {
            out += "      <p begin=\"" + t (l.start) + "\" end=\"" + t (l.end) + "\">";
            for (size_t i = 0; i < l.words.size(); ++i)
            {
                auto& w = l.words[i];
                out += "<span begin=\"" + t (w.start) + "\" end=\"" + t (w.end) + "\">" + xmlEscape (w.text) + "</span>";
                if (i + 1 < l.words.size()) out += " ";
            }
            out += "</p>\n";
        }
        out += "    </div>\n";
    }
    out += "  </body>\n</tt>\n";
    return out;
}

std::string toPlainLyrics (const std::vector<CaptionLine>& lines)
{
    std::string out;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        if (i > 0 && lines[i].start - lines[i - 1].words.back().end > kVerseBreak) out += "\n";
        out += lines[i].text() + "\n";
    }
    return out;
}
} // namespace shabd
