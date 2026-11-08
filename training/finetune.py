#!/usr/bin/env python3
"""
Fine-tune Whisper on your vocals so it writes Roman Urdu directly, in your
spelling, and gets used to your flow, accent and slang.

Runs on one GPU with 16 GB (Google Colab T4 / L4, Kaggle, RunPod...). Uses
LoRA, so only a small adapter is trained; it's merged back at the end.

    python finetune.py data/ out/shabd-moji --epochs 6

Then convert for the plugin:
    bash export_ggml.sh out/shabd-moji
"""
import argparse
import json
from dataclasses import dataclass
from pathlib import Path

import numpy as np
import soundfile as sf
import torch
from torch.utils.data import Dataset
from transformers import (
    Seq2SeqTrainer,
    Seq2SeqTrainingArguments,
    WhisperForConditionalGeneration,
    WhisperProcessor,
)

BASE_MODEL = "openai/whisper-large-v3-turbo"


class ClipDataset(Dataset):
    def __init__(self, root: Path, split: str, processor):
        self.root = root
        self.proc = processor
        path = root / f"{split}.jsonl"
        self.rows = [json.loads(l) for l in path.read_text(encoding="utf-8").splitlines() if l.strip()] if path.exists() else []

    def __len__(self):
        return len(self.rows)

    def __getitem__(self, i):
        r = self.rows[i]
        audio, sr = sf.read(str(self.root / r["audio"]), dtype="float32")
        feats = self.proc.feature_extractor(audio, sampling_rate=sr).input_features[0]
        labels = self.proc.tokenizer(r["text"]).input_ids
        return {"input_features": feats, "labels": labels}


@dataclass
class Collator:
    processor: WhisperProcessor
    decoder_start: int

    def __call__(self, batch):
        feats = self.processor.feature_extractor.pad(
            [{"input_features": b["input_features"]} for b in batch], return_tensors="pt")
        labels = self.processor.tokenizer.pad(
            [{"input_ids": b["labels"]} for b in batch], return_tensors="pt")
        ids = labels["input_ids"].masked_fill(labels.attention_mask.ne(1), -100)
        if (ids[:, 0] == self.decoder_start).all():
            ids = ids[:, 1:]
        feats["labels"] = ids
        return feats


def word_error_rate(preds, refs):
    import jiwer
    return jiwer.wer([r.lower() for r in refs], [p.lower() for p in preds])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("data", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--base", default=BASE_MODEL)
    ap.add_argument("--epochs", type=float, default=6)
    ap.add_argument("--lr", type=float, default=1e-4)
    ap.add_argument("--batch", type=int, default=4)
    ap.add_argument("--lora-rank", type=int, default=32)
    args = ap.parse_args()

    from peft import LoraConfig, get_peft_model

    # Urdu language token + transcribe task. The labels are Roman Urdu, so the
    # model learns "Urdu speech -> Roman text" in your spelling.
    processor = WhisperProcessor.from_pretrained(args.base, language="urdu", task="transcribe")
    model = WhisperForConditionalGeneration.from_pretrained(args.base, torch_dtype=torch.float32)
    model.generation_config.language = "urdu"
    model.generation_config.task = "transcribe"
    model.generation_config.forced_decoder_ids = None
    model.config.use_cache = False

    lora = LoraConfig(r=args.lora_rank, lora_alpha=args.lora_rank * 2, lora_dropout=0.05,
                      target_modules=["q_proj", "k_proj", "v_proj", "out_proj", "fc1", "fc2"])
    model = get_peft_model(model, lora)
    model.print_trainable_parameters()

    train = ClipDataset(args.data, "train", processor)
    evals = ClipDataset(args.data, "eval", processor)
    print(f"{len(train)} train clips, {len(evals)} eval clips")

    training_args = Seq2SeqTrainingArguments(
        output_dir=str(args.out / "checkpoints"),
        per_device_train_batch_size=args.batch,
        per_device_eval_batch_size=args.batch,
        gradient_accumulation_steps=max(1, 16 // args.batch),
        learning_rate=args.lr,
        warmup_ratio=0.1,
        num_train_epochs=args.epochs,
        fp16=torch.cuda.is_available(),
        eval_strategy="epoch" if len(evals) else "no",
        save_strategy="epoch",
        save_total_limit=2,
        logging_steps=10,
        predict_with_generate=False,
        remove_unused_columns=False,
        label_names=["labels"],
        report_to=[],
    )

    trainer = Seq2SeqTrainer(
        model=model,
        args=training_args,
        train_dataset=train,
        eval_dataset=evals if len(evals) else None,
        data_collator=Collator(processor, model.config.decoder_start_token_id),
    )
    trainer.train()

    # Merge the adapter into the full model and save in Hugging Face format.
    merged = model.merge_and_unload()
    final = args.out / "hf"
    merged.save_pretrained(final)
    processor.save_pretrained(final)
    print(f"saved {final}")

    # Word error rate on the held-out songs (lower is better).
    if len(evals):
        merged.eval()
        device = "cuda" if torch.cuda.is_available() else "cpu"
        merged.to(device)
        preds, refs = [], []
        for r in evals.rows:
            audio, sr = sf.read(str(args.data / r["audio"]), dtype="float32")
            feats = processor.feature_extractor(audio, sampling_rate=sr, return_tensors="pt").input_features.to(device)
            with torch.no_grad():
                out = merged.generate(feats, language="urdu", task="transcribe", num_beams=5, max_new_tokens=220)
            preds.append(processor.batch_decode(out, skip_special_tokens=True)[0].strip())
            refs.append(r["text"])
        print(f"held-out WER: {word_error_rate(preds, refs):.1%}")
        with open(args.out / "eval_samples.txt", "w", encoding="utf-8") as f:
            for p, r in zip(preds, refs):
                f.write(f"REF: {r}\nOUT: {p}\n\n")


if __name__ == "__main__":
    main()
