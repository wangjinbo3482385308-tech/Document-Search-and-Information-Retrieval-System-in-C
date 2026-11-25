#include "report.h"

#include <stdlib.h>
#include <string.h>

void report_print_stats(const Corpus *corpus, const InvertedIndex *idx) {
    printf("\n==================== COLLECTION & INDEX STATISTICS ====================\n");
    if (!corpus || corpus->count == 0) {
        printf("No document collection loaded.\n");
        return;
    }
    printf("Source Directory       : %s\n", corpus->source_dir);
    printf("Loaded Documents       : %zu (empty: %zu, skipped/unreadable: %zu)\n",
           corpus->count, corpus->empty_files, corpus->skipped_files);
    printf("Total Raw Words        : %llu\n", (unsigned long long)corpus->total_raw_tokens);
    printf("Total Indexed Tokens   : %llu\n", (unsigned long long)corpus->total_indexed_tokens);
    printf("Average Doc Length     : %.2f tokens\n", corpus->avg_doc_len);

    if (idx && idx->unique_terms > 0) {
        size_t mem = index_memory_usage(idx);
        double load_factor = idx->bucket_count > 0
            ? (double)idx->unique_terms / (double)idx->bucket_count
            : 0.0;
        printf("\n--- Inverted Index ---\n");
        printf("Stopword Filtering     : %s\n", idx->stopwords_removed ? "ENABLED" : "DISABLED");
        printf("Unique Vocabulary Terms: %zu\n", idx->unique_terms);
        printf("Total Posting Entries  : %zu\n", idx->total_postings);
        printf("Total Stored Positions : %llu\n", (unsigned long long)idx->total_positions);
        printf("Hash Buckets / Load    : %zu buckets (load factor %.3f)\n",
               idx->bucket_count, load_factor);
        printf("Index Build/Load Time  : %.4f s (%.2f ms)\n",
               idx->build_time_sec, idx->build_time_sec * 1000.0);
        printf("Estimated Index Memory : %.2f KB (%.2f MB)\n",
               (double)mem / 1024.0, (double)mem / (1024.0 * 1024.0));
    } else {
        printf("\nIndex Status           : NOT BUILT YET\n");
    }
    printf("=======================================================================\n");
}

void report_print_doc_table(const Corpus *corpus, size_t limit) {
    if (!corpus || corpus->count == 0) return;
    size_t n = (limit == 0 || limit > corpus->count) ? corpus->count : limit;
    printf("\n--- Document Overview (showing %zu of %zu) ---\n", n, corpus->count);
    printf("| %4s | %-24s | %8s | %8s | %7s | %-32s |\n",
           "ID", "Filename", "RawWords", "Indexed", "Unique", "Title");
    printf("|------|--------------------------|----------|----------|---------|----------------------------------|\n");
    for (size_t i = 0; i < n; i++) {
        const Document *d = &corpus->docs[i];
        char short_title[33];
        snprintf(short_title, sizeof(short_title), "%.32s", d->title);
        printf("| %4u | %-24.24s | %8u | %8u | %7u | %-32s |\n",
               d->doc_id, d->filename, d->raw_tokens, d->total_tokens, d->unique_terms, short_title);
    }
}

void report_print_top_terms_chart(const InvertedIndex *idx, size_t top_n) {
    if (!idx || idx->unique_terms == 0) return;
    size_t count = 0;
    const TermEntry **top = index_top_terms(idx, top_n, &count);
    if (!top || count == 0) return;

    uint64_t max_freq = top[0]->total_freq;
    if (max_freq == 0) max_freq = 1;

    printf("\n--- Top %zu Most Frequent Indexed Terms ---\n", count);
    for (size_t i = 0; i < count; i++) {
        const TermEntry *e = top[i];
        int bar_len = (int)((e->total_freq * 32) / max_freq);
        if (bar_len < 1) bar_len = 1;
        printf("  %2zu. %-18s | ", i + 1, e->term);
        for (int b = 0; b < bar_len; b++) putchar('#');
        printf(" (%llu occ, %u docs)\n", (unsigned long long)e->total_freq, e->doc_freq);
    }
    free(top);
}

void report_print_search_results(const Corpus *corpus, const SearchResponse *resp,
                                 const SearchConfig *cfg) {
    if (!corpus || !resp || !cfg) return;

    printf("\n==================== SEARCH RESULTS ====================\n");
    printf("Query          : \"%s\"\n", resp->original_query);
    printf("Normalized     : %s\n", resp->normalized_summary[0] ? resp->normalized_summary : "(empty)");
    printf("Scoring Method : %s\n", search_scoring_name(cfg->scoring));
    printf("Matches Found  : %zu (showing top %zu) in %.4f ms\n",
           resp->total_matches, resp->returned_count, resp->query_time_sec * 1000.0);

    if (resp->returned_count == 0) {
        printf("\nNo matching documents found.\n");
        printf("========================================================\n");
        return;
    }

    printf("\n| %4s | %4s | %-24s | %9s | %-28s |\n",
           "Rank", "ID", "Filename", "Score", "Matched Terms");
    printf("|------|------|--------------------------|-----------|------------------------------|\n");
    for (size_t i = 0; i < resp->returned_count; i++) {
        const SearchResult *r = &resp->results[i];
        const Document *d = corpus_get_doc(corpus, r->doc_id);
        printf("| %4zu | %4u | %-24.24s | %9.4f | %-28.28s |\n",
               i + 1, r->doc_id, d ? d->filename : "?", r->score, r->matched_terms);
    }

    if (cfg->show_snippets) {
        printf("\n--- Snippets ---\n");
        for (size_t i = 0; i < resp->returned_count; i++) {
            const SearchResult *r = &resp->results[i];
            const Document *d = corpus_get_doc(corpus, r->doc_id);
            printf("[%zu] %s (ID %u, Score %.4f) - %s\n",
                   i + 1, d ? d->filename : "?", r->doc_id, r->score, d ? d->title : "");
            if (r->snippet[0] != '\0') {
                printf("    %s\n", r->snippet);
            }
        }
    }
    printf("========================================================\n");
}

void report_view_document(const Corpus *corpus, const InvertedIndex *idx,
                          uint32_t doc_id, size_t preview_lines) {
    (void)idx;
    const Document *doc = corpus_get_doc(corpus, doc_id);
    if (!doc) {
        printf("Error: Invalid document ID %u.\n", doc_id);
        return;
    }

    printf("\n==================== DOCUMENT VIEWER ====================\n");
    printf("Doc ID      : %u\n", doc->doc_id);
    printf("Filename    : %s\n", doc->filename);
    printf("Path        : %s\n", doc->filepath);
    printf("Title       : %s\n", doc->title);
    printf("Size        : %zu bytes\n", doc->file_size);
    printf("Tokens      : %u raw / %u indexed / %u unique\n",
           doc->raw_tokens, doc->total_tokens, doc->unique_terms);
    printf("---------------------------------------------------------\n");

    char *text = corpus_read_document_text(doc, NULL);
    if (!text) {
        printf("(Unable to read file from disk)\n");
        return;
    }
    if (text[0] == '\0') {
        printf("(Empty document)\n");
        free(text);
        return;
    }

    size_t lines = 0;
    const char *p = text;
    while (*p) {
        putchar(*p);
        if (*p == '\n') {
            lines++;
            if (preview_lines > 0 && lines >= preview_lines) {
                printf("\n... [preview truncated at %zu lines] ...\n", preview_lines);
                break;
            }
        }
        p++;
    }
    if (lines == 0 || *(p - 1) != '\n') putchar('\n');
    printf("=========================================================\n");
    free(text);
}

bool report_export_txt(const char *filepath, const Corpus *corpus,
                       const InvertedIndex *idx, const SearchResponse *last_search,
                       const SearchConfig *cfg, const BenchmarkSummary *bench) {
    if (!filepath || !corpus) return false;
    FILE *fp = fopen(filepath, "w");
    if (!fp) return false;

    fprintf(fp, "=================================================================\n");
    fprintf(fp, "  Document Search & Information Retrieval System Report (v%s)\n", CSIR_VERSION);
    fprintf(fp, "=================================================================\n\n");

    fprintf(fp, "[1] Collection & Index Statistics\n");
    fprintf(fp, "  Source Directory     : %s\n", corpus->source_dir);
    fprintf(fp, "  Document Count       : %zu (empty: %zu, skipped: %zu)\n",
            corpus->count, corpus->empty_files, corpus->skipped_files);
    fprintf(fp, "  Total Raw Tokens     : %llu\n", (unsigned long long)corpus->total_raw_tokens);
    fprintf(fp, "  Total Indexed Tokens : %llu\n", (unsigned long long)corpus->total_indexed_tokens);
    fprintf(fp, "  Average Doc Length   : %.2f\n", corpus->avg_doc_len);
    if (idx && idx->unique_terms > 0) {
        fprintf(fp, "  Unique Terms         : %zu\n", idx->unique_terms);
        fprintf(fp, "  Total Postings       : %zu\n", idx->total_postings);
        fprintf(fp, "  Total Positions      : %llu\n", (unsigned long long)idx->total_positions);
        fprintf(fp, "  Stopword Filtering   : %s\n", idx->stopwords_removed ? "Enabled" : "Disabled");
        fprintf(fp, "  Build Time           : %.4f s\n", idx->build_time_sec);
        fprintf(fp, "  Memory Usage         : %.2f KB\n", (double)index_memory_usage(idx) / 1024.0);
    }

    if (last_search && last_search->original_query[0] != '\0') {
        fprintf(fp, "\n[2] Last Search Results\n");
        fprintf(fp, "  Query          : \"%s\"\n", last_search->original_query);
        fprintf(fp, "  Normalized     : %s\n", last_search->normalized_summary);
        fprintf(fp, "  Scoring Method : %s\n", cfg ? search_scoring_name(cfg->scoring) : "N/A");
        fprintf(fp, "  Total Matches  : %zu (returned %zu)\n",
                last_search->total_matches, last_search->returned_count);
        fprintf(fp, "  Query Latency  : %.4f ms\n\n", last_search->query_time_sec * 1000.0);

        for (size_t i = 0; i < last_search->returned_count; i++) {
            const SearchResult *r = &last_search->results[i];
            const Document *d = corpus_get_doc(corpus, r->doc_id);
            fprintf(fp, "  #%zu | DocID=%u | File=%s | Score=%.4f | Matched=[%s]\n",
                    i + 1, r->doc_id, d ? d->filename : "?", r->score, r->matched_terms);
            if (r->snippet[0]) {
                fprintf(fp, "       Snippet: %s\n", r->snippet);
            }
        }
    }

    if (bench && bench->hash_table.queries_run > 0) {
        fprintf(fp, "\n[3] Benchmark Comparison\n");
        const MethodBenchResult *ms[4] = {
            &bench->linear_scan, &bench->linked_list, &bench->hash_table, &bench->sorted_array
        };
        for (int i = 0; i < 4; i++) {
            fprintf(fp, "  %-30s | Build=%.4fs | Avg=%.4fms | Worst=%.4fms | QPS=%.1f | Mem=%.1fKB\n",
                    ms[i]->method_name, ms[i]->build_time_sec,
                    ms[i]->avg_latency_ms, ms[i]->worst_latency_ms,
                    ms[i]->throughput_qps, (double)ms[i]->memory_bytes / 1024.0);
        }
        if (bench->has_ir_eval) {
            fprintf(fp, "\n[4] IR Retrieval Quality Evaluation (%zu queries)\n", bench->eval_bm25.num_queries);
            fprintf(fp, "  Basic TF   : P@5=%.4f P@10=%.4f R@5=%.4f R@10=%.4f MRR=%.4f MAP@10=%.4f NDCG@10=%.4f\n",
                    bench->eval_basic_tf.precision_at_5, bench->eval_basic_tf.precision_at_10,
                    bench->eval_basic_tf.recall_at_5, bench->eval_basic_tf.recall_at_10,
                    bench->eval_basic_tf.mrr, bench->eval_basic_tf.map_at_10, bench->eval_basic_tf.ndcg_at_10);
            fprintf(fp, "  TF-IDF     : P@5=%.4f P@10=%.4f R@5=%.4f R@10=%.4f MRR=%.4f MAP@10=%.4f NDCG@10=%.4f\n",
                    bench->eval_tfidf.precision_at_5, bench->eval_tfidf.precision_at_10,
                    bench->eval_tfidf.recall_at_5, bench->eval_tfidf.recall_at_10,
                    bench->eval_tfidf.mrr, bench->eval_tfidf.map_at_10, bench->eval_tfidf.ndcg_at_10);
            fprintf(fp, "  Okapi BM25 : P@5=%.4f P@10=%.4f R@5=%.4f R@10=%.4f MRR=%.4f MAP@10=%.4f NDCG@10=%.4f\n",
                    bench->eval_bm25.precision_at_5, bench->eval_bm25.precision_at_10,
                    bench->eval_bm25.recall_at_5, bench->eval_bm25.recall_at_10,
                    bench->eval_bm25.mrr, bench->eval_bm25.map_at_10, bench->eval_bm25.ndcg_at_10);
        }
    }

    fclose(fp);
    return true;
}

bool report_export_csv(const char *filepath, const Corpus *corpus,
                       const SearchResponse *last_search,
                       const BenchmarkSummary *bench) {
    if (!filepath || !corpus) return false;
    FILE *fp = fopen(filepath, "w");
    if (!fp) return false;

    if (last_search && last_search->returned_count > 0) {
        fprintf(fp, "rank,doc_id,filename,score,matched_terms\n");
        for (size_t i = 0; i < last_search->returned_count; i++) {
            const SearchResult *r = &last_search->results[i];
            const Document *d = corpus_get_doc(corpus, r->doc_id);
            fprintf(fp, "%zu,%u,\"%s\",%.6f,\"%s\"\n",
                    i + 1, r->doc_id, d ? d->filename : "", r->score, r->matched_terms);
        }
    } else if (bench && bench->hash_table.queries_run > 0) {
        fprintf(fp, "method,build_sec,avg_latency_ms,worst_latency_ms,throughput_qps,memory_kb\n");
        const MethodBenchResult *ms[4] = {
            &bench->linear_scan, &bench->linked_list, &bench->hash_table, &bench->sorted_array
        };
        for (int i = 0; i < 4; i++) {
            fprintf(fp, "\"%s\",%.6f,%.6f,%.6f,%.2f,%.2f\n",
                    ms[i]->method_name, ms[i]->build_time_sec,
                    ms[i]->avg_latency_ms, ms[i]->worst_latency_ms,
                    ms[i]->throughput_qps, (double)ms[i]->memory_bytes / 1024.0);
        }
    } else {
        fprintf(fp, "doc_id,filename,file_size,raw_tokens,indexed_tokens,unique_terms\n");
        for (size_t i = 0; i < corpus->count; i++) {
            const Document *d = &corpus->docs[i];
            fprintf(fp, "%u,\"%s\",%zu,%u,%u,%u\n",
                    d->doc_id, d->filename, d->file_size,
                    d->raw_tokens, d->total_tokens, d->unique_terms);
        }
    }

    fclose(fp);
    return true;
}
