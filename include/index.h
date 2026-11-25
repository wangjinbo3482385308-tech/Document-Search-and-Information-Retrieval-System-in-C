#ifndef CSIR_INDEX_H
#define CSIR_INDEX_H

#include "common.h"
#include "corpus.h"

/* Posting list node: linked list mapping a term to a document with term frequency
 * and positional offsets (to support exact phrase queries). */
typedef struct PostingNode {
    uint32_t            doc_id;
    uint32_t            term_freq;
    uint32_t           *positions;     /* sorted ascending token positions in document */
    uint32_t            pos_capacity;
    struct PostingNode *next;
} PostingNode;

/* Vocabulary entry in the hash table (separate chaining) */
typedef struct TermEntry {
    char             *term;
    uint32_t          doc_freq;        /* number of documents containing this term */
    uint64_t          total_freq;      /* sum of term_freq across corpus */
    PostingNode      *postings_head;
    PostingNode      *postings_tail;
    struct TermEntry *next;            /* next entry in hash bucket chain */
} TermEntry;

/* Primary Hash-Table Inverted Index */
typedef struct InvertedIndex {
    TermEntry **buckets;
    size_t      bucket_count;
    size_t      unique_terms;
    size_t      total_postings;        /* total (term, doc) posting nodes */
    uint64_t    total_positions;       /* total stored token positions */
    bool        stopwords_removed;
    double      build_time_sec;
} InvertedIndex;

/* Alternative 1: Unsorted Linked-List Vocabulary Index (for Experiment B vs C comparison) */
typedef struct ListTermNode {
    char                *term;
    uint32_t             doc_freq;
    PostingNode         *postings_head;
    PostingNode         *postings_tail;
    struct ListTermNode *next;
} ListTermNode;

typedef struct LinkedListIndex {
    ListTermNode *head;
    size_t        unique_terms;
    size_t        total_postings;
    double        build_time_sec;
} LinkedListIndex;

/* Alternative 2: Sorted Array + Binary Search Index (for Advanced comparison) */
typedef struct SortedArrayIndex {
    TermEntry **entries;   /* pointers to TermEntry sorted lexicographically by term */
    size_t      count;
    double      build_time_sec;
} SortedArrayIndex;

/* Hash-Table Inverted Index lifecycle */
void index_init(InvertedIndex *idx, size_t initial_buckets);
void index_free(InvertedIndex *idx);

/* Build inverted index from an already-loaded Corpus.
 * Updates document token stats and corpus avg_doc_len. Returns true on success. */
bool index_build(InvertedIndex *idx, Corpus *corpus, bool remove_stopwords);

/* Add a single term occurrence (used by builder and binary loader) */
TermEntry *index_add_occurrence(InvertedIndex *idx, const char *term,
                                uint32_t doc_id, uint32_t position);

/* Insert a pre-constructed posting (used by persistence loader) */
TermEntry *index_insert_posting(InvertedIndex *idx, const char *term,
                                uint32_t doc_id, uint32_t term_freq,
                                const uint32_t *positions);

/* O(1) average lookup by exact normalized term */
const TermEntry *index_lookup(const InvertedIndex *idx, const char *term);

/* Prefix lookup for wildcard queries (e.g., "comput*").
 * Populates out_matches with pointers to matching TermEntry items.
 * Caller must free(*out_matches). Returns match count. */
size_t index_lookup_prefix(const InvertedIndex *idx, const char *prefix,
                           const TermEntry ***out_matches);

/* Estimate heap memory used by the inverted index (in bytes) */
size_t index_memory_usage(const InvertedIndex *idx);

/* Collect top-N most frequent terms in the corpus (caller must free returned array) */
const TermEntry **index_top_terms(const InvertedIndex *idx, size_t top_n, size_t *out_count);

/* Linked-list vocabulary index (built from primary index or corpus for benchmarking) */
void ll_index_init(LinkedListIndex *ll);
void ll_index_free(LinkedListIndex *ll);
bool ll_index_build(LinkedListIndex *ll, const Corpus *corpus, bool remove_stopwords);
const ListTermNode *ll_index_lookup(const LinkedListIndex *ll, const char *term);
size_t ll_index_memory_usage(const LinkedListIndex *ll);

/* Sorted-array binary-search index (built as a view over InvertedIndex) */
void sa_index_init(SortedArrayIndex *sa);
void sa_index_free(SortedArrayIndex *sa);
bool sa_index_build_from_hash(SortedArrayIndex *sa, const InvertedIndex *idx);
const TermEntry *sa_index_lookup(const SortedArrayIndex *sa, const char *term);
size_t sa_index_memory_usage(const SortedArrayIndex *sa);

#endif /* CSIR_INDEX_H */
