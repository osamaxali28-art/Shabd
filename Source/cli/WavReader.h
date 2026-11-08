#pragma once
// Tiny WAV reader: PCM 16/24/32-bit and 32-bit float, any channel count -> mono.
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace shabd
{
struct WavData
{
    std::vector<float> mono;
    double sampleRate = 0.0;
};

inline bool readWav (const std::string& path, WavData& out, std::string& error)
{
    std::ifstream f (path, std::ios::binary);
    if (! f) { error = "cannot open " + path; return false; }
    std::vector<char> buf ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (buf.size() < 12 || std::memcmp (buf.data(), "RIFF", 4) != 0 || std::memcmp (buf.data() + 8, "WAVE", 4) != 0)
    { error = "not a WAV file"; return false; }

    auto u16 = [&] (size_t o) { return (uint16_t) ((uint8_t) buf[o] | ((uint8_t) buf[o + 1] << 8)); };
    auto u32 = [&] (size_t o) { return (uint32_t) u16 (o) | ((uint32_t) u16 (o + 2) << 16); };

    int format = 0, channels = 0, bits = 0;
    size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= buf.size();)
    {
        const uint32_t len = u32 (o + 4);
        if (std::memcmp (buf.data() + o, "fmt ", 4) == 0 && o + 24 <= buf.size())
        {
            format = u16 (o + 8);
            channels = u16 (o + 10);
            out.sampleRate = u32 (o + 12);
            bits = u16 (o + 22);
            if (format == 0xFFFE && len >= 40) format = u16 (o + 32); // WAVE_FORMAT_EXTENSIBLE sub-format
        }
        else if (std::memcmp (buf.data() + o, "data", 4) == 0)
        {
            dataOff = o + 8;
            dataLen = std::min<size_t> (len, buf.size() - dataOff);
            break;
        }
        o += 8 + len + (len & 1);
    }

    if (dataOff == 0 || channels == 0) { error = "WAV has no audio data"; return false; }
    if (! ((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && bits == 32)))
    { error = "unsupported WAV format (use 16/24/32-bit PCM or 32-bit float)"; return false; }

    const size_t bytes = (size_t) bits / 8;
    const size_t frames = dataLen / (bytes * (size_t) channels);
    out.mono.assign (frames, 0.0f);
    const char* p = buf.data() + dataOff;
    for (size_t i = 0; i < frames; ++i)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c, p += bytes)
        {
            float v = 0.0f;
            if (format == 3) std::memcpy (&v, p, 4);
            else if (bits == 16) v = (int16_t) ((uint8_t) p[0] | ((uint8_t) p[1] << 8)) / 32768.0f;
            else if (bits == 24)
            {
                int32_t s = ((uint8_t) p[0]) | ((uint8_t) p[1] << 8) | ((int8_t) p[2] * 65536);
                v = (float) s / 8388608.0f;
            }
            else
            {
                int32_t s;
                std::memcpy (&s, p, 4);
                v = (float) s / 2147483648.0f;
            }
            sum += v;
        }
        out.mono[i] = sum / (float) channels;
    }
    return true;
}
} // namespace shabd
