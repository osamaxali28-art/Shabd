# Shabd

**Shabd** listens to Urdu rap vocals and writes them down in Roman Urdu as the
track plays, then exports captions for YouTube, Spotify, Apple Music and lyric
sites.

Built by Moji ([Itsthemoji](https://youtube.com/@Itsthemoji)).

- Put it on your **lead vocal** or **vocal bus** (not the master: the beat confuses it)
- Words appear a few seconds behind the playhead, bar by bar, on the song's timeline
- Words it isn't sure about show in **amber**: double-click any word to fix it
- **Final pass** re-listens to the whole take with slower, more accurate settings
- **Export** writes every format at once

| File | Use it for |
|---|---|
| `.srt` | YouTube captions, Premiere, CapCut, VLC |
| `.vtt` | YouTube, web players |
| `.lrc` | Spotify (via Musixmatch), Apple Music, lyric apps |
| `.words.lrc` | Word-by-word karaoke timing |
| `.ttml` | Apple Music time-synced lyrics, distributors that ask for TTML |
| `.txt` | Plain lyrics for DistroKid, Genius, video descriptions |

Spelling follows the [Roman Urdu style guide](docs/ROMAN_URDU_STYLE.md)
(*hai, nahi, main / mein, kya / kiya, bohat, yaar*...). Change any spelling in
[`lexicon/roman_urdu.tsv`](lexicon/roman_urdu.tsv).

## Install

1. **Get the plugin.** Every push to `main` builds Shabd in the cloud: open the
   repo's **Actions** tab, pick the latest green *Build Shabd* run and download
   `Shabd-Windows` or `Shabd-macOS`. Tagged versions (`v0.1.0`...) also appear under
   **Releases**.
   - Windows: copy `Shabd.vst3` to `C:\Program Files\Common Files\VST3\`
   - macOS: copy `Shabd.vst3` to `~/Library/Audio/Plug-Ins/VST3/` and
     `Shabd.component` to `~/Library/Audio/Plug-Ins/Components/`. The build isn't
     signed yet, so run this once in Terminal:
     `xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Shabd.vst3 ~/Library/Audio/Plug-Ins/Components/Shabd.component`

2. **Get a model** (the "ears"). Download
   [`ggml-large-v3-turbo-q5_0.bin`](https://huggingface.co/ggerganov/whisper.cpp/blob/main/ggml-large-v3-turbo-q5_0.bin)
   (~550 MB) and put it in `Documents/Shabd/models/`. Shabd loads it
   automatically. A model fine-tuned on your own vocals (see below) with `shabd`
   in its file name is picked first.

3. Rescan plugins in your DAW, insert **Shabd** on the vocal, press play.

## Getting the best results

- Feed it the **dry or lightly processed** lead. Heavy reverb/delay and stacked
  doubles make it guess more.
- Ad-lib tracks: put a second Shabd on them, or leave them off.
- Run **Final pass** once the take is done, *then* fix the amber words. (Final
  pass replaces the live text.)
- Your fixes are saved with the DAW project.

Stock Whisper understands Urdu, but fast flows, slang and Punjabi words will
trip it up. The fix is to **train it on your own vocals**: every song you
correct and export becomes training data.
See [`training/`](training/README.md).

## Without a DAW

`shabd-cli` (included in the download) does the accurate pass on a bounced vocal:

```
shabd-cli vocal.wav --model ggml-large-v3-turbo-q5_0.bin --title "Song" --artist "Moji"
```

## Build from source

Needs CMake 3.22+ and a C++17 compiler. JUCE 9.0.5 and whisper.cpp 1.9.5 are
downloaded by CMake.

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --target Shabd_VST3 shabd-cli
```

Engine tests only (fast, no downloads): `cmake -B build -DSHABD_CORE_ONLY=ON && cmake --build build && ctest --test-dir build`

How it works: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).
