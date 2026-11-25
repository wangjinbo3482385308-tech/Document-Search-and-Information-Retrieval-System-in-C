# CSIR — Document Search & Information Retrieval System in C

A modular, zero-dependency **Document Search and Information Retrieval Engine** implemented in **standard C11** (`-std=c11 -Wall -Wextra -Wpedantic`), built around a positional inverted index, deterministic relevance ranking (Basic TF, TF-IDF, Okapi BM25), persistent binary serialization, and an empirical benchmarking & IR evaluation suite.

Designed to satisfy all core and optional distinction specifications of **COMP1028: Programming and Algorithms** using strictly standard C libraries — no AI/ML models, no third-party libraries, and no external retrieval frameworks.

---

## Features

### Core Capabilities
- **Multi-Format Corpus Loader** (`src/corpus.c`): Loads `.txt`, `.md`, and `.trec` (SGML-tagged) documents from any directory in deterministic sorted order; gracefully handles missing directories, unreadable files, and empty documents without crashing.
- **Deterministic Tokenizer & Normalizer** (`src/tokenizer.c`): Lowercasing, consistent splitting on punctuation/hyphens/underscores, SGML tag skipping, and optional English stopword filtering via binary search (`bsearch`) over a sorted stopword table.
- **Positional Inverted Index** (`src/index.c`):
  - **Primary**: 64-bit FNV-1a Hash Table with separate chaining and dynamic rehashing (load factor <= 0.75), mapping each unique term to a linked list of `PostingNode` structs storing `(doc_id, term_freq, positions[])`.
  - **Alternative Structures (for Benchmarking)**: Unsorted Linked-List vocabulary index and Sorted-Array Binary-Search vocabulary index.
- **Rich Query Engine** (`src/search.c`):
  - Single-term & multi-term keyword queries (with automatic punctuation normalization & repeated-term deduplication).
  - **Exact Phrase Search** using positional offsets (e.g., `"renewable energy"`).
  - **Boolean Queries** (`AND`, `OR`, `NOT`, e.g., `solar AND energy NOT fossil`).
  - **Prefix Wildcard Search** (e.g., `comput*`).
  - **Highlighted Search Snippets**: Extracts context windows with `[term]` highlighting.
- **Three Deterministic Scoring Models**:
  - **Basic TF**: Sum of query-term frequencies in the document.
  - **TF-IDF**: Sublinear TF `(1 + ln(tf)) * (ln((N + 1) / (df + 1)) + 1)`.
  - **Okapi BM25** *(default)*: `k1 = 1.5, b = 0.75` length-normalized ranking.
  - Consistent deterministic tie-breaking by `doc_id` ascending.
- **Binary Index Persistence** (`src/persistence.c`): Custom documented `CSIR v1` binary file format (`fopen`/`fwrite`/`fread`/`fseek`/`ftell`) preserving corpus metadata, vocabulary, postings, and token positions for **~7.4x faster** index reloads.
- **Statistics, Visualization & Reporting** (`src/report.c`): Collection & per-document statistics, ASCII term-frequency bar charts, document viewer, and export to `report.txt` and `report.csv`.
- **Interactive Menu-Driven CLI & Non-Interactive Flags** (`src/main.c`): Safe `fgets`-based console menu plus CLI flags (`--dir`, `--query`, `--benchmark`, `--eval`).

---

## Project Structure

```text
c-doc-search-engine/
├── include/
│   ├── common.h          # Constants, high-res timer, portable dir listing, safe input
│   ├── corpus.h          # Document metadata & collection management
│   ├── tokenizer.h       # Normalization, punctuation handling, stopword binary search
│   ├── index.h           # Hash-Table, Linked-List, and Sorted-Array inverted indexes
│   ├── persistence.h     # CSIR v1 binary index save/load interface
│   ├── search.h          # Query parser, phrase/boolean/wildcard matcher, BM25/TF-IDF/TF
│   ├── benchmark.h       # Comparative latency/throughput/memory & IR qrels evaluator
│   └── report.h          # Console tables, ASCII histograms, report.txt & report.csv export
├── src/                  # Modular C11 implementations (1-to-1 with headers + main.c)
├── tests/
│   └── test_suite.c      # 37 automated unit & integration assertions
├── scripts/
│   └── generate_corpus.py# Deterministic corpus & IR evaluation qrels generator
├── data/
│   ├── sample_corpus/    # 63 files (60 .txt, 1 .md, 1 .trec, 1 empty), 109,087 raw words
│   ├── eval_corpus/      # 20 focused documents for IR retrieval quality evaluation
│   ├── eval_queries.txt  # 12 evaluation queries
│   └── eval_qrels.txt    # 27 graded relevance judgments (0/1/2)
├── docs/
│   ├── ARCHITECTURE.md   # System pipeline, data structures, scoring formulas & trade-offs
│   └── INDEX_FORMAT.md   # Binary index file specification (CSIR v1)
├── Makefile              # Cross-platform GCC build (Windows MinGW & Linux/macOS)
└── build.bat             # One-click Windows build script
```

---

## Quick Start

### 1. Build
**Windows (MinGW GCC):**
```bat
build.bat
```
or with `mingw32-make`:
```powershell
mingw32-make all
```

**Linux / macOS (GCC / Clang):**
```bash
make all
```

### 2. Run Interactive Menu CLI
```bash
./csir
```
(Or pre-load and index the sample corpus directly on startup:)
```bash
./csir --dir data/sample_corpus
```

### 3. Run Single Query from Command Line
```bash
./csir --dir data/sample_corpus --query "renewable energy"
./csir --dir data/sample_corpus --query '"renewable energy" AND solar'
./csir --dir data/sample_corpus --query "comput*"
```

### 4. Run Automated Unit & Integration Tests
```bash
mingw32-make test   # Windows
make test           # Linux/macOS
```

### 5. Run Full Benchmark & IR Quality Evaluation
```bash
./csir --benchmark data/sample_corpus \
       --eval data/eval_corpus data/eval_queries.txt data/eval_qrels.txt \
       --report-txt report.txt --report-csv benchmark_report.csv
```

---

## Measured Benchmark Results

Measured on `data/sample_corpus` (**63 documents**, **109,087 raw words**, **92,553 indexed tokens**, **1,047 unique terms**, **13,449 posting entries**) compiled with `gcc -std=c11 -O2`:

### Experiment A–D: Search Latency, Throughput & Memory Trade-off
| Method | Build Time (s) | Avg Latency (ms) | Worst Latency (ms) | Throughput (QPS) | Memory (KB) |
|---|---:|---:|---:|---:|---:|
| **A: Linear Scan (Baseline)** | 0.0000 | 39.8859 | 43.8001 | 25.1 | 56.6 |
| **B: Linked-List Inverted Index** | 0.2126 | 0.1530 | 0.7153 | 6,537.9 | 291.8 |
| **C: Hash-Table Inverted Index** | 0.0505 | 0.1393 | 0.7050 | 7,181.1 | 821.1 |
| **D: Sorted-Array Binary Search** | 0.0507 | 0.1314 | 0.6486 | 7,610.9 | 809.2 |

- **Speedup**: Hash-table indexed search is **286.4x faster** than linear scanning raw text and **4.2x faster to build** than an unsorted linked-list vocabulary index.
- **Binary Persistence (`CSIR v1`)**: `493.58 KB` on disk — **3.84 ms** to save, **6.85 ms** to reload (**7.4x faster** than rebuilding from text files).

### Retrieval Quality Evaluation (`data/eval_corpus`, 12 Queries, Graded Qrels)
| Scoring Model | P@5 | P@10 | R@5 | R@10 | MRR | MAP@10 | NDCG@10 |
|---|---:|---:|---:|---:|---:|---:|---:|
| **Basic TF** | 0.3500 | 0.1750 | 0.7639 | 0.7639 | 1.0000 | 0.7500 | 0.8996 |
| **TF-IDF** | 0.3500 | 0.1750 | 0.7639 | 0.7639 | 1.0000 | 0.7500 | 0.9145 |
| **Okapi BM25** | 0.3500 | 0.1750 | 0.7639 | 0.7639 | 1.0000 | 0.7500 | 0.9145 |

---

## Documentation
- [Architecture, Algorithms & Trade-offs](docs/ARCHITECTURE.md)
- [Binary Index File Format Specification (`CSIR v1`)](docs/INDEX_FORMAT.md)
