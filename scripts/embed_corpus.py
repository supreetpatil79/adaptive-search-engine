#!/usr/bin/env python3
"""
scripts/embed_corpus.py
=======================
Runs the ONNX model over every document in the corpus and writes a
binary embedding file that the C++ engine loads at startup.

Output format (data/embeddings.bin):
  [4 bytes] int32  num_docs
  [4 bytes] int32  dim        (384 for all-MiniLM-L6-v2)
  [num_docs * dim * 4 bytes]  float32, row-major (doc 0 first, then doc 1, …)

The binary uses native float32 — no padding, no headers beyond the two
int32s.  The C++ FlatEmbedIndex reads this directly via fread.

Usage:
    python3 scripts/embed_corpus.py [corpus_path] [out_path]

Defaults:
    corpus_path  = data/documents.txt
    out_path     = data/embeddings.bin
"""

import sys
import os
import struct
import numpy as np

try:
    from transformers import AutoTokenizer
    import onnxruntime as ort
except ImportError as e:
    print(f"Missing: {e}")
    print("Run: pip3 install transformers onnxruntime --break-system-packages")
    sys.exit(1)

CORPUS_PATH  = sys.argv[1] if len(sys.argv) > 1 else "data/documents.txt"
OUT_PATH     = sys.argv[2] if len(sys.argv) > 2 else "data/embeddings.bin"
MODEL_ONNX   = "models/all-MiniLM-L6-v2.onnx"
TOK_PATH     = "models/tokenizer_config"
MAX_SEQ_LEN  = 128
BATCH_SIZE   = 16

if not os.path.exists(MODEL_ONNX):
    print(f"ONNX model not found: {MODEL_ONNX}")
    print("Run export first:  python3 scripts/export_model.py")
    sys.exit(1)

# ── Load corpus ────────────────────────────────────────────────────────────
with open(CORPUS_PATH, "r", encoding="utf-8") as f:
    docs = [line.strip() for line in f if line.strip()]

print(f"Corpus: {len(docs)} documents from {CORPUS_PATH}")

# ── Load tokenizer + ONNX session ─────────────────────────────────────────
print(f"Loading tokenizer from {TOK_PATH}")
tokenizer = AutoTokenizer.from_pretrained(TOK_PATH)

print(f"Loading ONNX session from {MODEL_ONNX}")
sess_opts = ort.SessionOptions()
sess_opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
sess = ort.InferenceSession(MODEL_ONNX, sess_options=sess_opts,
                             providers=["CPUExecutionProvider"])

DIM = 384  # all-MiniLM-L6-v2 output dimension

# ── Embed in batches ───────────────────────────────────────────────────────
all_embeddings = np.zeros((len(docs), DIM), dtype=np.float32)

for start in range(0, len(docs), BATCH_SIZE):
    batch      = docs[start : start + BATCH_SIZE]
    batch_end  = start + len(batch)

    encoded = tokenizer(
        batch,
        padding    = "max_length",
        max_length = MAX_SEQ_LEN,
        truncation = True,
        return_tensors = "np",
    )

    input_ids      = encoded["input_ids"].astype(np.int64)
    attention_mask = encoded["attention_mask"].astype(np.int64)
    token_type_ids = encoded.get("token_type_ids",
                                 np.zeros_like(input_ids)).astype(np.int64)

    outputs = sess.run(
        ["sentence_embedding"],
        {
            "input_ids":      input_ids,
            "attention_mask": attention_mask,
            "token_type_ids": token_type_ids,
        },
    )

    embeddings = outputs[0]  # (batch, 384)

    # Verify unit norm (mean pool + L2 norm baked into ONNX graph)
    norms = np.linalg.norm(embeddings, axis=1)
    assert np.allclose(norms, 1.0, atol=1e-4), f"Non-unit norm at batch {start}: {norms}"

    all_embeddings[start:batch_end] = embeddings

    if (start // BATCH_SIZE) % 5 == 0:
        pct = batch_end / len(docs) * 100
        print(f"  Embedded {batch_end}/{len(docs)} ({pct:.0f}%)")

# ── Write binary ───────────────────────────────────────────────────────────
num_docs = len(docs)
with open(OUT_PATH, "wb") as f:
    f.write(struct.pack("ii", num_docs, DIM))          # 8-byte header
    f.write(all_embeddings.astype(np.float32).tobytes())

file_mb = os.path.getsize(OUT_PATH) / 1024 / 1024
print(f"\n✓  Written {num_docs} × {DIM} float32 embeddings → {OUT_PATH} ({file_mb:.2f} MB)")
print(f"   Format: int32 num_docs | int32 dim | float32[num_docs * dim]")
