# Shabd Roman Urdu style guide

How Shabd writes Urdu rap in Roman letters. The plugin follows these rules
automatically; this page is for you when you correct a transcript by hand, so
your corrections (which become training data) stay consistent.

The word list lives in [`lexicon/roman_urdu.tsv`](../lexicon/roman_urdu.tsv).
If you prefer a different spelling for a word, change it there and Shabd will
use it everywhere.

## 1. One word, one spelling

Online desi rap captions spell the same word five ways (*hai / hay / he / hy / h*).
Shabd picks one and sticks to it. The most important ones:

| Word | Shabd writes | Not |
|---|---|---|
| ہے | **hai** | hay, he, hy, h |
| ہیں | **hain** | hen, hn |
| نہیں | **nahi** | nai, nahin, nhi |
| میں (I) | **main** | mai |
| میں (in) | **mein** | me, men |
| کیا (what) | **kya** | kia |
| کیا (did) | **kiya** | kia |
| تو (then/so) | **toh** | to |
| تُو (you) | **tu** | |
| یہ / وہ | **yeh / woh** | ye, wo |
| بہت | **bohat** | bahut, bht |
| ہوں | **hoon** | hun, hu |
| تھے | **the** | thay |
| کیوں | **kyun** | kyu, q |
| یار / بھائی | **yaar / bhai** | yar, bhae |

## 2. Letters

- Long vowels are doubled: **aa** (baat, raat, aag), **ee** (zameen, neeche), **oo** (hoon, poora).
- Short vowels stay single: dil, tum, sab.
- No accents or diacritics (no ā, ī). They don't render well on Spotify or YouTube.
- Nasal ں is written **n**: main, hain, aankhein.
- Aspirated sounds take an **h**: bh, ph, th, dh, ch, gh, kh, jh (bhai, phir, tha, ghar, jhoot).
- ق is **q** (qasam, qalam, waqt), not k.
- خ is **kh** (khwaab, khushi), غ is **gh** (gham, ghairat).
- ز ذ ض ظ are all **z** (zindagi, lafz, alfaaz).
- ش is **sh**, چ is **ch**.
- Words ending in ہ sound like **a**: paisa, raasta.

## 3. English words

Write them in normal English spelling: flow, scene, bars, beat, mic, real,
fake, hype, haters, studio. Whisper often writes these in Urdu script
(فلو, سین); Shabd converts them back.

Shabd never "corrects" a word that is also a real English word. *to, say, day,
or, in, use* are left alone because they appear in English lines.

## 4. Names

Capitalise places, people and God: Allah, Khuda, Pakistan, Lahore, Karachi,
Islamabad, Pindi. Everything else is lowercase except the first word of a line.

## 5. Lines and cues (for SRT / YouTube / Spotify)

- One bar per line where possible.
- At most 42 characters per line, two lines per caption.
- A new line starts after a pause longer than 0.35 s.
- Ad-libs go in brackets: (yeah), (skrrt), (ayy).
- Write what is said. Mumbled or doubled words are written once.

## 6. Words Shabd can't read

When Shabd hears a word that is not in the list, it guesses a spelling and
marks it amber in the plugin window. Fix those first. If a word comes up
often, add it to `lexicon/roman_urdu.tsv`.
