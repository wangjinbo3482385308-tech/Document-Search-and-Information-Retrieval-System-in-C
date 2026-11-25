#ifndef CSIR_REPORT_H
#define CSIR_REPORT_H

#include "common.h"
#include "corpus.h"
#include "index.h"
#include "search.h"
#include "benchmark.h"

/* Print collection and index statistics to console */
void report_print_stats(const Corpus *corpus, const InvertedIndex *idx);

/* Print per-document breakdown (first limit documents, or all if limit == 0) */
void report_print_doc_table(const Corpus *corpus, size_t limit);

/* Print ASCII bar chart of top-N frequent terms in the index */
void report_print_top_terms_chart(const InvertedIndex *idx, size_t top_n);

/* Display formatted search results to console */
void report_print_search_results(const Corpus *corpus, const SearchResponse *resp,
                                 const SearchConfig *cfg);

/* View document contents (full or preview) plus its top terms */
void report_view_document(const Corpus *corpus, const InvertedIndex *idx,
                          uint32_t doc_id, size_t preview_lines);

/* Export comprehensive text report (collection stats + last search + optional benchmark) */
bool report_export_txt(const char *filepath, const Corpus *corpus,
                       const InvertedIndex *idx, const SearchResponse *last_search,
                       const SearchConfig *cfg, const BenchmarkSummary *bench);

/* Export search results and/or benchmark metrics to CSV */
bool report_export_csv(const char *filepath, const Corpus *corpus,
                       const SearchResponse *last_search,
                       const BenchmarkSummary *bench);

#endif /* CSIR_REPORT_H */
