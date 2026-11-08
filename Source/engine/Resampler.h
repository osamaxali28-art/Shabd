#pragma once
// Streaming resampler to 16 kHz mono for Whisper.
// Windowed-sinc low-pass (anti-alias at ~7.4 kHz) followed by linear
// interpolation. Plenty for speech recognition, cheap enough for a plugin.
#include <cmath>
#include <vector>

namespace shabd
{
class StreamResampler
{
public:
    static constexpr double kTargetRate = 16000.0;

    void reset (double sourceRate)
    {
        srcRate = sourceRate;
        step = srcRate / kTargetRate;
        pos = 0.0;
        designFilter();
        history.assign (taps.size(), 0.0f);
        histIndex = 0;
        prevFiltered = 0.0f;
        haveFiltered = false;
    }

    double sourceRate() const { return srcRate; }

    // Appends resampled output to `out`.
    void process (const float* in, int n, std::vector<float>& out)
    {
        if (srcRate <= 0.0) return;

        if (std::abs (srcRate - kTargetRate) < 1.0) // already 16 kHz
        {
            out.insert (out.end(), in, in + n);
            return;
        }

        const size_t nt = taps.size();
        for (int i = 0; i < n; ++i)
        {
            history[histIndex] = in[i];
            histIndex = (histIndex + 1) % nt;

            float y = 0.0f;
            size_t h = histIndex;
            for (size_t k = 0; k < nt; ++k)
            {
                y += taps[k] * history[h];
                h = (h + 1) % nt;
            }

            if (! haveFiltered) { prevFiltered = y; haveFiltered = true; continue; }

            // Output samples fall between prevFiltered (t-1) and y (t).
            while (pos <= 1.0)
            {
                out.push_back (prevFiltered + (float) pos * (y - prevFiltered));
                pos += step;
            }
            pos -= 1.0;
            prevFiltered = y;
        }
    }

private:
    double srcRate = 0.0, step = 1.0, pos = 0.0;
    std::vector<float> taps, history;
    size_t histIndex = 0;
    float prevFiltered = 0.0f;
    bool haveFiltered = false;

    void designFilter()
    {
        const int n = 63;
        taps.assign ((size_t) n, 0.0f);
        const double cutoff = std::min (0.4625 * kTargetRate, 0.45 * srcRate) / srcRate; // normalised
        const double pi = 3.14159265358979323846;
        double sum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double m = i - (n - 1) / 2.0;
            const double sinc = (i == (n - 1) / 2) ? 2.0 * cutoff : std::sin (2.0 * pi * cutoff * m) / (pi * m);
            const double win = 0.42 - 0.5 * std::cos (2.0 * pi * i / (n - 1)) + 0.08 * std::cos (4.0 * pi * i / (n - 1));
            taps[(size_t) i] = (float) (sinc * win);
            sum += taps[(size_t) i];
        }
        for (auto& t : taps) t = (float) (t / sum);
    }
};
} // namespace shabd
