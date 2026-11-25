#ifndef CSIR_BENCHMARK_H
#define CSIR_BENCHMARK_H

#include "common.h"
#include "corpus.h"
#include "index.h"
#include "search.h"

typedef struct MethodBenchResult {
    const char *method_name;
    double      build_time_sec;
    double      avg_latency_ms;
    double      worst_latency_ms;
    double      total_time_ms;
    double      throughput_qps;
    size_t      memory_bytes;
    size_t      queries_run;
} MethodBenchResult;

typedef struct IREvalMetrics {
    size_t num_queries;
    double precision_at_5;
    double precision_at_10;
    double recall_at_5;
    double recall_at_10;
    double mrr;
    double map_at_10;
    double ndcg_at_10;
} IREvalMetrics;

typedef struct BenchmarkSummary {
    MethodBenchResult linear_scan;
    MethodBenchResult linked_list;
    MethodBenchResult hash_table;
    MethodBenchResult sorted_array;
    MethodBenchResult lru_cache_hit;
    double            binary_save_ms;
    double            binary_load_ms;
    size_t            binary_file_bytes;
    bool              has_ir_eval;
    IREvalMetrics     eval_basic_tf;
    IREvalMetrics     eval_tfidf;
    IREvalMetrics     eval_bm25;
} BenchmarkSummary;

/* Run full comparative performance benchmark (Linear vs Linked-List vs Hash-Table vs Sorted-Array)
 * plus binary persistence benchmark. */
bool benchmark_run_performance(const Corpus *corpus, const InvertedIndex *hash_idx,
                               const SearchConfig *cfg, int iterations,
                               BenchmarkSummary *out_summary);

/* Evaluate retrieval quality (Precision@K, Recall@K, MRR, MAP@10, NDCG@10)
 * given a queries file ("<qid>\t<query>") and qrels file ("<qid>\t<filename>\t<rel>"). */
bool benchmark_run_ir_eval(const Corpus *corpus, const InvertedIndex *idx,
                           const char *queries_path, const char *qrels_path,
                           ScoringMethod method, IREvalMetrics *out_metrics);

/* Print formatted benchmark tables to stdout */
void benchmark_print_summary(const BenchmarkSummary *summary);

#endif /* CSIR_BENCHMARK_H */
