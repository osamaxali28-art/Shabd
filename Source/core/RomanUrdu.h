#pragma once
// RomanUrdu: turns whatever the speech model produced (Urdu script, Roman, or
// a mix with English) into consistent Roman Urdu, following
// docs/ROMAN_URDU_STYLE.md and lexicon/roman_urdu.tsv.
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace shabd
{
struct RomanWord
{
    std::string text;     // Roman Urdu / English output
    bool guessed = false; // true when spelled by the fallback rules (not in lexicon)
    int source = 0;       // index of the first input word this came from (convertWords)
    int sourceCount = 1;  // how many input words it covers (2 for "اسلام آباد")
};

class RomanUrdu
{
public:
    // Parses the TSV lexicon text. Returns number of entries loaded.
    int loadLexicon (const std::string& tsvText);
    int size() const { return (int) urduToRoman.size() + (int) romanOnly; }

    // Convert one line / segment of model output into Roman Urdu words.
    std::vector<RomanWord> convertLine (const std::string& utf8Text) const;

    // Convert a sequence of separately-timed words (e.g. one per Whisper
    // segment) with full context, remembering which input each output came from.
    std::vector<RomanWord> convertWords (const std::vector<std::string>& words) const;

    // Convenience: convertLine joined with spaces.
    std::string convertToString (const std::string& utf8Text) const;

    static bool containsUrduScript (const std::string& utf8Text);

    // Exposed for tests.
    std::string fallbackTransliterate (const std::u32string& urduWord) const;

private:
    std::unordered_map<std::u32string, std::string> urduToRoman; // normalised Urdu -> canonical Roman
    std::unordered_map<std::string, std::string> variantToCanonical; // lowercase variant -> canonical
    std::unordered_map<std::string, std::string> canonicalByLower;   // lowercase canonical -> canonical (keeps capitals)
    int romanOnly = 0;

    std::vector<RomanWord> convertTokens (const std::vector<std::u32string>& tokens, const std::vector<int>& sources) const;
    std::string convertUrduToken (const std::vector<std::u32string>& tokens, size_t index, size_t& consumed, bool& guessed) const;
    std::string normaliseLatinToken (const std::string& token) const;
};

// Normalises Arabic-script presentation differences Whisper may emit
// (Arabic yeh/kaf/heh -> Urdu forms) and optionally strips short-vowel marks.
std::u32string normaliseUrdu (const std::u32string& w, bool stripDiacritics);
} // namespace shabd
