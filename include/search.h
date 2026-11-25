#ifndef CSIR_SEARCH_H
#define CSIR_SEARCH_H

#include "common.h"
#include "corpus.h"
#include "index.h"

typedef enum ScoringMethod {
    SCORE_BASIC_TF = 0, /* Sum of query-term frequencies in document */
    SCORE_TFIDF    = 1, /* Sublinear TF * smooth IDF */
    SCORE_BM25     = 2  /* Okapi BM25 (k1 = 1.5, b = 0.75) */
} ScoringMethod;

typedef struct SearchConfig {
    ScoringMethod scoring;
    size_t        top_k;
    bool          show_snippets;
    bool          remove_stopwords;
    double        bm25_k1;
    double        bm25_b;
} SearchConfig;

typedef struct SearchResult {
    uint32_t doc_id;
    double   score;
    uint32_t matched_term_count;
    char     matched_terms[256];
    char     snippet[320];
} SearchResult;

typedef struct SearchResponse {
    SearchResult *results;
    size_t        total_matches;    /* total matching docs before top-K truncation */
    size_t        returned_count;   /* min(total_matches, top_k) */
    double        query_time_sec;
    char          original_query[MAX_QUERY_LEN];
    char          normalized_summary[MAX_QUERY_LEN];
    bool          used_phrase;
    bool          used_boolean;
    bool          used_wildcard;
} SearchResponse;

void search_config_default(SearchConfig *cfg);
const char *search_scoring_name(ScoringMethod method);

void search_response_free(SearchResponse *resp);

/* Execute a query against the hash-table inverted index.
 * Supports:
 *   - Single & multi-term queries:   renewable energy
 *   - Quoted phrase queries:         "renewable energy"
 *   - Boolean operators:             solar AND energy, wind OR solar, energy NOT fossil
 *   - Prefix wildcards:              comput*
 *   - Combinations of the above!
 */
bool search_execute(const Corpus *corpus, const InvertedIndex *idx,
                    const char *query, const SearchConfig *cfg,
                    SearchResponse *out_resp);

/* Baseline linear-scan search (reads every document text directly without index)
 * Used for Section 8.1 Experiment A benchmarking. */
bool search_linear_scan(const Corpus *corpus, const char *query,
                        const SearchConfig *cfg, SearchResponse *out_resp);

/* Search using Linked-List Index (Experiment B) */
bool search_linked_list(const Corpus *corpus, const LinkedListIndex *ll,
                        const char *query, const SearchConfig *cfg,
                        SearchResponse *out_resp);

/* Search using Sorted-Array Binary Search Index (Experiment D) */
bool search_sorted_array(const Corpus *corpus, const SortedArrayIndex *sa,
                         const char *query, const SearchConfig *cfg,
                         SearchResponse *out_resp);

/* Extract a highlighted snippet around the first occurrence of any query term */
void search_extract_snippet(const Document *doc, const char *terms[],
                            size_t term_count, char *out_buf, size_t buf_size);

#endif /* CSIR_SEARCH_H */
