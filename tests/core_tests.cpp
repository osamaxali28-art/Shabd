// Shabd core tests: Roman Urdu conversion + subtitle export.
// Build: cmake -B build -DSHABD_CORE_ONLY=ON && cmake --build build && ctest --test-dir build
#include "../Source/core/RomanUrdu.h"
#include "../Source/core/Subtitles.h"
#include "../Source/core/Utf8.h"
#include "../Source/engine/Resampler.h"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;
static int checks = 0;

#define EXPECT_EQ(actual, expected)                                                              \
    do {                                                                                         \
        ++checks;                                                                                \
        const auto a_ = (actual);                                                                \
        const auto e_ = (expected);                                                              \
        if (! (a_ == e_)) {                                                                      \
            ++failures;                                                                          \
            std::printf ("FAIL %s:%d\n  got:      [%s]\n  expected: [%s]\n", __FILE__, __LINE__, \
                         std::string (a_).c_str(), std::string (e_).c_str());                    \
        }                                                                                        \
    } while (0)

#define EXPECT_TRUE(cond)                                                    \
    do {                                                                     \
        ++checks;                                                            \
        if (! (cond)) { ++failures; std::printf ("FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

static std::string readFile (const std::string& path)
{
    std::ifstream f (path, std::ios::binary);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

int main (int argc, char** argv)
{
    const std::string lexPath = argc > 1 ? argv[1] : "lexicon/roman_urdu.tsv";
    shabd::RomanUrdu ru;
    const int n = ru.loadLexicon (readFile (lexPath));
    std::printf ("lexicon entries: %d\n", n);
    EXPECT_TRUE (n > 250);

    // --- Urdu script (what Whisper outputs with language=ur) ---
    EXPECT_EQ (ru.convertToString ("یہ فلو بہت ریئل ہے"), "yeh flow bohat real hai");
    EXPECT_EQ (ru.convertToString ("گھر میں"), "ghar mein");
    EXPECT_EQ (ru.convertToString ("میں نہیں رکا"), "main nahi raka"); // "رکا" is a fallback guess
    EXPECT_EQ (ru.convertToString ("تم نے کیا کیا"), "tum ne kya kiya");
    EXPECT_EQ (ru.convertToString ("اس نے کیا ہے"), "us ne kiya hai");
    EXPECT_EQ (ru.convertToString ("میں نے کیا کہا"), "main ne kya kaha");
    EXPECT_EQ (ru.convertToString ("کیا ہے"), "kya hai");
    EXPECT_EQ (ru.convertToString ("تو ہے"), "tu hai");
    EXPECT_EQ (ru.convertToString ("پھر تو"), "phir toh");
    EXPECT_EQ (ru.convertToString ("اسلام آباد سے لاہور"), "Islamabad se Lahore");
    EXPECT_EQ (ru.convertToString ("یار، سن!"), "yaar sun");
    EXPECT_EQ (ru.convertToString ("مجھے پیسے نہیں چاہیے"), "mujhe paise nahi chahiye");

    // Arabic-script code points that Whisper sometimes emits (Arabic yeh U+064A, heh U+0647)
    EXPECT_EQ (ru.convertToString (shabd::utf8::encode (U"نہيں")), "nahi");
    EXPECT_EQ (ru.convertToString (shabd::utf8::encode (U"هے")), "hai");

    // Vowel marks disambiguate when present
    EXPECT_EQ (ru.convertToString ("تُو"), "tu");

    // --- Roman output (fine-tuned model) gets cleaned to one spelling ---
    EXPECT_EQ (ru.convertToString ("Ye flow bahut real hy"), "yeh flow bohat real hai");
    EXPECT_EQ (ru.convertToString ("nai yar kia scene hai"), "nahi yaar kya scene hai");
    // English words that look like variants are left alone
    EXPECT_EQ (ru.convertToString ("say it to the day"), "say it to the day");
    EXPECT_EQ (ru.convertToString ("i got bars"), "I got bars");
    EXPECT_EQ (ru.convertToString ("pindi lahore karachi"), "Pindi Lahore Karachi");

    // --- per-word conversion keeps context and source mapping ---
    {
        auto w = ru.convertWords ({ " اسلام", " آباد", " میں", " ہوں" });
        EXPECT_TRUE (w.size() == 3);
        if (w.size() == 3)
        {
            EXPECT_EQ (w[0].text, "Islamabad");
            EXPECT_TRUE (w[0].source == 0 && w[0].sourceCount == 2);
            EXPECT_EQ (w[1].text, "main"); // followed by "hoon" -> I
            EXPECT_TRUE (w[1].source == 2 && w[1].sourceCount == 1);
            EXPECT_EQ (w[2].text, "hoon");
        }
        auto g = ru.convertWords ({ "گھر", "میں", "بیٹھا" });
        EXPECT_TRUE (g.size() == 3 && g[1].text == "mein");
    }

    // --- fallback transliteration for unknown words ---
    {
        auto words = ru.convertLine ("سوچا بھاگ");
        EXPECT_TRUE (words.size() == 2);
        if (words.size() == 2)
        {
            EXPECT_EQ (words[0].text, "socha");
            EXPECT_EQ (words[1].text, "bhaag");
            EXPECT_TRUE (words[0].guessed && words[1].guessed);
        }
        auto known = ru.convertLine ("دل");
        EXPECT_TRUE (known.size() == 1 && ! known[0].guessed);
    }

    // --- subtitles ---
    EXPECT_EQ (shabd::formatSrtTime (3723.456), "01:02:03,456");
    EXPECT_EQ (shabd::formatVttTime (62.5), "00:01:02.500");
    EXPECT_EQ (shabd::formatLrcTime (62.345), "01:02.35");

    std::vector<shabd::TimedWord> words = {
        { 1.00, 1.30, "yeh" }, { 1.30, 1.60, "flow" }, { 1.60, 2.00, "bohat" }, { 2.00, 2.40, "real" }, { 2.40, 2.70, "hai" },
        // 1 s pause -> new line
        { 3.70, 4.00, "Lahore" }, { 4.00, 4.40, "se" }, { 4.40, 4.90, "Pindi", 0.3f },
    };
    auto lines = shabd::buildLines (words);
    EXPECT_TRUE (lines.size() == 2);
    if (lines.size() == 2)
    {
        EXPECT_EQ (lines[0].text(), "Yeh flow bohat real hai");
        EXPECT_EQ (lines[1].text(), "Lahore se Pindi");
        EXPECT_TRUE (lines[0].end <= lines[1].start);
        EXPECT_TRUE (lines[1].words[2].needsReview());
    }

    const auto srt = shabd::toSRT (lines);
    EXPECT_EQ (srt.substr (0, srt.find ("\n\n")), std::string ("1\n00:00:01,000 --> 00:00:02,950\nYeh flow bohat real hai"));

    shabd::ExportInfo info { "Test", "Moji" };
    const auto lrc = shabd::toLRC (lines, info);
    EXPECT_TRUE (lrc.find ("[00:01.00]Yeh flow bohat real hai") != std::string::npos);
    EXPECT_TRUE (lrc.find ("[ar:Moji]") != std::string::npos);

    const auto elrc = shabd::toLRC (lines, info, true);
    EXPECT_TRUE (elrc.find ("[00:03.70]<00:03.70> Lahore<00:04.00> se<00:04.40> Pindi <00:04.90>") != std::string::npos);

    const auto vtt = shabd::toVTT (lines);
    EXPECT_TRUE (vtt.rfind ("WEBVTT", 0) == 0);

    const auto ttml = shabd::toTTML (lines, info);
    EXPECT_TRUE (ttml.find ("xml:lang=\"ur-Latn\"") != std::string::npos);
    EXPECT_TRUE (ttml.find ("<span begin=\"00:00:03.700\" end=\"00:00:04.000\">Lahore</span>") != std::string::npos);

    // long line splits at 42 chars
    std::vector<shabd::TimedWord> many;
    for (int i = 0; i < 20; ++i) many.push_back ({ i * 0.2, i * 0.2 + 0.2, "bohat" });
    for (auto& l : shabd::buildLines (many)) EXPECT_TRUE (l.text().size() <= 42);

    // --- resampler: 48 kHz -> 16 kHz keeps speech band, removes what would alias ---
    {
        auto toneRms = [] (double freq)
        {
            shabd::StreamResampler rs;
            rs.reset (48000.0);
            std::vector<float> in (48000), out;
            for (size_t i = 0; i < in.size(); ++i) in[i] = (float) std::sin (2.0 * 3.14159265358979 * freq * (double) i / 48000.0);
            // feed in uneven blocks like a DAW would
            for (size_t off = 0; off < in.size(); off += 517)
                rs.process (in.data() + off, (int) std::min<size_t> (517, in.size() - off), out);
            double sum = 0.0;
            for (size_t i = 200; i < out.size(); ++i) sum += (double) out[i] * out[i];
            return std::make_pair (out.size(), std::sqrt (sum / (double) (out.size() - 200)));
        };
        const auto [len, speechRms] = toneRms (1000.0);
        const auto [len2, aliasRms] = toneRms (10000.0);
        std::printf ("resampler: %zu samples, 1 kHz rms %.3f, 10 kHz rms %.4f\n", len, speechRms, aliasRms);
        EXPECT_TRUE (len > 15990 && len < 16010);
        EXPECT_TRUE (speechRms > 0.68 && speechRms < 0.72);
        EXPECT_TRUE (aliasRms < 0.01);
        (void) len2;
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
