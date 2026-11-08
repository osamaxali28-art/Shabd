#pragma once
// Minimal UTF-8 <-> UTF-32 helpers (no dependencies).
#include <string>

namespace shabd::utf8
{
inline std::u32string decode (const std::string& s)
{
    std::u32string out;
    out.reserve (s.size());
    for (size_t i = 0; i < s.size();)
    {
        const auto c = static_cast<unsigned char> (s[i]);
        char32_t cp = 0;
        int extra = 0;
        if (c < 0x80)            { cp = c; }
        else if ((c >> 5) == 6)  { cp = c & 0x1F; extra = 1; }
        else if ((c >> 4) == 14) { cp = c & 0x0F; extra = 2; }
        else if ((c >> 3) == 30) { cp = c & 0x07; extra = 3; }
        else                     { ++i; continue; } // invalid lead byte: skip

        if (extra > 0 && i + (size_t) extra >= s.size())
            break; // truncated sequence at end of string

        bool ok = true;
        for (int k = 1; k <= extra; ++k)
        {
            const auto cc = static_cast<unsigned char> (s[i + (size_t) k]);
            if ((cc >> 6) != 2) { ok = false; break; }
            cp = (cp << 6) | (cc & 0x3F);
        }
        i += (size_t) extra + 1;
        if (ok) out.push_back (cp);
    }
    return out;
}

inline void append (std::string& out, char32_t cp)
{
    if (cp < 0x80) out += static_cast<char> (cp);
    else if (cp < 0x800)
    {
        out += static_cast<char> (0xC0 | (cp >> 6));
        out += static_cast<char> (0x80 | (cp & 0x3F));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char> (0xE0 | (cp >> 12));
        out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char> (0x80 | (cp & 0x3F));
    }
    else
    {
        out += static_cast<char> (0xF0 | (cp >> 18));
        out += static_cast<char> (0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char> (0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char> (0x80 | (cp & 0x3F));
    }
}

inline std::string encode (const std::u32string& s)
{
    std::string out;
    for (auto cp : s) append (out, cp);
    return out;
}
} // namespace shabd::utf8
