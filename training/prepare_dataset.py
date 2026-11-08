#!/usr/bin/env python3
"""
Turn your acapellas + timed Roman Urdu lyrics into a Whisper fine-tuning set.

Input folder (one pair per song, same file name):
    songs/
      meri-gali.wav        dry lead vocal / acapella (any sample rate, mono or stereo)
      meri-gali.lrc        timed lyrics in Roman Urdu (.lrc, .words.lrc or .srt)

The easiest way to get the lyric file: run Shabd on the vocal, fix the amber
words in the plugin, Export. That corrected export IS your training label.

Output:
    data/
      audio/<song>_<n>.wav   16 kHz mono clips, each <= 28 s, cut between bars
      train.jsonl            {"audio": "...", "text": "...", "song": "..."}
      eval.jsonl             held-out songs, for measuring improvement

Usage:
    python prepare_dataset.py songs/ data/ --eval-songs 1
"""
import argparse
import json
import random
import re
import sys
from pathlib import Path

import numpy as np
import soundfile as sf

try:
    import librosa
except ImportError:  # only needed when the wav isn't 16 kHz already
    librosa = None

TARGET_SR = 16000
MAX_CLIP = 28.0  # Whisper sees 30 s windows; leave room
LEXICON = Path(__file__).resolve().parent.parent / "lexicon" / "roman_urdu.tsv"


# --------------------------------------------------------------------- lyrics
def parse_time_lrc(t):
    m, s = t.split(":")
    return int(m) * 60 + float(s)


MAX_BAR = 10.0  # plain LRC has no end times; a bar rarely runs longer than this


def parse_lrc(text):
    lines = []
    for raw in text.splitlines():
        stamps = re.findall(r"\[(\d+:\d+(?:\.\d+)?)\]", raw)
        if not stamps:
            continue
        word_times = [parse_time_lrc(t) for t in re.findall(r"<(\d+:\d+(?:\.\d+)?)>", raw)]
        words = " ".join(re.sub(r"\[[^\]]*\]|<[^>]*>", " ", raw).split())
        for st in stamps:
            # enhanced LRC (.words.lrc): the last word tag is the real end of the bar
            end = max(word_times) if word_times else None
            lines.append([parse_time_lrc(st), end, words])
    lines.sort(key=lambda x: x[0])
    for i, l in enumerate(lines):
        nxt = lines[i + 1][0] if i + 1 < len(lines) else l[0] + MAX_BAR
        l[1] = min(l[1], nxt) if l[1] is not None else min(nxt, l[0] + MAX_BAR)
    return [tuple(l) for l in lines if l[2]]


def parse_srt(text):
    out = []
    for block in re.split(r"\n\s*\n", text.strip()):
        rows = [r for r in block.splitlines() if r.strip()]
        for i, r in enumerate(rows):
            m = re.match(r"(\d+):(\d+):(\d+)[,.](\d+)\s*-->\s*(\d+):(\d+):(\d+)[,.](\d+)", r)
            if m:
                g = [int(x) for x in m.groups()]
                a = g[0] * 3600 + g[1] * 60 + g[2] + g[3] / 1000
                b = g[4] * 3600 + g[5] * 60 + g[6] + g[7] / 1000
                words = " ".join(rows[i + 1:]).strip()
                if words:
                    out.append((a, b, words))
                break
    return out


def load_lyrics(path):
    text = path.read_text(encoding="utf-8")
    return parse_srt(text) if path.suffix.lower() == ".srt" else parse_lrc(text)


# ----------------------------------------------------------- spelling cleanup
def load_variants():
    variants, canon = {}, {}
    if not LEXICON.exists():
        return variants, canon
    for raw in LEXICON.read_text(encoding="utf-8").splitlines():
        if not raw.strip() or raw.startswith("#"):
            continue
        cols = raw.split("\t")
        if len(cols) < 2:
            continue
        roman = cols[1].strip()
        canon[roman.lower()] = roman
        if len(cols) > 2:
            for v in cols[2].split("|"):
                v = v.strip().lower()
                if v:
                    variants.setdefault(v, roman)
    return variants, canon


VARIANTS, CANON = load_variants()


def normalise(line):
    """Apply the Shabd style guide so every label spells words the same way."""
    out = []
    for tok in line.split():
        core = tok.strip(".,!?;:\"'")
        low = core.lower()
        if low in CANON:
            rep = CANON[low]
        elif low in VARIANTS:
            rep = VARIANTS[low]
        else:
            rep = low if core not in ("I",) else core
        out.append(tok.replace(core, rep) if core else tok)
    text = " ".join(out)
    return text[:1].upper() + text[1:] if text else text


# --------------------------------------------------------------------- audio
def load_audio(path):
    audio, sr = sf.read(str(path), dtype="float32", always_2d=True)
    audio = audio.mean(axis=1)
    if sr != TARGET_SR:
        if librosa is None:
            sys.exit(f"{path.name} is {sr} Hz; install librosa to resample")
        audio = librosa.resample(audio, orig_sr=sr, target_sr=TARGET_SR)
    return audio


def group_lines(lines):
    """Pack consecutive bars into clips of at most MAX_CLIP seconds."""
    clips, cur = [], []
    for a, b, text in lines:
        if b - a > MAX_CLIP:  # a single over-long "line": skip, can't be labelled reliably
            continue
        if cur and b - cur[0][0] > MAX_CLIP:
            clips.append(cur)
            cur = []
        cur.append((a, b, text))
    if cur:
        clips.append(cur)
    return clips


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("songs", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--eval-songs", type=int, default=1, help="songs held out to measure accuracy")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()

    pairs = []
    for wav in sorted(args.songs.glob("*.wav")):
        for ext in (".words.lrc", ".lrc", ".srt"):
            lyr = wav.with_name(wav.stem + ext)
            if lyr.exists():
                pairs.append((wav, lyr))
                break
        else:
            print(f"skip {wav.name}: no .lrc / .srt next to it")

    if not pairs:
        sys.exit("no wav + lyrics pairs found")

    random.Random(args.seed).shuffle(pairs)
    eval_names = {p[0].stem for p in pairs[: args.eval_songs]} if len(pairs) > 1 else set()

    (args.out / "audio").mkdir(parents=True, exist_ok=True)
    rows = {"train": [], "eval": []}
    total = 0.0
    for wav, lyr in pairs:
        audio = load_audio(wav)
        lines = load_lyrics(lyr)
        split = "eval" if wav.stem in eval_names else "train"
        for n, clip in enumerate(group_lines(lines)):
            a = max(0.0, clip[0][0] - 0.2)
            b = min(len(audio) / TARGET_SR, clip[-1][1] + 0.2)
            seg = audio[int(a * TARGET_SR): int(b * TARGET_SR)]
            if len(seg) < TARGET_SR * 0.5 or np.sqrt(np.mean(seg ** 2)) < 1e-3:
                continue
            name = f"{wav.stem}_{n:03d}.wav"
            sf.write(str(args.out / "audio" / name), seg, TARGET_SR)
            text = " ".join(normalise(t) for _, _, t in clip)
            rows[split].append({"audio": f"audio/{name}", "text": text, "song": wav.stem})
            total += b - a
        print(f"{wav.stem}: {len(lines)} lines -> {split}")

    for split, items in rows.items():
        with open(args.out / f"{split}.jsonl", "w", encoding="utf-8") as f:
            for r in items:
                f.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"\n{len(rows['train'])} train clips, {len(rows['eval'])} eval clips, {total / 60:.1f} min of vocals")


if __name__ == "__main__":
    main()
