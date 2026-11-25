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
- **单向链表 + 双向链表 + 哈希表全覆盖**：
  - **主倒排索引（哈希表 + 单向链表）**：64 位 FNV-1a 哈希表（拉链法 + 动态扩容 rehash）+ 带词位置偏移数组（`positions[]`）的有序单向链表倒排表（支持乱序安全插入）。
  - **LRU 查询缓存与双向历史（哈希表 + 双向链表）**：通过 `prev` / `next` 双向链表与独立 FNV-1a 哈希表协同，实现 $O(1)$ 查询缓存命中提升、$O(1)$ 尾部 LRU 淘汰、$O(1)$ 任意节点摘除以及交互式历史记录正/逆序双向翻阅。
  - **对比索引**：链表词典倒排索引、排序数组二分查找索引，以及无索引线性扫描基线（Baseline）。
- **高性能查询与排序算法**：
  - 支持单关键词/多关键词、最短词驱动的单遍精确短语检索（`"renewable energy"`）、布尔查询（`AND` / `OR` / `NOT`）与前缀通配符（`comput*`）。
  - 支持 **Basic TF**、**TF-IDF** 和 **Okapi BM25** 三种确定性相关性评分，采用紧凑 24 字节累加器 + $O(M \log K)$ Top-K 最小堆选排与结果片段高亮（Snippet）。
- **二进制索引持久化（`CSIR v1`）**：支持将完整语料元数据、词典及位置倒排表序列化为 `.idx` 二进制文件，重载速度比从文本重建快约 **8.5 倍**。
- **完整评测、压力测试与统计报表**：内置 5 路吞吐量/延迟/内存对比基准、IR 检索质量评测（`Precision@K`、`Recall@K`、`MRR`、`MAP@10`、`NDCG@10`）、73 项自动化单元测试、复杂批处理/交互式压力测试脚本，以及 RFC 4180 合规的 `report.txt` / `report.csv` 导出功能。

---

## Features

### Core Capabilities
- **Multi-Format Corpus Loader** (`src/corpus.c`): Loads `.txt`, `.md`, and `.trec` (SGML-tagged) documents from any directory in deterministic sorted order; gracefully handles missing directories, unreadable files, and empty documents without crashing.
- **Deterministic Tokenizer & Normalizer** (`src/tokenizer.c`): Lowercasing, consistent splitting on punctuation/hyphens/underscores, SGML tag skipping, and optional English stopword filtering via binary search (`bsearch`) over a sorted stopword table.
- **Positional Inverted Index** (`src/index.c`):
  - **Primary**: 64-bit FNV-1a Hash Table with separate chaining and dynamic rehashing (load factor <= 0.75), mapping each unique term to an ascending singly-linked list of `PostingNode` structs storing `(doc_id, term_freq, positions[])` with safe out-of-order insertion support.
  - **Alternative Structures (for Benchmarking)**: Unsorted Linked-List vocabulary index and Sorted-Array Binary-Search vocabulary index.
- **O(1) LRU Query Cache & Bidirectional Search History** (`src/history_cache.c`):
  - **Doubly Linked List + FNV-1a Hash Table** combining `prev`/`next` MRU-to-LRU ordering with `hash_next` bucket chaining for $O(1)$ cache hits, MRU promotion, tail eviction, arbitrary node deletion, configuration-isolated cache keys, and forward/reverse interactive history navigation.
- **Optimized Query Engine** (`src/search.c`):
  - Single-term & multi-term keyword queries (with automatic punctuation normalization & repeated-term deduplication).
  - **Single-Pass Rarest-Pivot Phrase Search** using positional offsets (e.g., `"renewable energy"`).
  - **Boolean Queries** (`AND`, `OR`, `NOT`, e.g., `"renewable energy" AND solar NOT fossil`).
  - **Prefix Wildcard Search** (e.g., `comput*`).
  - **Compact 24-byte Accumulator + $O(M \log K)$ Binary Min-Heap Top-K Selection** and **Highlighted Search Snippets** (`[term]`).
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
│   ├── search.h          # Query parser, phrase/boolean/wildcard matcher, BM25/TF-IDF/TF, Min-Heap Top-K
│   ├── history_cache.h   # Doubly linked list + FNV-1a hash table O(1) LRU cache & history
│   ├── benchmark.h       # Comparative latency/throughput/memory & IR qrels evaluator
│   └── report.h          # Console tables, ASCII histograms, RFC4180 CSV & report.txt export
├── src/                  # Modular C11 implementations (1-to-1 with headers + main.c)
├── tests/
│   └── test_suite.c      # 73 automated unit & integration assertions
├── scripts/
│   └── generate_corpus.py# Deterministic corpus & IR evaluation qrels generator
├── data/
│   ├── sample_corpus/    # 63 files (60 .txt, 1 .md, 1 .trec, 1 empty), 109,087 raw words
│   ├── eval_corpus/      # 20 focused documents for IR retrieval quality evaluation
│   ├── eval_queries.txt  # 12 evaluation queries
│   ├── eval_qrels.txt    # 27 graded relevance judgments (0/1/2)
│   └── stress_tests/     # Complex batch & scripted interactive CLI stress inputs
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

### 5. Run Complex Stress Tests (Batch & Scripted Interactive CLI)
```bash
# Complex batch stress runner (LRU eviction, compound Boolean+Phrase+Wildcard, invariants, persistence)
./csir --dir data/sample_corpus --batch data/stress_tests/complex_batch_stress.txt

# Scripted full interactive CLI menu session
./csir < data/stress_tests/complex_interactive_cli.in
```

### 6. Run Full Benchmark & IR Quality Evaluation
```bash
./csir --benchmark data/sample_corpus \
       --eval data/eval_corpus data/eval_queries.txt data/eval_qrels.txt \
       --report-txt report.txt --report-csv benchmark_report.csv
```

---

## Measured Benchmark Results

Measured on `data/sample_corpus` (**63 documents**, **109,087 raw words**, **92,553 indexed tokens**, **1,047 unique terms**, **13,449 posting entries**) compiled with `gcc -std=c11 -O2`:

### Experiment A–E: Search Latency, Throughput & Memory Trade-off
| Method | Build Time (s) | Avg Latency (ms) | Worst Latency (ms) | Throughput (QPS) | Memory (KB) |
|---|---:|---:|---:|---:|---:|
| **A: Linear Scan (Baseline)** | 0.0000 | 39.5077 | 45.1040 | 25.3 | 56.6 |
| **B: Linked-List Inverted Index** | 0.2115 | 0.0511 | 1.1619 | 19,551.1 | 291.8 |
| **C: Hash-Table Inverted Index** | 0.0457 | 0.0374 | 0.1519 | 26,724.6 | 821.1 |
| **D: Sorted-Array Binary Search** | 0.0459 | 0.0323 | 0.1111 | 30,951.8 | 809.2 |
| **E: Hash+DLL LRU Cache (Hit)** | 0.0457 | 0.0063 | 0.0114 | 158,751.2 | 847.5 |

- **Speedup**: Hash-table indexed search is **1,055.8x faster** than linear scanning raw text and **4.6x faster to build** than an unsorted linked-list vocabulary index; Hash+DLL LRU Cache hits are **5.9x faster** than uncached hash-table lookups (**6,271.9x faster** than linear scan).
- **Binary Persistence (`CSIR v1`)**: `493.58 KB` on disk — **4.89 ms** to save, **5.40 ms** to reload (**8.5x faster** than rebuilding from text files).

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
