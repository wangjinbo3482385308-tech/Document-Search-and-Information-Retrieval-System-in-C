#ifndef CSIR_HISTORY_CACHE_H
#define CSIR_HISTORY_CACHE_H

#include "common.h"
#include "search.h"

#define MAX_CACHE_KEY_LEN 600

/* Doubly linked list node stored simultaneously in:
 *   1) Doubly linked list (head = MRU/newest, tail = LRU/oldest) via prev/next
 *   2) Hash table bucket chain (for O(1) average lookup by key) via hash_next
 */
typedef struct CacheNode {
    uint64_t          seq_id;
    char              key[MAX_CACHE_KEY_LEN];
    char              query[MAX_QUERY_LEN];
    ScoringMethod     scoring;
    size_t            top_k;
    SearchResponse    response;
    uint64_t          hit_count;
    struct CacheNode *prev;      /* closer to MRU head (NULL if head) */
    struct CacheNode *next;      /* closer to LRU tail (NULL if tail) */
    struct CacheNode *hash_next; /* next node in hash table bucket */
} CacheNode;

/* O(1) LRU Query Cache & Bidirectional Search History
 * Backed by a Doubly Linked List + FNV-1a Hash Table. */
typedef struct QueryCache {
    CacheNode **buckets;
    size_t      bucket_count;
    CacheNode  *head;            /* Most Recently Used (newest) */
    CacheNode  *tail;            /* Least Recently Used (oldest) */
    CacheNode  *cursor;          /* Current navigation pointer for history browsing */
    size_t      size;
    size_t      capacity;
    uint64_t    next_seq_id;
    uint64_t    total_hits;
    uint64_t    total_misses;
    uint64_t    total_evictions;
} QueryCache;

/* Lifecycle */
bool qcache_init(QueryCache *cache, size_t capacity, size_t bucket_count);
void qcache_free(QueryCache *cache);
void qcache_clear(QueryCache *cache);

/* Deep copy a SearchResponse (caller must free dst with search_response_free) */
bool search_response_clone(const SearchResponse *src, SearchResponse *dst);

/* O(1) average cache lookup.
 * On hit: promotes node to MRU head, clones response into out_resp, returns true.
 * On miss: increments total_misses, returns false. */
bool qcache_get(QueryCache *cache, const char *query, const SearchConfig *cfg,
                bool index_stopwords_removed, SearchResponse *out_resp);

/* O(1) average cache insert/update.
 * Evicts LRU tail node in O(1) if capacity is exceeded. */
bool qcache_put(QueryCache *cache, const char *query, const SearchConfig *cfg,
                bool index_stopwords_removed, const SearchResponse *resp);

/* O(1) unlink & delete by node pointer or O(n) lookup by seq_id + O(1) doubly-linked unlink */
bool qcache_remove_by_seq(QueryCache *cache, uint64_t seq_id);

/* Bidirectional history navigation using doubly linked list pointers */
const CacheNode *qcache_cursor_current(const QueryCache *cache);
const CacheNode *qcache_cursor_older(QueryCache *cache); /* moves along ->next */
const CacheNode *qcache_cursor_newer(QueryCache *cache); /* moves along ->prev */
void             qcache_cursor_reset(QueryCache *cache); /* resets to head (newest) */

/* Print history in forward (MRU -> LRU via ->next) or reverse (LRU -> MRU via ->prev) order */
void qcache_print_history(const QueryCache *cache, bool reverse_order);

/* Verify internal invariants (doubly linked list links & hash count); useful for tests */
bool qcache_verify_invariants(const QueryCache *cache);

#endif /* CSIR_HISTORY_CACHE_H */
