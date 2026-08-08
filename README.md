# 🔍 Adaptive Search Engine (Production C++17)

![CI](https://github.com/supreetpatil/adaptive-search-engine/actions/workflows/ci.yml/badge.svg)

A high-performance, multi-threaded Information Retrieval (IR) and Hybrid Search Engine implemented in C++17.
Features a dual-path hybrid retrieval pipeline combining **BM25 lexical search** and **HNSW / ONNX dense vector embeddings** via **Reciprocal Rank Fusion (RRF)**, **WAND top-K candidate pruning**, **positional inverted index with skip lists**, **spell correction & Porter stemming**, and **thread-safe reader-writer concurrency**.

---

## 🏛️ System Architecture

```
                                    ┌────────────────────────┐
                                    │    Query Input Text    │
                                    └───────────┬────────────┘
                                                │
                     ┌──────────────────────────┴──────────────────────────┐
                     ▼                                                     ▼
        ┌─────────────────────────┐                           ┌─────────────────────────┐
        │  Lexical Retrieval Path │                           │  Dense Retrieval Path   │
        └────────────┬────────────┘                           └────────────┬────────────┘
                     │                                                     │
                     ▼                                                     ▼
        ┌─────────────────────────┐                           ┌─────────────────────────┐
        │  Spell-Checker & Stem   │                           │       OrtEmbedder       │
        │ Edit-Dist Vocab Scan +  │                           │   ONNX Runtime C++ API  │
        │ Porter Stemmer (1a-5b)  │                           │ all-MiniLM-L6-v2 (384d) │
        └────────────┬────────────┘                           └────────────┬────────────┘
                     │                                                     │
                     ▼                                                     ▼
        ┌─────────────────────────┐                           ┌─────────────────────────┐
        │     Positional Index    │                           │        HNSW Index       │
        │   Skip Lists (step=8)   │                           │ Hierarchical Navigable  │
        │ WAND Pruning (maxScore) │                           │ Small World O(log N)    │
        └────────────┬────────────┘                           └────────────┬────────────┘
                     │                                                     │
                     ▼                                                     ▼
        ┌─────────────────────────┐                           ┌─────────────────────────┐
        │ BM25(k1=1.5, b=0.75)    │                           │  Top-20 Dense Vector    │
        │ Top-20 Lexical Results  │                           │       Candidates        │
        └────────────┬────────────┘                           └────────────┬────────────┘
                     │                                                     │
                     └──────────────────────────┬──────────────────────────┘
                                                │
                                                ▼
                                   ┌─────────────────────────┐
                                   │       RRFFusion         │
                                   │  Score(d) = Σ 1/(60+r)  │
                                   └────────────┬────────────┘
                                                │
                                                ▼
                                   ┌─────────────────────────┐
                                   │    LRUCache / Output    │
                                   │ Top-10 Ranked Results   │
                                   └─────────────────────────┘
```

---

## ⚡ Performance Benchmarks & Quality Evaluation

All benchmarks measured on Apple Silicon (M-series, C++17, Release build):

### 1. WAND Candidate Pruning Latency & Tail Latency (2,000 Documents, 500 Samples)
| Query Mode | Mean Latency | P50 Latency | P95 Latency | P99 Latency | Speedup (Mean / P99) |
|---|---|---|---|---|---|
| **Unpruned Lexical Scoring** | 3,069.72 µs | 854.65 µs | 8,329.11 µs | 8,757.52 µs | 1.00x (Baseline) |
| **WAND Top-K Pruning** | **53.03 µs** | **47.42 µs** | **70.11 µs** | **82.51 µs** | **57.9x / 106.1x Faster** |

### 2. Search Relevance Quality (NDCG@10 on 10,000 Documents)
| Retrieval Strategy | Mean NDCG@10 | Quality Gain |
|---|---|---|
| **BM25 Lexical Only** | 0.8631 | Baseline |
| **Hybrid RRF (BM25 + ONNX Dense)** | **0.9597** | **+11.19% Overall NDCG Gain** |

*Note: For queries with vocabulary mismatch (e.g., `"cybersecurity cryptography ledger privacy"`), Hybrid RRF achieved **+6,875% NDCG@10 gain** over pure BM25 by retrieving semantically relevant passages without exact term overlap.*

### 3. Multi-Threaded Concurrency (Reader-Writer Stress Test)
- **Workload**: 4 concurrent Writer threads (indexing new documents continuously) + 8 concurrent Reader threads (evaluating WAND queries).
- **Result**: **2,358 writes** + **2,802 reads** completed under load with **0 deadlocks** and **0 data races** under `-fsanitize=address,undefined`.

---

## ⚖️ Key Architectural Components

### 1. Approximate Nearest Neighbors via HNSW (`embed/hnsw_index.h`)
- Implements Hierarchical Navigable Small World graphs (Malkov & Yashunin 2018).
- Replaces brute-force $O(N \cdot D)$ cosine similarity scanning with $O(\log N)$ beam-search graph traversal.
- Binary graph persistence with configurable $M=16$ and $ef_{construction}=200$.

### 2. Query Expansion & Lexical Normalization (`query/`)
- **Full 5-Step Porter Stemmer**: Handles suffix stripping rules (Steps 1a–5b), reducing variations like `"generalization"` $\rightarrow$ `"general"`.
- **Vocabulary-Aware Spell Checker**: Dynamically scans indexed terms using Levenshtein distance with length pre-filtering to correct misspellings (e.g. `"lerning"` $\rightarrow$ `"learning"`).

### 3. Hybrid Retrieval & Reciprocal Rank Fusion (`embed/rrf_fusion.h`)
- Combines ranked lists from BM25 lexical search and HNSW dense vector search using $RRF(d) = \sum \frac{1}{k + r(d)}$ with $k=60$.
- Eliminates scale normalization issues between lexical and vector scores.

---

## 🛠️ Build & Usage Instructions

### Build All Executables & Run Tests
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

### Build Offline HNSW Vector Graph
```bash
./build/build_hnsw data/embeddings.bin data/hnsw.bin
```

### Executables
1. **Interactive Search CLI** (supports hybrid search, `bm25 <q>`, `dense <q>`, `phrase <q>`, `click <rank>`):
   ```bash
   ./build/adaptive-search-engine data/documents.txt data/embeddings.bin
   ```
2. **WAND Pruning Latency Benchmark**:
   ```bash
   ./build/benchmark_pruning
   ```
3. **Query Throughput (QPS) Benchmark**:
   ```bash
   ./build/benchmark_throughput
   ```
4. **Concurrency Stress Test**:
   ```bash
   ./build/test_concurrency
   ```
5. **NDCG@10 IR Evaluation**:
   ```bash
   ./build/evaluator data/corpus_10k.txt data/embeddings_10k.bin
   ```
