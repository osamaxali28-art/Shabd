// shabd-cli: run the Shabd detector on a bounced vocal and write captions.
//
//   shabd-cli vocal.wav --model ggml-large-v3-turbo-q5_0.bin --title "Song" --artist "Moji"
//
// Writes vocal.srt, vocal.vtt, vocal.lrc, vocal.words.lrc, vocal.ttml, vocal.txt
#include "../core/LexiconData.h"
#include "../engine/Detector.h"
#include "../engine/Resampler.h"
#include "WavReader.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

using namespace shabd;

static void usage()
{
    std::puts ("usage: shabd-cli <vocal.wav> --model <ggml model.bin> [options]\n"
               "  --out <base>        output path without extension (default: next to the wav)\n"
               "  --title <text>      song title for LRC / TTML\n"
               "  --artist <text>     artist name for LRC / TTML\n"
               "  --language <code>   ur (default), hi, pa or auto\n"
               "  --threads <n>       CPU threads (default: all cores)\n"
               "  --lexicon <file>    use this roman_urdu.tsv instead of the built-in one\n"
               "  --offset <seconds>  where the wav starts on the song timeline (default 0)\n"
               "  --prompt <text>     vocabulary hint for the model (default: Urdu rap words)\n"
               "  --cpu               don't use the GPU");
}

static bool writeText (const std::string& path, const std::string& text)
{
    std::ofstream f (path, std::ios::binary);
    f << text;
    std::printf ("  wrote %s\n", path.c_str());
    return (bool) f;
}

int main (int argc, char** argv)
{
    if (argc < 2) { usage(); return 1; }

    std::string wavPath, outBase, lexPath;
    DetectorSettings settings;
    settings.threads = (int) std::max (1u, std::thread::hardware_concurrency());
    ExportInfo info;
    double offset = 0.0;

    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--model") settings.modelPath = next();
        else if (a == "--out") outBase = next();
        else if (a == "--title") info.title = next();
        else if (a == "--artist") info.artist = next();
        else if (a == "--language") settings.language = next();
        else if (a == "--threads") settings.threads = std::atoi (next().c_str());
        else if (a == "--lexicon") lexPath = next();
        else if (a == "--offset") offset = std::atof (next().c_str());
        else if (a == "--cpu") settings.useGpu = false;
        else if (a == "--prompt") settings.prompt = next();
        else if (a == "-h" || a == "--help") { usage(); return 0; }
        else if (! a.empty() && a[0] != '-') wavPath = a;
        else { std::printf ("unknown option %s\n", a.c_str()); usage(); return 1; }
    }
    if (wavPath.empty() || settings.modelPath.empty()) { usage(); return 1; }
    if (settings.language == "en") settings.prompt.clear(); // the default hint is Urdu rap vocabulary
    if (outBase.empty())
    {
        const auto dot = wavPath.find_last_of ('.');
        outBase = dot == std::string::npos ? wavPath : wavPath.substr (0, dot);
    }

    WavData wav;
    std::string err;
    if (! readWav (wavPath, wav, err)) { std::printf ("error: %s\n", err.c_str()); return 1; }
    std::printf ("read %s: %.1f s at %.0f Hz\n", wavPath.c_str(), wav.mono.size() / wav.sampleRate, wav.sampleRate);

    StreamResampler rs;
    rs.reset (wav.sampleRate);
    std::vector<float> audio16;
    audio16.reserve ((size_t) (wav.mono.size() * 16000.0 / wav.sampleRate) + 16);
    rs.process (wav.mono.data(), (int) wav.mono.size(), audio16);

    Detector det;
    if (lexPath.empty()) det.setLexicon (shabdLexiconTsv());
    else
    {
        std::ifstream lf (lexPath, std::ios::binary);
        std::stringstream ss;
        ss << lf.rdbuf();
        det.setLexicon (ss.str());
    }
    std::printf ("lexicon: %d words\n", det.lexiconEntries());

    if (! det.load (settings, err)) { std::printf ("error: %s\n", err.c_str()); return 1; }
    std::printf ("model: %s\n", settings.modelPath.c_str());

    auto words = det.transcribeLong (audio16, offset, true, [] (float p) {
        std::printf ("\r  transcribing %3d%%", (int) (p * 100.0f));
        std::fflush (stdout);
    });
    std::printf ("\n%zu words\n", words.size());

    const auto lines = buildLines (words);
    writeText (outBase + ".srt", toSRT (lines));
    writeText (outBase + ".vtt", toVTT (lines));
    writeText (outBase + ".lrc", toLRC (lines, info, false));
    writeText (outBase + ".words.lrc", toLRC (lines, info, true));
    writeText (outBase + ".ttml", toTTML (lines, info));
    writeText (outBase + ".txt", toPlainLyrics (lines));

    int review = 0;
    for (auto& w : words)
        if (w.needsReview()) ++review;
    std::printf ("%d of %zu words need a check (unknown spelling or low confidence)\n", review, words.size());
    return 0;
}
