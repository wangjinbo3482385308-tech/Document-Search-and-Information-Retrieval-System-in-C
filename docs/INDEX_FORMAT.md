# CSIR v1 Binary Index File Format Specification

The persistent index module (`src/persistence.c`) serializes both the `Corpus` metadata table and the full positional `InvertedIndex` to a compact binary `.idx` file using standard C I/O (`fopen`, `fwrite`, `fread`, `fseek`, `ftell`).

## Binary Layout (Little-Endian Host Native)

### 1. Header
| Field | Type | Size (bytes) | Description |
|---|---|---:|---|
| `magic` | `char[4]` | 4 | ASCII magic bytes `"CSIR"` |
| `version` | `uint32_t` | 4 | Format version (`1`) |
| `flags` | `uint32_t` | 4 | Bit 0: `1` if stopwords were removed during indexing |
| `doc_count` | `uint32_t` | 4 | Number of documents ($N$) |
| `unique_terms` | `uint32_t` | 4 | Number of unique vocabulary terms ($V$) |
| `total_raw_tokens` | `uint64_t` | 8 | Total raw tokens across all documents |
| `total_indexed_tokens` | `uint64_t` | 8 | Total indexed tokens after stopword filtering |
| `avg_doc_len` | `double` | 8 | Mean indexed document length (used by BM25) |
| `source_dir` | `String` | $4 + L$ | Length-prefixed (`uint32_t` + raw bytes) directory path |

### 2. Document Table (`doc_count` entries)
For each document $i \in [0, N-1]$:
- `doc_id` (`uint32_t`)
- `file_size` (`uint64_t`)
- `total_tokens` (`uint32_t`)
- `raw_tokens` (`uint32_t`)
- `unique_terms` (`uint32_t`)
- `is_empty` (`uint32_t`: `0` or `1`)
- `filename` (`String`: `uint32_t len` + `len` bytes)
- `filepath` (`String`: `uint32_t len` + `len` bytes)
- `title` (`String`: `uint32_t len` + `len` bytes)

### 3. Vocabulary & Positional Postings (`unique_terms` entries)
For each unique term $t \in [0, V-1]$:
- `term` (`String`: `uint32_t len` + `len` bytes)
- `doc_freq` (`uint32_t`): number of posting entries following
- For each posting $p \in [0, \text{doc\_freq}-1]$:
  - `doc_id` (`uint32_t`)
  - `term_freq` (`uint32_t`)
  - `positions` (`uint32_t[term_freq]`): ascending 0-based raw token offsets

## Validation & Safety
On load (`persistence_load_index`), the reader builds into temporary `Corpus` and `InvertedIndex` structs and checks:
1. Magic header equals `"CSIR"` and `version == 1`.
2. Every string length fits within fixed buffer bounds before reading.
3. Every `fread` returns the exact expected item count.

If any check fails (truncated, corrupted, or wrong file), all temporary allocations are freed and the active in-memory corpus/index remains untouched.
