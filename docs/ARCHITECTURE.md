# How Shabd works

```
DAW audio ──► processBlock (audio thread)
               - audio passes through untouched
               - downmix to mono, stamp with the DAW playhead time
               - push into a lock-free FIFO (never blocks, never allocates)
                    │
                    ▼
             LiveSession worker thread
               - resample to 16 kHz (windowed-sinc anti-alias filter)
               - write onto the song timeline (loops / punch-ins overwrite)
               - live pass: every bar-pause or 6 s, transcribe the new audio
                 (+1 s overlap for context, hold back the last 1 s unless the rapper paused)
               - final pass: whole take, ≤28 s chunks cut at the quietest point, beam 8
                    │
                    ▼
             Detector (whisper.cpp)
               - language = Urdu, word-level timestamps
               - vocabulary prompt with rap words and Pakistani city names
               - each window decoded independently (stops hallucination loops)
               - drops non-speech tags and a word repeated > 4 times in a row
                    │
                    ▼
             RomanUrdu
               - Urdu script → Roman via lexicon (279 words) + context rules
                 (main/mein, kya/kiya, toh/tu)
               - unknown words: rule-based transliteration, flagged amber
               - Roman output (fine-tuned model): variant spellings → one spelling
               - English words left alone
                    │
                    ▼
             Subtitles: lines (pause > 0.35 s or 42 chars) → SRT / VTT / LRC / TTML / TXT
```

## Source layout

| Path | What |
|---|---|
| `Source/core/` | Roman Urdu converter + subtitle writers. No dependencies; unit-tested. |
| `Source/engine/` | Detector (whisper.cpp), LiveSession (threading, timeline), resampler |
| `Source/plugin/` | JUCE processor, editor and transcript view |
| `Source/cli/` | `shabd-cli` for offline transcription of a WAV |
| `lexicon/` | `roman_urdu.tsv`: the spelling list, embedded at build time |
| `training/` | Fine-tune Whisper on your vocals, export to ggml |
| `tests/` | Engine tests (`-DSHABD_CORE_ONLY=ON`) |

## Tuning knobs

- `Source/engine/LiveSession.cpp` top: live window, overlap, hold-back, pause/silence thresholds
- `Source/engine/Detector.h` `DetectorSettings`: beam sizes, language, vocabulary prompt
- `Source/core/Subtitles.h` `LineRules`: line length, pause that starts a new line
- `Documents/Shabd/roman_urdu.tsv`: if present, the plugin uses it instead of the
  built-in list, so you can change spellings without rebuilding

## Known limits (v0.1)

- Transcribes only while the DAW is playing or recording (the standalone app
  always listens).
- Captured audio isn't saved with the project. After reopening, play the track
  through again before running Final pass. The transcript itself is saved.
- While Final pass runs, live listening pauses.
- Rule-based transliteration of unknown words is a rough guess. The fine-tuned
  model writes Roman directly and skips that step.
