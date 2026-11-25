#include "benchmark.h"
#include "persistence.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const char *const BENCH_QUERIES[] = {
    "renewable",
    "energy",
    "geothermal",
    "renewable energy",
    "quantumbanana",
    "renewable-energy",
    "energy energy",
    "algorithm hash table",
    "operating system kernel memory",
    "network tcp routing protocol",
    "database transaction indexing",
    "cybersecurity encryption authentication"
};

static const size_t BENCH_QUERY_COUNT = sizeof(BENCH_QUERIES) / sizeof(BENCH_QUERIES[0]);

bool benchmark_run_performance(const Corpus *corpus, const InvertedIndex *hash_idx,
                               const SearchConfig *cfg, int iterations,
                               BenchmarkSummary *out_summary) {
    if (!corpus || !hash_idx || !cfg || !out_summary || corpus->count == 0) return false;
    memset(out_summary, 0, sizeof(*out_summary));

    if (iterations < 1) iterations = 5;

    SearchConfig bench_cfg = *cfg;
    bench_cfg.show_snippets = false;
    bench_cfg.remove_stopwords = hash_idx->stopwords_removed;

    /* 1. Build alternative indexes (Linked-List and Sorted-Array) */
    LinkedListIndex ll;
    ll_index_init(&ll);
    ll_index_build(&ll, corpus, hash_idx->stopwords_removed);

    SortedArrayIndex sa;
    sa_index_init(&sa);
    sa_index_build_from_hash(&sa, hash_idx);

    /* Experiment A: Linear Scan (run 1 pass over all benchmark queries since disk scan is slow) */
    {
        double total_ms = 0.0, worst_ms = 0.0;
        for (size_t q = 0; q < BENCH_QUERY_COUNT; q++) {
            SearchResponse resp;
            search_linear_scan(corpus, BENCH_QUERIES[q], &bench_cfg, &resp);
            double ms = resp.query_time_sec * 1000.0;
            total_ms += ms;
            if (ms > worst_ms) worst_ms = ms;
            search_response_free(&resp);
        }
        out_summary->linear_scan.method_name = "A: Linear Scan (Baseline)";
        out_summary->linear_scan.build_time_sec = 0.0;
        out_summary->linear_scan.queries_run = BENCH_QUERY_COUNT;
        out_summary->linear_scan.total_time_ms = total_ms;
        out_summary->linear_scan.avg_latency_ms = total_ms / (double)BENCH_QUERY_COUNT;
        out_summary->linear_scan.worst_latency_ms = worst_ms;
        out_summary->linear_scan.throughput_qps = total_ms > 0.0 ? ((double)BENCH_QUERY_COUNT * 1000.0 / total_ms) : 0.0;
        out_summary->linear_scan.memory_bytes = corpus->count * sizeof(Document);
    }

    /* Experiment B: Linked-List Inverted Index */
    {
        double total_ms = 0.0, worst_ms = 0.0;
        size_t total_q = (size_t)iterations * BENCH_QUERY_COUNT;
        for (int it = 0; it < iterations; it++) {
            for (size_t q = 0; q < BENCH_QUERY_COUNT; q++) {
                SearchResponse resp;
                search_linked_list(corpus, &ll, BENCH_QUERIES[q], &bench_cfg, &resp);
                double ms = resp.query_time_sec * 1000.0;
                total_ms += ms;
                if (ms > worst_ms) worst_ms = ms;
                search_response_free(&resp);
            }
        }
        out_summary->linked_list.method_name = "B: Linked-List Inverted Index";
        out_summary->linked_list.build_time_sec = ll.build_time_sec;
        out_summary->linked_list.queries_run = total_q;
        out_summary->linked_list.total_time_ms = total_ms;
        out_summary->linked_list.avg_latency_ms = total_ms / (double)total_q;
        out_summary->linked_list.worst_latency_ms = worst_ms;
        out_summary->linked_list.throughput_qps = total_ms > 0.0 ? ((double)total_q * 1000.0 / total_ms) : 0.0;
        out_summary->linked_list.memory_bytes = ll_index_memory_usage(&ll);
    }

    /* Experiment C: Hash-Table Inverted Index */
    {
        double total_ms = 0.0, worst_ms = 0.0;
        size_t total_q = (size_t)iterations * BENCH_QUERY_COUNT;
        for (int it = 0; it < iterations; it++) {
            for (size_t q = 0; q < BENCH_QUERY_COUNT; q++) {
                SearchResponse resp;
                search_execute(corpus, hash_idx, BENCH_QUERIES[q], &bench_cfg, &resp);
                double ms = resp.query_time_sec * 1000.0;
                total_ms += ms;
                if (ms > worst_ms) worst_ms = ms;
                search_response_free(&resp);
            }
        }
        out_summary->hash_table.method_name = "C: Hash-Table Inverted Index";
        out_summary->hash_table.build_time_sec = hash_idx->build_time_sec;
        out_summary->hash_table.queries_run = total_q;
        out_summary->hash_table.total_time_ms = total_ms;
        out_summary->hash_table.avg_latency_ms = total_ms / (double)total_q;
        out_summary->hash_table.worst_latency_ms = worst_ms;
        out_summary->hash_table.throughput_qps = total_ms > 0.0 ? ((double)total_q * 1000.0 / total_ms) : 0.0;
        out_summary->hash_table.memory_bytes = index_memory_usage(hash_idx);
    }

    /* Experiment D: Sorted-Array Binary Search Index */
    {
        double total_ms = 0.0, worst_ms = 0.0;
        size_t total_q = (size_t)iterations * BENCH_QUERY_COUNT;
        for (int it = 0; it < iterations; it++) {
            for (size_t q = 0; q < BENCH_QUERY_COUNT; q++) {
                SearchResponse resp;
                search_sorted_array(corpus, &sa, BENCH_QUERIES[q], &bench_cfg, &resp);
                double ms = resp.query_time_sec * 1000.0;
                total_ms += ms;
                if (ms > worst_ms) worst_ms = ms;
                search_response_free(&resp);
            }
        }
        out_summary->sorted_array.method_name = "D: Sorted-Array Binary Search";
        out_summary->sorted_array.build_time_sec = sa.build_time_sec;
        out_summary->sorted_array.queries_run = total_q;
        out_summary->sorted_array.total_time_ms = total_ms;
        out_summary->sorted_array.avg_latency_ms = total_ms / (double)total_q;
        out_summary->sorted_array.worst_latency_ms = worst_ms;
        out_summary->sorted_array.throughput_qps = total_ms > 0.0 ? ((double)total_q * 1000.0 / total_ms) : 0.0;
        out_summary->sorted_array.memory_bytes = sa_index_memory_usage(&sa);
    }

    /* Persistence benchmark (save + reload binary index) */
    {
        const char *tmp_path = "_bench_tmp.idx";
        double save_sec = 0.0, load_sec = 0.0;
        if (persistence_save_index(tmp_path, corpus, hash_idx, &save_sec)) {
            FILE *fp = fopen(tmp_path, "rb");
            if (fp) {
                fseek(fp, 0, SEEK_END);
                long sz = ftell(fp);
                if (sz > 0) out_summary->binary_file_bytes = (size_t)sz;
                fclose(fp);
            }
            Corpus reloaded_c;
            InvertedIndex reloaded_i;
            corpus_init(&reloaded_c);
            index_init(&reloaded_i, 64);
            if (persistence_load_index(tmp_path, &reloaded_c, &reloaded_i, &load_sec)) {
                out_summary->binary_save_ms = save_sec * 1000.0;
                out_summary->binary_load_ms = load_sec * 1000.0;
            }
            corpus_free(&reloaded_c);
            index_free(&reloaded_i);
            remove(tmp_path);
        }
    }

    ll_index_free(&ll);
    sa_index_free(&sa);
    return true;
}

/* ==================== IR Retrieval Quality Evaluation ==================== */

typedef struct EvalQuery {
    int  qid;
    char text[MAX_QUERY_LEN];
} EvalQuery;

typedef struct QrelEntry {
    int      qid;
    uint32_t doc_id;
    int      rel;
} QrelEntry;

static int cmp_int_desc(const void *a, const void *b) {
    return (*(const int *)b) - (*(const int *)a);
}

bool benchmark_run_ir_eval(const Corpus *corpus, const InvertedIndex *idx,
                           const char *queries_path, const char *qrels_path,
                           ScoringMethod method, IREvalMetrics *out_metrics) {
    if (!corpus || !idx || !queries_path || !qrels_path || !out_metrics) return false;
    memset(out_metrics, 0, sizeof(*out_metrics));

    FILE *qf = fopen(queries_path, "r");
    FILE *rf = fopen(qrels_path, "r");
    if (!qf || !rf) {
        if (qf) fclose(qf);
        if (rf) fclose(rf);
        return false;
    }

    EvalQuery queries[256];
    size_t qcount = 0;
    char line[MAX_LINE_LEN];
    while (qcount < 256 && fgets(line, sizeof(line), qf)) {
        char *trimmed = csir_trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        char *tab = strchr(trimmed, '\t');
        if (!tab) continue;
        *tab = '\0';
        int qid = 0;
        if (!csir_parse_int(trimmed, &qid)) continue;
        queries[qcount].qid = qid;
        snprintf(queries[qcount].text, sizeof(queries[qcount].text), "%s", csir_trim(tab + 1));
        qcount++;
    }
    fclose(qf);

    QrelEntry qrels[4096];
    size_t rcount = 0;
    while (rcount < 4096 && fgets(line, sizeof(line), rf)) {
        char *trimmed = csir_trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#') continue;
        char *t1 = strchr(trimmed, '\t');
        if (!t1) continue;
        *t1 = '\0';
        char *t2 = strchr(t1 + 1, '\t');
        if (!t2) continue;
        *t2 = '\0';

        int qid = 0, rel = 0;
        if (!csir_parse_int(trimmed, &qid) || !csir_parse_int(t2 + 1, &rel)) continue;
        int doc_id = corpus_find_by_filename(corpus, csir_trim(t1 + 1));
        if (doc_id < 0) continue;

        qrels[rcount].qid = qid;
        qrels[rcount].doc_id = (uint32_t)doc_id;
        qrels[rcount].rel = rel;
        rcount++;
    }
    fclose(rf);

    if (qcount == 0 || rcount == 0) return false;

    SearchConfig cfg;
    search_config_default(&cfg);
    cfg.scoring = method;
    cfg.top_k = 20;
    cfg.show_snippets = false;
    cfg.remove_stopwords = idx->stopwords_removed;

    double sum_p5 = 0.0, sum_p10 = 0.0;
    double sum_r5 = 0.0, sum_r10 = 0.0;
    double sum_rr = 0.0, sum_ap10 = 0.0, sum_ndcg10 = 0.0;
    size_t evaluated = 0;

    for (size_t qi = 0; qi < qcount; qi++) {
        int qid = queries[qi].qid;

        /* Collect all relevant docs and graded relevances for this qid */
        int ideal_rels[256];
        size_t total_rel = 0;
        for (size_t ri = 0; ri < rcount; ri++) {
            if (qrels[ri].qid == qid && qrels[ri].rel > 0) {
                if (total_rel < 256) ideal_rels[total_rel] = qrels[ri].rel;
                total_rel++;
            }
        }
        if (total_rel == 0) continue;
        evaluated++;

        SearchResponse resp;
        search_execute(corpus, idx, queries[qi].text, &cfg, &resp);

        size_t rel_in_5 = 0, rel_in_10 = 0;
        double rr = 0.0;
        double ap_sum = 0.0;
        double dcg10 = 0.0;

        for (size_t rank = 0; rank < resp.returned_count && rank < 10; rank++) {
            uint32_t did = resp.results[rank].doc_id;
            int rel = 0;
            for (size_t ri = 0; ri < rcount; ri++) {
                if (qrels[ri].qid == qid && qrels[ri].doc_id == did) {
                    rel = qrels[ri].rel;
                    break;
                }
            }
            if (rel > 0) {
                if (rank < 5) rel_in_5++;
                rel_in_10++;
                if (rr == 0.0) rr = 1.0 / (double)(rank + 1);
                ap_sum += (double)rel_in_10 / (double)(rank + 1);
                double gain = (double)((1 << rel) - 1);
                dcg10 += gain / (log((double)(rank + 2)) / log(2.0));
            }
        }

        /* Compute IDCG@10 */
        size_t ideal_n = total_rel < 256 ? total_rel : 256;
        qsort(ideal_rels, ideal_n, sizeof(int), cmp_int_desc);
        double idcg10 = 0.0;
        for (size_t rank = 0; rank < ideal_n && rank < 10; rank++) {
            double gain = (double)((1 << ideal_rels[rank]) - 1);
            idcg10 += gain / (log((double)(rank + 2)) / log(2.0));
        }

        sum_p5 += (double)rel_in_5 / 5.0;
        sum_p10 += (double)rel_in_10 / 10.0;
        sum_r5 += (double)rel_in_5 / (double)total_rel;
        sum_r10 += (double)rel_in_10 / (double)total_rel;
        sum_rr += rr;
        sum_ap10 += ap_sum / (double)(total_rel < 10 ? total_rel : 10);
        sum_ndcg10 += (idcg10 > 0.0) ? (dcg10 / idcg10) : 0.0;

        search_response_free(&resp);
    }

    if (evaluated == 0) return false;
    out_metrics->num_queries = evaluated;
    out_metrics->precision_at_5 = sum_p5 / (double)evaluated;
    out_metrics->precision_at_10 = sum_p10 / (double)evaluated;
    out_metrics->recall_at_5 = sum_r5 / (double)evaluated;
    out_metrics->recall_at_10 = sum_r10 / (double)evaluated;
    out_metrics->mrr = sum_rr / (double)evaluated;
    out_metrics->map_at_10 = sum_ap10 / (double)evaluated;
    out_metrics->ndcg_at_10 = sum_ndcg10 / (double)evaluated;
    return true;
}

static void print_method_row(const MethodBenchResult *r) {
    printf("| %-30s | %8.4f | %9.4f | %10.4f | %10.1f | %9.1f |\n",
           r->method_name,
           r->build_time_sec,
           r->avg_latency_ms,
           r->worst_latency_ms,
           r->throughput_qps,
           (double)r->memory_bytes / 1024.0);
}

void benchmark_print_summary(const BenchmarkSummary *s) {
    if (!s) return;
    printf("\n==================== PERFORMANCE BENCHMARK SUMMARY ====================\n");
    printf("| %-30s | %8s | %9s | %10s | %10s | %9s |\n",
           "Method", "Build(s)", "Avg(ms)", "Worst(ms)", "QPS", "Mem(KB)");
    printf("|--------------------------------|----------|-----------|------------|------------|-----------|\n");
    print_method_row(&s->linear_scan);
    print_method_row(&s->linked_list);
    print_method_row(&s->hash_table);
    print_method_row(&s->sorted_array);
    printf("-----------------------------------------------------------------------------------------------\n");

    if (s->hash_table.avg_latency_ms > 0.0) {
        double speedup_vs_linear = s->linear_scan.avg_latency_ms / s->hash_table.avg_latency_ms;
        double speedup_vs_ll = s->linked_list.avg_latency_ms / s->hash_table.avg_latency_ms;
        printf("Hash-Table Speedup: %.1fx faster than Linear Scan, %.1fx faster than Linked-List Index\n",
               speedup_vs_linear, speedup_vs_ll);
    }

    if (s->binary_file_bytes > 0) {
        printf("\nBinary Persistence Benchmark (CSIR v%d):\n", CSIR_IDX_VERSION);
        printf("  Index File Size : %.2f KB (%zu bytes)\n",
               (double)s->binary_file_bytes / 1024.0, s->binary_file_bytes);
        printf("  Save Time       : %.3f ms\n", s->binary_save_ms);
        printf("  Reload Time     : %.3f ms (vs %.3f ms full build -> %.1fx faster reload)\n",
               s->binary_load_ms, s->hash_table.build_time_sec * 1000.0,
               s->binary_load_ms > 0.0 ? (s->hash_table.build_time_sec * 1000.0 / s->binary_load_ms) : 0.0);
    }

    if (s->has_ir_eval) {
        printf("\n==================== RETRIEVAL QUALITY EVALUATION =====================\n");
        printf("Queries evaluated: %zu\n", s->eval_bm25.num_queries);
        printf("| %-14s | %7s | %7s | %7s | %7s | %7s | %7s | %7s |\n",
               "Scoring", "P@5", "P@10", "R@5", "R@10", "MRR", "MAP@10", "NDCG@10");
        printf("|----------------|---------|---------|---------|---------|---------|---------|---------|\n");
        const IREvalMetrics *ms[3] = { &s->eval_basic_tf, &s->eval_tfidf, &s->eval_bm25 };
        const char *names[3] = { "Basic TF", "TF-IDF", "Okapi BM25" };
        for (int i = 0; i < 3; i++) {
            printf("| %-14s | %7.4f | %7.4f | %7.4f | %7.4f | %7.4f | %7.4f | %7.4f |\n",
                   names[i],
                   ms[i]->precision_at_5, ms[i]->precision_at_10,
                   ms[i]->recall_at_5, ms[i]->recall_at_10,
                   ms[i]->mrr, ms[i]->map_at_10, ms[i]->ndcg_at_10);
        }
        printf("---------------------------------------------------------------------------------------\n");
    }
}
