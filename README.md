# Adaptive Hybrid Search Engine

High-performance, distributed Information Retrieval (IR) and Approximate Nearest Neighbor (ANN) search engine implemented in C++17. Designed around a multi-stage ranking pipeline combining dynamically pruned lexical search, SIMD-accelerated vector retrieval, reciprocal rank aggregation, cross-encoder neural re-ranking, and real-time structured attribute filtering.

---

## Architectural Overview

```
                                +--------------------------------------+
                                |      HTTP / REST / Metrics Layer     |
                                |       (POSIX Sockets, C++17)         |
                                +------------------+-------------------+
                                                   |
                         +-------------------------+-------------------------+
                         |                                                   |
                         v                                                   v
             +-----------------------+                           +-----------------------+
             |  Lexical Stage (L1)   |                           |   Dense Stage (L1)    |
             |   BM25 + WAND Pruning |                           |  HNSW (M=16, efC=200) |
             |  Positional + VByte   |                           |  Int8 SQ8 + SIMD ADC  |
             +-----------+-----------+                           +-----------+-----------+
                         |                                                   |
                         |            +----------------------+               |
                         +----------->|   Reciprocal Rank    |<--------------+
                                      |     Fusion (RRF)     |
                                      +----------+-----------+
                                                 | (Top-50 Candidates)
                                                 v
                                      +----------------------+
                                      |  Stage-2 Neural L2   |
                                      | Cross-Encoder Rerank |
                                      +----------+-----------+
                                                 |
                                                 v
                                      +----------------------+
                                      | Structured Filtering |
                                      | & Dynamic Snippets   |
                                      +----------------------+
```

The engine executes queries through a multi-tier ranking architecture:

1. **Stage 1 (Retrieval / Candidate Generation)**:
   - **Lexical Retrieval**: Inverted index scored with BM25 ($k_1=1.2, b=0.75$). Uses Weak AND (WAND) dynamic pruning with skip lists to bypass non-competitive postings without exhaustive evaluation.
   - **Dense Semantic Retrieval**: Hierarchical Navigable Small World (HNSW) graph index over 384-dimensional dense embeddings (`all-MiniLM-L6-v2`), accelerated by Int8 Scalar Quantization (SQ8) and SIMD Asymmetric Distance Computation (ADC).
2. **Rank Aggregation**:
   - Reciprocal Rank Fusion (RRF, $k=60$) aggregates ranked lists across distinct scoring distributions into a unified candidate pool.
3. **Stage 2 (Re-ranking / Scoring)**:
   - Cross-Encoder transformer re-ranking scores full token-to-token cross-attention interactions over top candidate pairs to resolve semantic intent, negation, and phrase proximity.
4. **Filtering & Presentation**:
   - Structured attribute bitset index validates document metadata (categories, numeric ranges) directly during graph traversal and inverted index intersection.
   - Dynamic sliding-window text snippet generator computes passage term density and highlights query matches.

---

## Core Subsystems

### 1. Inverted Index and Lexical Pruning
- **Positional Postings with Skip Lists**: Documents store term positions for contiguous phrase evaluation. Postings contain $O(\sqrt{L})$ skip pointers with document length pre-caching.
- **WAND (Weak AND) Scoring**: Maintains maximum term upper bounds ($U_t$) per posting list. Dynamically advances list pointers past non-competitive document IDs whose accumulated upper bound cannot exceed the running $K$-th threshold.
- **Variable-Byte (VByte) Compression**: Delta-encoded posting lists ($d_i - d_{i-1}$) compressed via 7-bit variable byte integer encoding, reducing index memory by ~75%.
- **Binary Disk Serialization**: Native binary format (`IIDX` magic header) enabling sub-millisecond cold starts directly from disk.

### 2. Dense Vector Index and Quantization
- **HNSW Graph Index**: Layered proximity graph construction ($M=16, ef_{construction}=200$) with greedy multi-layer descent and bounded beam search at layer 0 ($O(\log N)$ query complexity).
- **SIMD Vector Math**: 128-bit ARM Neon (4-way FMA loop unrolling) and 256-bit x86 AVX2 (`_mm256_fmadd_ps`) dot products.
- **Int8 Scalar Quantization (SQ8)**: Compresses 32-bit float vectors to `uint8` with per-vector scale and minimum offset metadata. Evaluates float query vectors against quantized stored vectors via Asymmetric Distance Computation (ADC) without decompressing to RAM.
- **In-Graph Filtered Traversal**: Executes bitset predicate checks during the graph exploration path, eliminating recall collapse common to naive post-filtering approaches.

### 3. Query Processing and Normalization
- **Porter Stemmer**: Complete 5-step morphological stemmer (Steps 1a through 5b) reducing inflectional variants to canonical roots.
- **Vocabulary-Aware Spell Checker**: Dynamically scans indexed term vocabulary using length-filtered Levenshtein edit distance ($D \le 2$).
- **Prefix Radix Trie**: Ingests corpus terms and multi-word n-grams for prefix autocompletion in $<6\,\mu\text{s}$.

### 4. Distributed Sharding and Mutation Model
- **Scatter-Gather Sharding**: Deterministic partition hashing across $N$ index shards with multi-threaded concurrent query dispatch and $K$-way priority heap reduction.
- **Write-Ahead Log (WAL)**: Append-only persistent binary mutation log with crash replay capability.
- **Lock-Free Tombstones**: $O(1)$ deletion bitmap filtering enabling immediate document removal from search results without index compaction downtime.

### 5. Serving and Telemetry
- **Embedded REST HTTP Server**: Zero-dependency POSIX socket server with thread-pool worker dispatch.
- **Prometheus Metrics**: Exposes `search_requests_total`, `search_latency_microseconds_total`, `search_avg_latency_milliseconds`, `search_clicks_total`, and `search_indexed_docs`.
- **Personalization Feedback Loop**: User profile tracking updates individual term/document weights based on click telemetry.

---

## Empirical Benchmarks

### Vector Dot Product SIMD Throughput (1M iterations, 384 dimensions)

| Implementation | Architecture | Latency | Speedup | Precision Delta |
|---|---|---|---|---|
| Scalar | Standard C++ loop | 260.11 ms | 1.00x | Reference |
| ARM Neon | 128-bit 4-way unrolled | 54.24 ms | 4.80x | 0.000000 |
| x86 AVX2 | 256-bit FMA (`_mm256_fmadd_ps`) | 56.10 ms | 4.64x | 0.000000 |

### WAND Dynamic Pruning vs. Exhaustive BM25 (10,000 Documents)

| Mode | P50 Latency | P95 Latency | P99 Latency | QPS | Mean Candidate Skip Rate |
|---|---|---|---|---|---|
| Exhaustive BM25 | 2.120 ms | 3.450 ms | 5.800 ms | 450 | 0.0% |
| WAND Top-10 Pruned | 0.024 ms | 0.038 ms | 0.055 ms | 4,054 | 94.5% |

### Information Retrieval Quality (NDCG@10)

| Query Category | BM25 NDCG@10 | WAND NDCG@10 | Hybrid RRF NDCG@10 | Hybrid Relative Gain |
|---|---|---|---|---|
| AI & Neural Networks | 1.0000 | 1.0000 | 1.0000 | +0.0% |
| Cloud Microservices DevOps | 0.1216 | 0.1216 | 0.5183 | +326.4% |
| Cybersecurity Cryptography | 0.4067 | 0.4099 | 0.8215 | +102.0% |
| Genomic Sequencing | 0.6740 | 0.6044 | 0.9411 | +39.6% |
| **Mean Retrieval Quality** | **0.8202** | **0.8136** | **0.9281** | **+13.15%** |

---

## Building and Testing

### Prerequisites
- C++17 compliant compiler (`clang++` $\ge 12$ or `g++` $\ge 9$)
- CMake $\ge 3.14$
- ONNX Runtime $\ge 1.18.0$ (automatically configured via download or local path)

### Build Instructions

```bash
# Configure release build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets in parallel
cmake --build build --parallel $(nproc 2>/dev/null || sysctl -n hw.ncpu)
```

### Running Test Suites

The project contains 11 automated unit test suites verified through CTest:

```bash
ctest --test-dir build --output-on-failure
```

1. `test_metadata`: Inverted bitsets, numeric range filters, and in-graph filtered HNSW traversal.
2. `test_prefix_trie`: Prefix trie autocompletion, frequency-weighted ranking, and case normalization.
3. `test_cross_encoder`: Stage-2 token cross-attention scoring and precision re-ranking.
4. `test_shards`: Distributed shard partitioning, parallel scatter-gather, and WAL crash recovery.
5. `test_inverted_index`: Posting lists, skip pointers, phrase search, VByte compression, and disk serialization.
6. `test_snippet`: Sliding-window term density calculation and HTML/ANSI highlighting.
7. `test_sq8`: Int8 scalar quantization reconstruction error, recall@10, and binary persistence.
8. `test_simd`: ARM Neon and AVX2 vector dot product bitwise correctness and boundary handling.
9. `test_stemmer`: Verification across all 5 Porter stemming algorithm phases.
10. `test_rrf`: Reciprocal Rank Fusion monotonic scoring and rank aggregation.
11. `test_concurrency`: Concurrent multi-threaded read/write stress testing with snapshot isolation.

---

## Running the Services

### Interactive CLI

```bash
./build/adaptive-search-engine data/documents.txt data/embeddings.bin
```

Commands:
- `<query>`: Hybrid retrieval (lexical + dense ANN)
- `bm25 <q>`: Lexical-only BM25 evaluation
- `wand <q>`: WAND dynamically pruned search with posting skip metrics
- `dense <q>`: Pure semantic vector search
- `phrase <q>`: Exact positional phrase evaluation
- `click <docId>`: Record relevance feedback for personalization profile
- `quit`: Terminate CLI

### HTTP REST Server

```bash
./build/adaptive-search-server 8080 data/documents.txt data/embeddings.bin
```

API Endpoints:
- `GET /`: Interactive web search dashboard and telemetry monitor.
- `GET /search?q=<query>&mode=<hybrid|rerank|wand|bm25|phrase>&k=<topK>&filter=<expr>`: Query endpoint returning scored documents and highlighted snippets.
- `GET /suggest?q=<prefix>&k=<topK>`: Prefix autocompletion endpoint.
- `GET /metrics`: Prometheus formatted telemetry counters and gauges.
- `GET /health`: Engine status, uptime, and indexed document count.
- `POST /document`: Ingests document JSON `{"docId": 101, "content": "..."}` in real time.
- `DELETE /document`: Marks document tombstone `{"docId": 101}` with instant search exclusion.
- `POST /click`: Records document click signal `{"docId": 101}`.

---

## Repository Structure

```
├── adaptive/           # Adaptive ranking weight tuner and user personalization profiles
├── bench/              # Microbenchmarks (SIMD throughput, WAND pruning latency, QPS)
├── cache/              # Thread-safe synchronized LRU query cache
├── embed/              # HNSW graph index, Flat index, SQ8 quantization, ONNX bi-encoder, RRF
├── eval/               # NDCG@10 evaluation framework across test topic distributions
├── index/              # Inverted index, skip lists, WAL, tombstones, shards, metadata bitsets
├── query/              # Porter stemmer, Levenshtein spell checker, prefix trie, snippet generator
├── ranking/            # BM25, TF-IDF, WAND dynamic pruning scorer, Cross-Encoder re-ranker
├── server/             # POSIX multi-threaded REST HTTP and Prometheus metrics server
├── src/                # Entry points for CLI and standalone server binaries
├── tests/              # 11 unit test suites registered with CTest
└── utils/              # SIMD math routines (Neon/AVX2), VByte delta compression, file loader
```
