# Document Search and Information Retrieval System in C (CSIR)

> **Standard C11 | Positional Inverted Index | Okapi BM25 / TF-IDF / Basic TF | Phrase, Boolean & Wildcard Search | Binary Persistence & IR Benchmarking**

A modular, zero-dependency **Document Search and Information Retrieval Engine** implemented in **standard C11** (`-std=c11 -Wall -Wextra -Wpedantic`), built around a positional inverted index, deterministic relevance ranking (Basic TF, TF-IDF, Okapi BM25), persistent binary serialization, and an empirical benchmarking & IR evaluation suite.

Designed to satisfy all core and optional distinction specifications of **COMP1028: Programming and Algorithms** using strictly standard C libraries — no AI/ML models, no third-party libraries, and no external retrieval frameworks.

---

## 中文简介

本项目是一个基于 **标准 C11** 实现的高性能、零第三方依赖的**文档搜索与信息检索系统（CSIR）**，采用经典信息检索（IR）算法与底层数据结构构建，完全不使用机器学习模型、LLM 或外部检索框架。

### 核心特性
- **多格式文档加载**：支持 `.txt`、`.md` 与 TREC SGML `.trec` 格式，能够稳健处理空文件、缺失目录与异常输入。
- **文本预处理与分词归一化**：自动小写化、标点/连字符切分、SGML 标签过滤，以及基于二分查找（`bsearch`）的英文停用词过滤。
- **多种索引结构（含性能对照）**：
  - **主索引**：64 位 FNV-1a 哈希表（拉链法 + 动态扩容 rehash）+ 带词位置偏移数组（`positions[]`）的链表倒排表。
  - **对比索引**：链表词典倒排索引、排序数组二分查找索引，以及无索引线性扫描基线（Baseline）。
- **丰富的查询语法与排序算法**：
  - 支持单关键词/多关键词、精确短语检索（`"renewable energy"`）、布尔查询（`AND` / `OR` / `NOT`）与前缀通配符（`comput*`）。
  - 支持 **Basic TF**、**TF-IDF** 和 **Okapi BM25** 三种确定性相关性评分，并支持结果片段高亮（Snippet）。
- **二进制索引持久化（`CSIR v1`）**：支持将完整语料元数据、词典及位置倒排表序列化为 `.idx` 二进制文件，重载速度比从文本重建快约 **7.4 倍**。
- **完整评测与统计报表**：内置吞吐量/延迟/内存对比基准、IR 检索质量评测（`Precision@K`、`Recall@K`、`MRR`、`MAP@10`、`NDCG@10`）、ASCII 词频柱状图，以及 `report.txt` / `report.csv` 导出功能。

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
