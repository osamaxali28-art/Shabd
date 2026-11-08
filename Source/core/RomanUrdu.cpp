#include "RomanUrdu.h"
#include "Utf8.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace shabd
{
namespace
{
bool isUrduChar (char32_t c)
{
    return (c >= 0x0600 && c <= 0x06FF) || (c >= 0x0750 && c <= 0x077F)
        || (c >= 0xFB50 && c <= 0xFDFF) || (c >= 0xFE70 && c <= 0xFEFF);
}

bool isDiacritic (char32_t c)
{
    return (c >= 0x064B && c <= 0x065F) || c == 0x0670;
}

bool isSeparator (char32_t c)
{
    if (c == U' ' || c == U'\t' || c == U'\n' || c == U'\r') return true;
    // Urdu / Arabic punctuation: ۔ ، ؟ ؛ ٫ ٬
    if (c == 0x06D4 || c == 0x060C || c == 0x061F || c == 0x061B || c == 0x066B || c == 0x066C) return true;
    if (c < 0x80)
    {
        if (c == U'\'') return false; // keep apostrophes inside English words
        return std::ispunct ((int) c) != 0;
    }
    if (c == 0x2018 || c == 0x2019 || c == 0x201C || c == 0x201D || c == 0x2026 || c == 0x2014 || c == 0x2013)
        return true;
    return false;
}

std::string toLowerAscii (std::string s)
{
    for (auto& ch : s)
        ch = (char) std::tolower ((unsigned char) ch);
    return s;
}

std::string trim (const std::string& s)
{
    const auto b = s.find_first_not_of (" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of (" \t\r\n");
    return s.substr (b, e - b + 1);
}

std::vector<std::string> split (const std::string& s, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    for (char ch : s)
    {
        if (ch == sep) { out.push_back (cur); cur.clear(); }
        else cur += ch;
    }
    out.push_back (cur);
    return out;
}

bool hasUrdu (const std::u32string& t)
{
    return std::any_of (t.begin(), t.end(), isUrduChar);
}

// Letters that behave as consonants in the fallback transliterator.
const char* consonant (char32_t c)
{
    switch (c)
    {
        case U'ب': return "b";  case U'پ': return "p";  case U'ت': return "t";  case U'ٹ': return "t";
        case U'ث': return "s";  case U'ج': return "j";  case U'چ': return "ch"; case U'ح': return "h";
        case U'خ': return "kh"; case U'د': return "d";  case U'ڈ': return "d";  case U'ذ': return "z";
        case U'ر': return "r";  case U'ڑ': return "r";  case U'ز': return "z";  case U'ژ': return "zh";
        case U'س': return "s";  case U'ش': return "sh"; case U'ص': return "s";  case U'ض': return "z";
        case U'ط': return "t";  case U'ظ': return "z";  case U'غ': return "gh"; case U'ف': return "f";
        case U'ق': return "q";  case U'ک': return "k";  case U'گ': return "g";  case U'ل': return "l";
        case U'م': return "m";  case U'ن': return "n";  case U'ہ': return "h";
        default: return nullptr;
    }
}

// Context lists (diacritics stripped).
const std::u32string kMain   = U"میں";
const std::u32string kKya    = U"کیا";
const std::u32string kTo     = U"تو";
const std::u32string kNe     = U"نے";

bool inList (const std::u32string& w, std::initializer_list<const char32_t*> list)
{
    for (auto* s : list)
        if (w == s) return true;
    return false;
}

bool endsWith (const std::u32string& w, const std::u32string& suffix)
{
    return w.size() >= suffix.size() && std::equal (suffix.rbegin(), suffix.rend(), w.rbegin());
}
} // namespace

std::u32string normaliseUrdu (const std::u32string& w, bool stripDiacritics)
{
    std::u32string out;
    out.reserve (w.size());
    for (auto c : w)
    {
        switch (c)
        {
            case 0x064A: c = 0x06CC; break; // Arabic yeh -> Farsi/Urdu yeh
            case 0x0649: c = 0x06CC; break; // alef maksura -> yeh
            case 0x0643: c = 0x06A9; break; // Arabic kaf -> keheh
            case 0x0647: c = 0x06C1; break; // Arabic heh -> heh goal
            case 0x06C0: c = 0x06C1; break; // heh with yeh above -> heh goal
            case 0x0629: c = 0x06C1; break; // teh marbuta -> heh goal
            case 0x200C: case 0x200D: case 0x0640: continue; // ZWNJ, ZWJ, tatweel
            default: break;
        }
        if (stripDiacritics && isDiacritic (c)) continue;
        out.push_back (c);
    }
    return out;
}

int RomanUrdu::loadLexicon (const std::string& tsvText)
{
    urduToRoman.clear();
    variantToCanonical.clear();
    canonicalByLower.clear();
    romanOnly = 0;

    std::istringstream in (tsvText);
    std::string line;
    int count = 0;
    while (std::getline (in, line))
    {
        if (! line.empty() && line.back() == '\r') line.pop_back();
        if (trim (line).empty() || line[0] == '#') continue;

        auto cols = split (line, '\t');
        if (cols.size() < 2) continue;

        const auto urdu  = trim (cols[0]);
        const auto roman = trim (cols[1]);
        if (roman.empty()) continue;

        canonicalByLower.emplace (toLowerAscii (roman), roman);

        if (urdu != "-" && ! urdu.empty())
        {
            const auto u = utf8::decode (urdu);
            urduToRoman.emplace (normaliseUrdu (u, false), roman); // exact (keeps zabar/pesh)
            urduToRoman.emplace (normaliseUrdu (u, true), roman);  // stripped; first entry wins
        }
        else
        {
            ++romanOnly;
        }

        if (cols.size() > 2)
            for (auto& v : split (cols[2], '|'))
            {
                auto lv = toLowerAscii (trim (v));
                if (! lv.empty()) variantToCanonical.emplace (lv, roman);
            }
        ++count;
    }
    return count;
}

bool RomanUrdu::containsUrduScript (const std::string& utf8Text)
{
    return hasUrdu (utf8::decode (utf8Text));
}

std::string RomanUrdu::normaliseLatinToken (const std::string& token) const
{
    const auto lower = toLowerAscii (token);
    if (lower == "i") return "I";
    if (auto it = canonicalByLower.find (lower); it != canonicalByLower.end()) return it->second;
    if (auto it = variantToCanonical.find (lower); it != variantToCanonical.end()) return it->second;
    return lower;
}

std::string RomanUrdu::convertUrduToken (const std::vector<std::u32string>& tokens, size_t i,
                                         size_t& consumed, bool& guessed) const
{
    consumed = 1;
    guessed = false;

    const auto exact = normaliseUrdu (tokens[i], false);
    const auto bare  = normaliseUrdu (tokens[i], true);

    auto bareAt = [&] (long idx) -> std::u32string
    {
        if (idx < 0 || (size_t) idx >= tokens.size()) return {};
        return normaliseUrdu (tokens[(size_t) idx], true);
    };
    const auto prev = bareAt ((long) i - 1);
    const auto next = bareAt ((long) i + 1);

    // Two-word entries such as "اسلام آباد".
    if (! next.empty() && hasUrdu (next))
    {
        auto key = bare + U" " + next;
        if (auto it = urduToRoman.find (key); it != urduToRoman.end())
        {
            consumed = 2;
            return it->second;
        }
    }

    // Words that are spelled the same in Urdu script but said differently.
    // Only applied when the model didn't write the vowel marks itself.
    if (exact == bare)
    {
        if (bare == kMain)
        {
            // "main" (I) vs "mein" (in)
            const bool startOfLine = (i == 0);
            const bool verbAfter = inList (next, { U"ہوں", U"تھا", U"تھی", U"نے", U"کر", U"کرتا", U"کرتی",
                                                   U"جا", U"آ", U"بول", U"بولا", U"چلا", U"رہا", U"رہی",
                                                   U"آیا", U"گیا", U"خود", U"اب" });
            const bool subjectBefore = inList (prev, { U"کہ", U"اور", U"جب", U"اگر", U"تو", U"پر", U"لیکن", U"مگر", U"یار" });
            return (startOfLine || verbAfter || subjectBefore) ? "main" : "mein";
        }
        if (bare == kKya)
        {
            // "kiya" (did) needs an ergative "ne" earlier (maine / usne / tum ne)
            // and sits at the end of the clause: "maine kiya", "tum ne kya kiya",
            // "usne kiya hai". Otherwise it is "kya" (what): "maine kya kaha".
            if (next == kKya) return "kya";
            bool neBefore = false;
            for (size_t j = 0; j < i; ++j)
                if (endsWith (normaliseUrdu (tokens[j], true), kNe)) neBefore = true;
            const bool clauseEnd = next.empty() || inList (next, { U"ہے", U"تھا", U"تھی", U"ہو", U"گیا", U"اور", U"تو", U"ہیں" });
            if (neBefore && clauseEnd) return "kiya";
            return "kya";
        }
        if (bare == kTo)
        {
            // "tu" (you) when it is the subject: "tu hai", "tu ne", "tu tha".
            if (inList (next, { U"ہے", U"تھا", U"تھی", U"نے", U"بھی", U"کون", U"کیا" }) && (i == 0 || prev.empty()))
                return "tu";
            return "toh";
        }
    }

    if (auto it = urduToRoman.find (exact); it != urduToRoman.end()) return it->second;
    if (auto it = urduToRoman.find (bare); it != urduToRoman.end()) return it->second;

    // Common joined spellings: "اسکا" -> "us ka" is rare in Whisper output, so
    // only handle the ergative suffix: "اسنے" / "تمنے" -> "usne" / "tumne".
    if (bare.size() > 2 && endsWith (bare, kNe))
    {
        const auto stem = bare.substr (0, bare.size() - kNe.size());
        if (auto it = urduToRoman.find (stem); it != urduToRoman.end())
            return it->second + "ne";
    }

    guessed = true;
    return fallbackTransliterate (exact);
}

std::string RomanUrdu::fallbackTransliterate (const std::u32string& w) const
{
    std::string out;
    bool prevConsonant = false;
    bool suppressInherent = false;
    std::string lastCons;

    const size_t n = w.size();
    auto isLast = [&] (size_t i)
    {
        for (size_t j = i + 1; j < n; ++j)
            if (! isDiacritic (w[j])) return false;
        return true;
    };
    auto nextLetter = [&] (size_t i) -> char32_t
    {
        for (size_t j = i + 1; j < n; ++j)
            if (! isDiacritic (w[j])) return w[j];
        return 0;
    };
    auto emitConsonant = [&] (const std::string& s)
    {
        if (prevConsonant && ! suppressInherent) out += "a";
        out += s;
        lastCons = s;
        prevConsonant = true;
        suppressInherent = false;
    };

    for (size_t i = 0; i < n; ++i)
    {
        const char32_t c = w[i];

        if (isDiacritic (c))
        {
            switch (c)
            {
                case 0x064E: out += "a"; prevConsonant = false; break;  // zabar
                case 0x0650: out += "i"; prevConsonant = false; break;  // zer
                case 0x064F: out += "u"; prevConsonant = false; break;  // pesh
                case 0x0670: out += "a"; prevConsonant = false; break;  // khari zabar
                case 0x064B: out += "an"; prevConsonant = false; break; // tanween
                case 0x0651: if (! lastCons.empty()) out += lastCons.substr (0, 1); break; // shadda
                case 0x0652: suppressInherent = true; break;            // sukun / jazm
                default: break;
            }
            continue;
        }

        if (c == U'ھ') { out += "h"; continue; } // aspiration: bh, th, gh, ...
        if (c == U'ں') { out += "n"; prevConsonant = false; continue; }
        if (c == U'ء') { continue; }

        if (c == U'ہ' && i > 0 && isLast (i))
        {
            out += "a"; // word-final heh: paisa, raasta
            prevConsonant = false;
            continue;
        }

        if (auto* cons = consonant (c))
        {
            emitConsonant (cons);
            continue;
        }

        switch (c)
        {
            case U'آ':
                out += "aa"; prevConsonant = false; break;
            case U'ا':
                out += (i == 0 || isLast (i)) ? "a" : "aa";
                prevConsonant = false;
                break;
            case U'و':
            {
                const auto nx = nextLetter (i);
                const bool beforeVowel = (nx == U'ا' || nx == U'ی' || nx == U'ے');
                if (i == 0 || (! prevConsonant && i > 0) || beforeVowel) emitConsonant ("w");
                else { out += "o"; prevConsonant = false; }
                break;
            }
            case U'ی':
            {
                const auto nx = nextLetter (i);
                const bool beforeVowel = (nx == U'ا' || nx == U'و' || nx == U'ے');
                if (i == 0 || beforeVowel || (! prevConsonant && ! isLast (i))) emitConsonant ("y");
                else if (isLast (i)) { out += "i"; prevConsonant = false; }
                else { out += "ee"; prevConsonant = false; }
                break;
            }
            case U'ے':
                out += "e"; prevConsonant = false; break;
            case U'ئ':
            {
                const auto nx = nextLetter (i);
                if (! (nx == U'ی' || nx == U'ے' || nx == 0)) out += "i";
                prevConsonant = false;
                break;
            }
            case U'ع':
            {
                const auto nx = nextLetter (i);
                if (i == 0 && nx != 0 && consonant (nx) != nullptr) out += "a";
                prevConsonant = false;
                break;
            }
            default:
                if (c < 0x80) out += (char) c; // digits etc.
                break;
        }
    }
    return out;
}

namespace
{
void tokenise (const std::string& utf8Text, int source, std::vector<std::u32string>& tokens, std::vector<int>& sources)
{
    std::u32string cur;
    for (auto c : utf8::decode (utf8Text))
    {
        if (isSeparator (c))
        {
            if (! cur.empty()) { tokens.push_back (cur); sources.push_back (source); cur.clear(); }
        }
        else cur.push_back (c);
    }
    if (! cur.empty()) { tokens.push_back (cur); sources.push_back (source); }
}
} // namespace

std::vector<RomanWord> RomanUrdu::convertTokens (const std::vector<std::u32string>& tokens, const std::vector<int>& sources) const
{
    std::vector<RomanWord> out;
    for (size_t i = 0; i < tokens.size();)
    {
        RomanWord w;
        w.source = sources[i];
        size_t consumed = 1;

        if (hasUrdu (tokens[i]))
        {
            w.text = convertUrduToken (tokens, i, consumed, w.guessed);
        }
        else
        {
            auto t = utf8::encode (tokens[i]);
            // trim stray apostrophes at the edges
            while (! t.empty() && t.front() == '\'') t.erase (t.begin());
            while (! t.empty() && t.back() == '\'') t.pop_back();
            if (! t.empty()) w.text = normaliseLatinToken (t);
        }

        w.sourceCount = sources[i + consumed - 1] - sources[i] + 1;
        if (! w.text.empty()) out.push_back (w);
        i += consumed;
    }
    return out;
}

std::vector<RomanWord> RomanUrdu::convertLine (const std::string& utf8Text) const
{
    std::vector<std::u32string> tokens;
    std::vector<int> sources;
    tokenise (utf8Text, 0, tokens, sources);
    return convertTokens (tokens, sources);
}

std::vector<RomanWord> RomanUrdu::convertWords (const std::vector<std::string>& words) const
{
    std::vector<std::u32string> tokens;
    std::vector<int> sources;
    for (size_t i = 0; i < words.size(); ++i)
        tokenise (words[i], (int) i, tokens, sources);
    return convertTokens (tokens, sources);
}

std::string RomanUrdu::convertToString (const std::string& utf8Text) const
{
    std::string s;
    for (auto& w : convertLine (utf8Text))
    {
        if (! s.empty()) s += ' ';
        s += w.text;
    }
    return s;
}
} // namespace shabd
