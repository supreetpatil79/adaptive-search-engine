#!/usr/bin/env python3
"""
scripts/export_model.py
=======================
Downloads sentence-transformers/all-MiniLM-L6-v2 from Hugging Face
and exports it to ONNX format suitable for use with the ONNX Runtime C++ API.

Output: models/all-MiniLM-L6-v2.onnx
        models/tokenizer_config/  (vocab + special tokens for BPE at index time)

Usage:
    python3 scripts/export_model.py

Run ONCE offline; the C++ runtime never calls Python.
"""

import os
import sys

try:
    import torch
    from transformers import AutoTokenizer, AutoModel
    import onnx
except ImportError as e:
    print(f"Missing dependency: {e}")
    print("Run: pip3 install torch transformers onnx onnxruntime --break-system-packages")
    sys.exit(1)

MODEL_ID  = "sentence-transformers/all-MiniLM-L6-v2"
OUT_DIR   = "models"
ONNX_PATH = os.path.join(OUT_DIR, "all-MiniLM-L6-v2.onnx")
TOK_PATH  = os.path.join(OUT_DIR, "tokenizer_config")

os.makedirs(OUT_DIR,  exist_ok=True)
os.makedirs(TOK_PATH, exist_ok=True)

print(f"[1/4] Loading tokenizer and model: {MODEL_ID}")
tokenizer = AutoTokenizer.from_pretrained(MODEL_ID)
model     = AutoModel.from_pretrained(MODEL_ID)
model.eval()

# Save tokenizer config so C++ knows the vocabulary.
tokenizer.save_pretrained(TOK_PATH)
print(f"      Tokenizer saved to {TOK_PATH}/")

# ── Dummy input for tracing ────────────────────────────────────────────────
print("[2/4] Creating dummy inputs for ONNX export")
dummy_text   = "This is a sample sentence for tracing."
encoded      = tokenizer(dummy_text, return_tensors="pt",
                         padding="max_length", max_length=128, truncation=True)
input_ids      = encoded["input_ids"]
attention_mask = encoded["attention_mask"]
token_type_ids = encoded.get("token_type_ids", torch.zeros_like(input_ids))

# ── Mean-pooling wrapper ───────────────────────────────────────────────────
# all-MiniLM-L6-v2 outputs last_hidden_state; we mean-pool over non-padding
# tokens and L2-normalise.  Baking the pooling into the ONNX graph means
# the C++ code only calls Run() once and gets a 384-d unit vector back.
class MeanPoolingModel(torch.nn.Module):
    def __init__(self, base):
        super().__init__()
        self.base = base

    def forward(self, input_ids, attention_mask, token_type_ids):
        out = self.base(input_ids=input_ids,
                        attention_mask=attention_mask,
                        token_type_ids=token_type_ids)
        hidden = out.last_hidden_state  # (B, seq, 384)
        mask   = attention_mask.unsqueeze(-1).float()
        summed = (hidden * mask).sum(dim=1)
        counts = mask.sum(dim=1).clamp(min=1e-9)
        pooled = summed / counts         # mean pool
        normed = torch.nn.functional.normalize(pooled, p=2, dim=1)
        return normed  # (B, 384)  — unit vector, cosine sim = dot product

wrapped = MeanPoolingModel(model)
wrapped.eval()

print("[3/4] Exporting to ONNX …")
torch.onnx.export(
    wrapped,
    (input_ids, attention_mask, token_type_ids),
    ONNX_PATH,
    opset_version    = 14,
    input_names      = ["input_ids", "attention_mask", "token_type_ids"],
    output_names     = ["sentence_embedding"],
    dynamic_axes     = {
        "input_ids":      {0: "batch", 1: "seq_len"},
        "attention_mask": {0: "batch", 1: "seq_len"},
        "token_type_ids": {0: "batch", 1: "seq_len"},
        "sentence_embedding": {0: "batch"},
    },
    do_constant_folding = True,
)

print("[4/4] Verifying ONNX model …")
onnx_model = onnx.load(ONNX_PATH)
onnx.checker.check_model(onnx_model)

size_mb = os.path.getsize(ONNX_PATH) / 1024 / 1024
print(f"\n✓  Exported: {ONNX_PATH}  ({size_mb:.1f} MB)")
print(f"✓  Tokenizer: {TOK_PATH}/")

# ── Extract vocab.txt for C++ WordPiece tokeniser ─────────────────────────
# Modern HuggingFace saves a single tokenizer.json (fast tokenizer format).
# The C++ OrtEmbedder reads vocab.txt (one token per line, index = token_id).
import json
tok_json_path = os.path.join(TOK_PATH, "tokenizer.json")
vocab_txt_path = os.path.join(TOK_PATH, "vocab.txt")

with open(tok_json_path) as f:
    tj = json.load(f)

vocab = tj["model"]["vocab"]                          # token → id
ordered = sorted(vocab.items(), key=lambda x: x[1])  # sort by id

with open(vocab_txt_path, "w") as f:
    for token, _ in ordered:
        f.write(token + "\n")

print(f"✓  vocab.txt: {vocab_txt_path}  ({len(ordered)} tokens)")
print(f"   CLS={vocab['[CLS]']}  SEP={vocab['[SEP]']}  UNK={vocab['[UNK]']}  PAD={vocab['[PAD]']}")
print("\nNext step:")
print("  python3 scripts/embed_corpus.py data/documents.txt data/embeddings.bin")

