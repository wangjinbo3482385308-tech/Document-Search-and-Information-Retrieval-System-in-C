#include "benchmark.h"
#include "common.h"
#include "corpus.h"
#include "index.h"
#include "persistence.h"
#include "report.h"
#include "search.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void print_banner(void) {
    printf("\n====================================================================\n");
    printf("   C Document Search & Information Retrieval Engine (CSIR v%s)\n", CSIR_VERSION);
    printf("   Standard C11 | Positional Inverted Index | BM25 / TF-IDF / TF\n");
    printf("====================================================================\n");
}

static void print_menu(const Corpus *corpus, const InvertedIndex *idx, const SearchConfig *cfg) {
    printf("\n[Status] Corpus: %zu docs | Index: %zu terms (%s) | Scoring: %s | Top-K: %zu\n",
           corpus->count,
           idx->unique_terms,
           idx->unique_terms > 0 ? "READY" : "NOT BUILT",
           cfg->scoring == SCORE_BM25 ? "BM25" : (cfg->scoring == SCORE_TFIDF ? "TF-IDF" : "Basic TF"),
           cfg->top_k);
    printf("--------------------------------------------------------------------\n");
    printf("  1. Load Document Collection (folder path)\n");
    printf("  2. Build Inverted Index\n");
    printf("  3. Save Index to Binary File (.idx)\n");
    printf("  4. Load Index from Binary File (.idx)\n");
    printf("  5. Search Documents (keyword / \"phrase\" / AND / OR / NOT / wild*)\n");
    printf("  6. Configure Search Settings (Top-K, Scoring, Snippets, Stopwords)\n");
    printf("  7. View Document (content preview & metadata)\n");
    printf("  8. Show Collection & Index Statistics (+ Top Terms Chart)\n");
    printf("  9. Run Performance Benchmark & IR Quality Evaluation\n");
    printf(" 10. Export Report (report.txt & report.csv)\n");
    printf("  0. Exit\n");
    printf("--------------------------------------------------------------------\n");
    printf("Enter choice: ");
}

static void handle_settings(SearchConfig *cfg, bool *need_rebuild) {
    char buf[128];
    printf("\n--- Search & Index Settings ---\n");
    printf("1. Scoring Method [current: %s]\n", search_scoring_name(cfg->scoring));
    printf("   (0 = Basic TF, 1 = TF-IDF, 2 = Okapi BM25)\n");
    printf("2. Top-K Results  [current: %zu]\n", cfg->top_k);
    printf("3. Show Snippets  [current: %s]\n", cfg->show_snippets ? "ON" : "OFF");
    printf("4. Stopwords      [current: %s]\n", cfg->remove_stopwords ? "REMOVE" : "KEEP");
    printf("0. Back\n");
    printf("Select setting to change: ");
    if (!csir_read_line(buf, sizeof(buf))) return;

    int opt = -1;
    if (!csir_parse_int(buf, &opt)) {
        printf("Invalid input.\n");
        return;
    }

    if (opt == 1) {
        printf("Choose scoring (0=Basic TF, 1=TF-IDF, 2=BM25): ");
        if (csir_read_line(buf, sizeof(buf))) {
            int m = -1;
            if (csir_parse_int(buf, &m) && m >= 0 && m <= 2) {
                cfg->scoring = (ScoringMethod)m;
                printf("Scoring updated to: %s\n", search_scoring_name(cfg->scoring));
            } else {
                printf("Invalid scoring option.\n");
            }
        }
    } else if (opt == 2) {
        printf("Enter Top-K (1-100): ");
        if (csir_read_line(buf, sizeof(buf))) {
            int k = 0;
            if (csir_parse_int(buf, &k) && k >= 1 && k <= 100) {
                cfg->top_k = (size_t)k;
                printf("Top-K updated to %zu.\n", cfg->top_k);
            } else {
                printf("Invalid Top-K value.\n");
            }
        }
    } else if (opt == 3) {
        cfg->show_snippets = !cfg->show_snippets;
        printf("Snippets are now %s.\n", cfg->show_snippets ? "ON" : "OFF");
    } else if (opt == 4) {
        cfg->remove_stopwords = !cfg->remove_stopwords;
        *need_rebuild = true;
        printf("Stopword removal is now %s. Rebuild the index (Option 2) to apply.\n",
               cfg->remove_stopwords ? "ENABLED" : "DISABLED");
    }
}

static int run_cli_benchmark_mode(const char *corpus_dir, const char *eval_dir,
                                  const char *queries_path, const char *qrels_path,
                                  const char *export_txt, const char *export_csv) {
    Corpus corpus;
    InvertedIndex idx;
    SearchConfig cfg;
    corpus_init(&corpus);
    index_init(&idx, 4096);
    search_config_default(&cfg);

    printf("Loading corpus from: %s\n", corpus_dir);
    int n = corpus_load_dir(&corpus, corpus_dir);
    if (n <= 0) {
        fprintf(stderr, "Error: Failed to load documents from '%s'\n", corpus_dir);
        corpus_free(&corpus);
        index_free(&idx);
        return 1;
    }

    index_build(&idx, &corpus, cfg.remove_stopwords);
    report_print_stats(&corpus, &idx);
    report_print_top_terms_chart(&idx, 12);

    BenchmarkSummary summary;
    memset(&summary, 0, sizeof(summary));
    benchmark_run_performance(&corpus, &idx, &cfg, 20, &summary);

    if (eval_dir && queries_path && qrels_path) {
        Corpus eval_c;
        InvertedIndex eval_i;
        corpus_init(&eval_c);
        index_init(&eval_i, 1024);
        if (corpus_load_dir(&eval_c, eval_dir) > 0 && index_build(&eval_i, &eval_c, true)) {
            bool ok1 = benchmark_run_ir_eval(&eval_c, &eval_i, queries_path, qrels_path,
                                             SCORE_BASIC_TF, &summary.eval_basic_tf);
            bool ok2 = benchmark_run_ir_eval(&eval_c, &eval_i, queries_path, qrels_path,
                                             SCORE_TFIDF, &summary.eval_tfidf);
            bool ok3 = benchmark_run_ir_eval(&eval_c, &eval_i, queries_path, qrels_path,
                                             SCORE_BM25, &summary.eval_bm25);
            summary.has_ir_eval = ok1 && ok2 && ok3;
        }
        corpus_free(&eval_c);
        index_free(&eval_i);
    }

    benchmark_print_summary(&summary);

    if (export_txt) {
        if (report_export_txt(export_txt, &corpus, &idx, NULL, &cfg, &summary)) {
            printf("Exported text report to %s\n", export_txt);
        }
    }
    if (export_csv) {
        if (report_export_csv(export_csv, &corpus, NULL, &summary)) {
            printf("Exported CSV report to %s\n", export_csv);
        }
    }

    corpus_free(&corpus);
    index_free(&idx);
    return 0;
}

static void print_usage(const char *prog) {
    printf("Usage: %s [options]\n", prog);
    printf("  (no args)              Launch interactive menu-driven CLI\n");
    printf("  --dir <folder>         Auto-load & index a corpus folder on startup\n");
    printf("  --benchmark <folder>   Run full performance benchmark on <folder> and exit\n");
    printf("  --eval <dir> <q> <qr>  Also run IR quality eval (P@K, R@K, MRR, MAP, NDCG)\n");
    printf("  --query <text>         Run a single query against --dir and exit\n");
    printf("  --report-txt <file>    Export benchmark report to text file\n");
    printf("  --report-csv <file>    Export benchmark metrics to CSV file\n");
    printf("  --help                 Show this help message\n");
}

int main(int argc, char **argv) {
    const char *auto_dir = NULL;
    const char *bench_dir = NULL;
    const char *eval_dir = NULL;
    const char *eval_queries = NULL;
    const char *eval_qrels = NULL;
    const char *single_query = NULL;
    const char *report_txt = NULL;
    const char *report_csv = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else if (strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            auto_dir = argv[++i];
        } else if (strcmp(argv[i], "--benchmark") == 0 && i + 1 < argc) {
            bench_dir = argv[++i];
        } else if (strcmp(argv[i], "--eval") == 0 && i + 3 < argc) {
            eval_dir = argv[++i];
            eval_queries = argv[++i];
            eval_qrels = argv[++i];
        } else if (strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            single_query = argv[++i];
        } else if (strcmp(argv[i], "--report-txt") == 0 && i + 1 < argc) {
            report_txt = argv[++i];
        } else if (strcmp(argv[i], "--report-csv") == 0 && i + 1 < argc) {
            report_csv = argv[++i];
        } else {
            fprintf(stderr, "Unknown or incomplete argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (bench_dir) {
        return run_cli_benchmark_mode(bench_dir, eval_dir, eval_queries, eval_qrels,
                                      report_txt, report_csv);
    }

    Corpus corpus;
    InvertedIndex idx;
    SearchConfig cfg;
    SearchResponse last_search;
    BenchmarkSummary last_bench;
    bool has_bench = false;
    bool need_rebuild = false;

    corpus_init(&corpus);
    index_init(&idx, 4096);
    search_config_default(&cfg);
    memset(&last_search, 0, sizeof(last_search));
    memset(&last_bench, 0, sizeof(last_bench));

    if (auto_dir) {
        int loaded = corpus_load_dir(&corpus, auto_dir);
        if (loaded > 0) {
            index_build(&idx, &corpus, cfg.remove_stopwords);
        } else {
            fprintf(stderr, "Warning: could not load corpus from '%s'\n", auto_dir);
        }
    }

    if (single_query) {
        if (idx.unique_terms == 0) {
            fprintf(stderr, "Error: --query requires --dir <corpus_folder>\n");
            corpus_free(&corpus);
            index_free(&idx);
            return 1;
        }
        search_execute(&corpus, &idx, single_query, &cfg, &last_search);
        report_print_search_results(&corpus, &last_search, &cfg);
        search_response_free(&last_search);
        corpus_free(&corpus);
        index_free(&idx);
        return 0;
    }

    print_banner();
    char input[MAX_QUERY_LEN];

    while (true) {
        print_menu(&corpus, &idx, &cfg);
        if (!csir_read_line(input, sizeof(input))) {
            printf("\nEOF received. Exiting.\n");
            break;
        }

        int choice = -1;
        if (!csir_parse_int(input, &choice)) {
            printf("Invalid choice. Please enter a number between 0 and 10.\n");
            continue;
        }

        switch (choice) {
            case 1: {
                printf("Enter corpus directory path [default: data/sample_corpus]: ");
                if (!csir_read_line(input, sizeof(input))) break;
                char *dir = csir_trim(input);
                if (dir[0] == '\0') dir = "data/sample_corpus";
                int loaded = corpus_load_dir(&corpus, dir);
                if (loaded < 0) {
                    printf("Error: Unable to open directory '%s'.\n", dir);
                } else if (loaded == 0) {
                    printf("Warning: No supported text files (.txt/.md/.trec) found in '%s'.\n", dir);
                } else {
                    index_free(&idx);
                    index_init(&idx, 4096);
                    search_response_free(&last_search);
                    printf("Loaded %d documents from '%s' (empty: %zu, skipped: %zu).\n",
                           loaded, dir, corpus.empty_files, corpus.skipped_files);
                    printf("Run Option 2 to build the inverted index.\n");
                }
                break;
            }
            case 2: {
                if (corpus.count == 0) {
                    printf("Error: Load a document collection first (Option 1).\n");
                    break;
                }
                printf("Building inverted index (stopwords %s)...\n",
                       cfg.remove_stopwords ? "removed" : "kept");
                if (index_build(&idx, &corpus, cfg.remove_stopwords)) {
                    need_rebuild = false;
                    printf("Index built in %.4f s: %zu unique terms, %zu postings, %llu raw words.\n",
                           idx.build_time_sec, idx.unique_terms, idx.total_postings,
                           (unsigned long long)corpus.total_raw_tokens);
                } else {
                    printf("Error: Failed to build index.\n");
                }
                break;
            }
            case 3: {
                if (idx.unique_terms == 0) {
                    printf("Error: Build an index first (Option 2).\n");
                    break;
                }
                printf("Enter output binary index path [default: index.idx]: ");
                if (!csir_read_line(input, sizeof(input))) break;
                char *path = csir_trim(input);
                if (path[0] == '\0') path = "index.idx";
                double elapsed = 0.0;
                if (persistence_save_index(path, &corpus, &idx, &elapsed)) {
                    printf("Index saved to '%s' in %.3f ms.\n", path, elapsed * 1000.0);
                } else {
                    printf("Error: Failed to save index to '%s'.\n", path);
                }
                break;
            }
            case 4: {
                printf("Enter binary index path to load [default: index.idx]: ");
                if (!csir_read_line(input, sizeof(input))) break;
                char *path = csir_trim(input);
                if (path[0] == '\0') path = "index.idx";
                double elapsed = 0.0;
                if (persistence_load_index(path, &corpus, &idx, &elapsed)) {
                    cfg.remove_stopwords = idx.stopwords_removed;
                    need_rebuild = false;
                    search_response_free(&last_search);
                    printf("Loaded index from '%s' in %.3f ms (%zu docs, %zu unique terms).\n",
                           path, elapsed * 1000.0, corpus.count, idx.unique_terms);
                } else {
                    printf("Error: Failed to load valid CSIR index from '%s'.\n", path);
                }
                break;
            }
            case 5: {
                if (idx.unique_terms == 0) {
                    printf("Error: Build or load an index first (Option 2 or 4).\n");
                    break;
                }
                if (need_rebuild) {
                    printf("Note: Stopword setting changed; using existing index setting (%s).\n",
                           idx.stopwords_removed ? "stopwords removed" : "stopwords kept");
                }
                printf("Enter search query (supports \"phrases\", AND/OR/NOT, prefix*): ");
                if (!csir_read_line(input, sizeof(input))) break;
                char *q = csir_trim(input);
                if (q[0] == '\0') {
                    printf("Empty query ignored. Please enter at least one search term.\n");
                    break;
                }
                search_response_free(&last_search);
                if (search_execute(&corpus, &idx, q, &cfg, &last_search)) {
                    report_print_search_results(&corpus, &last_search, &cfg);
                } else {
                    printf("Error: Search execution failed.\n");
                }
                break;
            }
            case 6:
                handle_settings(&cfg, &need_rebuild);
                break;
            case 7: {
                if (corpus.count == 0) {
                    printf("Error: Load a collection first.\n");
                    break;
                }
                printf("Enter document ID (0 to %zu): ", corpus.count - 1);
                if (!csir_read_line(input, sizeof(input))) break;
                int doc_id = -1;
                if (!csir_parse_int(input, &doc_id) || doc_id < 0 || (size_t)doc_id >= corpus.count) {
                    printf("Invalid document ID.\n");
                    break;
                }
                printf("Preview lines (0 for full document) [default: 25]: ");
                size_t lines = 25;
                if (csir_read_line(input, sizeof(input))) {
                    int v = 0;
                    if (csir_parse_int(input, &v) && v >= 0) lines = (size_t)v;
                }
                report_view_document(&corpus, &idx, (uint32_t)doc_id, lines);
                break;
            }
            case 8:
                report_print_stats(&corpus, &idx);
                if (corpus.count > 0) report_print_doc_table(&corpus, 15);
                if (idx.unique_terms > 0) report_print_top_terms_chart(&idx, 12);
                break;
            case 9: {
                if (idx.unique_terms == 0) {
                    printf("Error: Build or load an index first.\n");
                    break;
                }
                printf("Running benchmark (Linear Scan vs Linked-List vs Hash-Table vs Sorted-Array)...\n");
                if (benchmark_run_performance(&corpus, &idx, &cfg, 15, &last_bench)) {
                    has_bench = true;
                    /* Also run IR quality eval if eval corpus exists */
                    Corpus eval_c;
                    InvertedIndex eval_i;
                    corpus_init(&eval_c);
                    index_init(&eval_i, 1024);
                    if (corpus_load_dir(&eval_c, "data/eval_corpus") > 0 &&
                        index_build(&eval_i, &eval_c, true)) {
                        bool ok1 = benchmark_run_ir_eval(&eval_c, &eval_i,
                                                         "data/eval_queries.txt", "data/eval_qrels.txt",
                                                         SCORE_BASIC_TF, &last_bench.eval_basic_tf);
                        bool ok2 = benchmark_run_ir_eval(&eval_c, &eval_i,
                                                         "data/eval_queries.txt", "data/eval_qrels.txt",
                                                         SCORE_TFIDF, &last_bench.eval_tfidf);
                        bool ok3 = benchmark_run_ir_eval(&eval_c, &eval_i,
                                                         "data/eval_queries.txt", "data/eval_qrels.txt",
                                                         SCORE_BM25, &last_bench.eval_bm25);
                        last_bench.has_ir_eval = ok1 && ok2 && ok3;
                    }
                    corpus_free(&eval_c);
                    index_free(&eval_i);
                    benchmark_print_summary(&last_bench);
                } else {
                    printf("Benchmark failed.\n");
                }
                break;
            }
            case 10: {
                if (corpus.count == 0) {
                    printf("Error: Load a collection first.\n");
                    break;
                }
                bool ok1 = report_export_txt("report.txt", &corpus, &idx, &last_search, &cfg,
                                             has_bench ? &last_bench : NULL);
                bool ok2 = report_export_csv("report.csv", &corpus, &last_search,
                                             has_bench ? &last_bench : NULL);
                if (ok1 && ok2) {
                    printf("Successfully exported 'report.txt' and 'report.csv'.\n");
                } else {
                    printf("Error: Could not write report files.\n");
                }
                break;
            }
            case 0:
                printf("Cleaning up allocated memory and exiting. Goodbye!\n");
                search_response_free(&last_search);
                index_free(&idx);
                corpus_free(&corpus);
                return 0;
            default:
                printf("Unknown option %d. Please choose 0-10.\n", choice);
                break;
        }
    }

    search_response_free(&last_search);
    index_free(&idx);
    corpus_free(&corpus);
    return 0;
}
