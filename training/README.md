# Train Shabd on your vocals

Stock Whisper has heard plenty of Urdu news and podcasts, not much Urdu rap. Fine-tuning
on your own songs teaches it your voice, your flow, your slang, and to write
Roman Urdu directly in Shabd's spelling. Use your own music only: you need the
rights to the vocals and lyrics you train on.

**How much data:** about 20 songs is where it gets noticeably better. 40+ is great.
Start with what you have; you can retrain any time.

## 1. Make the training set

For each song, put two files with the same name in a `songs/` folder:

```
songs/
  meri-gali.wav     dry lead vocal (acapella stem)
  meri-gali.lrc     its lyrics, timed, in Roman Urdu
```

The lyric file is easiest to make with Shabd itself: play the vocal through
Shabd, run **Final pass**, fix every wrong word, **Export**. Rename the exported
`.lrc` (or `.srt`) to match the wav. Corrected exports are perfect labels.

```
pip install -r requirements.txt
python prepare_dataset.py songs/ data/ --eval-songs 2
```

One or two songs are held back so you can see how much the model improved on
songs it never trained on.

## 2. Train (in the cloud)

You need an NVIDIA GPU with 16 GB+. Free/cheap options: Google Colab (T4 / L4),
Kaggle notebooks, RunPod. Upload this repo and your `data/` folder, then:

```
pip install -r requirements.txt
python finetune.py data/ out/shabd-moji --epochs 6
```

It prints the **held-out WER** (word error rate) at the end, plus
`out/shabd-moji/eval_samples.txt` with what it heard vs your lyrics. Lower WER is better.
Run it once with `--epochs 0` first to get the stock model's score for comparison.

## 3. Put it in the plugin

```
bash export_ggml.sh out/shabd-moji
```

Copy `out/shabd-moji/ggml-shabd-moji-q5_0.bin` to `Documents/Shabd/models/`.
Shabd prefers any model with `shabd` in the name, so it uses yours automatically.

## The loop

Every new song: Shabd drafts → you fix → export → add to `songs/` → retrain now
and then. Each round, fewer amber words.
