#include "history_cache.h"

#include <stdlib.h>
#include <string.h>

static uint64_t fnv1a_key_hash(const char *str) {
    uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static size_t next_pow2_min(size_t n, size_t min_val) {
    size_t p = min_val;
    while (p < n) p <<= 1;
    return p;
}

static void build_cache_key(char *dst, size_t dst_size,
                            const char *query, const SearchConfig *cfg,
                            bool index_stopwords_removed) {
    snprintf(dst, dst_size, "%d|%lu|%d|%d|%.2f|%.2f|%s",
             (int)cfg->scoring,
             (unsigned long)cfg->top_k,
             cfg->show_snippets ? 1 : 0,
             index_stopwords_removed ? 1 : 0,
             cfg->bm25_k1,
             cfg->bm25_b,
             query ? query : "");
}

bool search_response_clone(const SearchResponse *src, SearchResponse *dst) {
    if (!src || !dst) return false;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->results = NULL;

    if (src->returned_count > 0 && src->results) {
        dst->results = (SearchResult *)malloc(src->returned_count * sizeof(SearchResult));
        if (!dst->results) {
            memset(dst, 0, sizeof(*dst));
            return false;
        }
        memcpy(dst->results, src->results, src->returned_count * sizeof(SearchResult));
    }
    return true;
}

bool qcache_init(QueryCache *cache, size_t capacity, size_t bucket_count) {
    if (!cache) return false;
    memset(cache, 0, sizeof(*cache));
    cache->capacity = capacity == 0 ? 32 : capacity;
    cache->bucket_count = next_pow2_min(bucket_count == 0 ? cache->capacity * 2 : bucket_count, 16);
    cache->buckets = (CacheNode **)calloc(cache->bucket_count, sizeof(CacheNode *));
    if (!cache->buckets) {
        memset(cache, 0, sizeof(*cache));
        return false;
    }
    cache->next_seq_id = 1;
    return true;
}

static void free_node(CacheNode *node) {
    if (!node) return;
    search_response_free(&node->response);
    free(node);
}

void qcache_clear(QueryCache *cache) {
    if (!cache) return;
    CacheNode *cur = cache->head;
    while (cur) {
        CacheNode *next = cur->next;
        free_node(cur);
        cur = next;
    }
    if (cache->buckets) {
        memset(cache->buckets, 0, cache->bucket_count * sizeof(CacheNode *));
    }
    cache->head = NULL;
    cache->tail = NULL;
    cache->cursor = NULL;
    cache->size = 0;
}

void qcache_free(QueryCache *cache) {
    if (!cache) return;
    qcache_clear(cache);
    free(cache->buckets);
    memset(cache, 0, sizeof(*cache));
}

/* ==================== Doubly Linked List Primitives (O(1)) ==================== */

static void dll_unlink(QueryCache *cache, CacheNode *node) {
    if (cache->cursor == node) {
        cache->cursor = node->next ? node->next : node->prev;
    }
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        cache->head = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    } else {
        cache->tail = node->prev;
    }
    node->prev = NULL;
    node->next = NULL;
}

static void dll_push_front(QueryCache *cache, CacheNode *node) {
    node->prev = NULL;
    node->next = cache->head;
    if (cache->head) {
        cache->head->prev = node;
    } else {
        cache->tail = node;
    }
    cache->head = node;
}

static void dll_move_to_front(QueryCache *cache, CacheNode *node) {
    if (cache->head == node) return;
    dll_unlink(cache, node);
    dll_push_front(cache, node);
}

/* ==================== Hash Table Primitives (O(1) avg) ==================== */

static CacheNode *hash_find(const QueryCache *cache, const char *key, size_t *out_bucket) {
    size_t b = (size_t)(fnv1a_key_hash(key) & (cache->bucket_count - 1));
    if (out_bucket) *out_bucket = b;
    for (CacheNode *cur = cache->buckets[b]; cur; cur = cur->hash_next) {
        if (strcmp(cur->key, key) == 0) {
            return cur;
        }
    }
    return NULL;
}

static void hash_insert(QueryCache *cache, CacheNode *node, size_t bucket) {
    node->hash_next = cache->buckets[bucket];
    cache->buckets[bucket] = node;
}

static void hash_remove(QueryCache *cache, CacheNode *node) {
    size_t b = (size_t)(fnv1a_key_hash(node->key) & (cache->bucket_count - 1));
    CacheNode **pp = &cache->buckets[b];
    while (*pp) {
        if (*pp == node) {
            *pp = node->hash_next;
            node->hash_next = NULL;
            return;
        }
        pp = &(*pp)->hash_next;
    }
}

/* ==================== Public Cache & History API ==================== */

bool qcache_get(QueryCache *cache, const char *query, const SearchConfig *cfg,
                bool index_stopwords_removed, SearchResponse *out_resp) {
    if (!cache || !cache->buckets || !query || !cfg || !out_resp) return false;

    double t0 = csir_now_sec();
    char key[MAX_CACHE_KEY_LEN];
    build_cache_key(key, sizeof(key), query, cfg, index_stopwords_removed);

    CacheNode *node = hash_find(cache, key, NULL);
    if (!node) {
        cache->total_misses++;
        return false;
    }

    /* Promote to MRU head in O(1) */
    dll_move_to_front(cache, node);
    cache->cursor = cache->head;
    node->hit_count++;
    cache->total_hits++;

    if (!search_response_clone(&node->response, out_resp)) {
        return false;
    }
    out_resp->query_time_sec = csir_now_sec() - t0;
    return true;
}

bool qcache_put(QueryCache *cache, const char *query, const SearchConfig *cfg,
                bool index_stopwords_removed, const SearchResponse *resp) {
    if (!cache || !cache->buckets || !query || !cfg || !resp) return false;

    char key[MAX_CACHE_KEY_LEN];
    build_cache_key(key, sizeof(key), query, cfg, index_stopwords_removed);

    size_t bucket = 0;
    CacheNode *existing = hash_find(cache, key, &bucket);
    if (existing) {
        SearchResponse cloned;
        if (!search_response_clone(resp, &cloned)) return false;
        search_response_free(&existing->response);
        existing->response = cloned;
        existing->seq_id = cache->next_seq_id++;
        dll_move_to_front(cache, existing);
        cache->cursor = cache->head;
        return true;
    }

    /* Evict LRU tail in O(1) if full */
    if (cache->size >= cache->capacity && cache->tail) {
        CacheNode *victim = cache->tail;
        dll_unlink(cache, victim);
        hash_remove(cache, victim);
        free_node(victim);
        cache->size--;
        cache->total_evictions++;
    }

    CacheNode *node = (CacheNode *)calloc(1, sizeof(CacheNode));
    if (!node) return false;
    if (!search_response_clone(resp, &node->response)) {
        free(node);
        return false;
    }

    node->seq_id = cache->next_seq_id++;
    snprintf(node->key, sizeof(node->key), "%s", key);
    snprintf(node->query, sizeof(node->query), "%s", query);
    node->scoring = cfg->scoring;
    node->top_k = cfg->top_k;
    node->hit_count = 0;

    dll_push_front(cache, node);
    hash_insert(cache, node, bucket);
    cache->size++;
    cache->cursor = cache->head;
    return true;
}

bool qcache_remove_by_seq(QueryCache *cache, uint64_t seq_id) {
    if (!cache || !cache->head) return false;
    for (CacheNode *cur = cache->head; cur; cur = cur->next) {
        if (cur->seq_id == seq_id) {
            dll_unlink(cache, cur);
            hash_remove(cache, cur);
            free_node(cur);
            cache->size--;
            return true;
        }
    }
    return false;
}

const CacheNode *qcache_cursor_current(const QueryCache *cache) {
    if (!cache) return NULL;
    return cache->cursor ? cache->cursor : cache->head;
}

const CacheNode *qcache_cursor_older(QueryCache *cache) {
    if (!cache || !cache->head) return NULL;
    if (!cache->cursor) {
        cache->cursor = cache->head;
    } else if (cache->cursor->next) {
        cache->cursor = cache->cursor->next;
    }
    return cache->cursor;
}

const CacheNode *qcache_cursor_newer(QueryCache *cache) {
    if (!cache || !cache->head) return NULL;
    if (!cache->cursor) {
        cache->cursor = cache->head;
    } else if (cache->cursor->prev) {
        cache->cursor = cache->cursor->prev;
    }
    return cache->cursor;
}

void qcache_cursor_reset(QueryCache *cache) {
    if (!cache) return;
    cache->cursor = cache->head;
}

void qcache_print_history(const QueryCache *cache, bool reverse_order) {
    if (!cache) return;
    uint64_t total_req = cache->total_hits + cache->total_misses;
    double hit_rate = total_req > 0 ? (100.0 * (double)cache->total_hits / (double)total_req) : 0.0;

    printf("\n============== LRU QUERY CACHE & BIDIRECTIONAL HISTORY ==============\n");
    printf("Structure : Doubly Linked List + FNV-1a Hash Table (%zu buckets)\n", cache->bucket_count);
    printf("Occupancy : %zu / %zu entries | Hits: %llu | Misses: %llu | Hit Rate: %.1f%% | Evictions: %llu\n",
           cache->size, cache->capacity,
           (unsigned long long)cache->total_hits,
           (unsigned long long)cache->total_misses,
           hit_rate,
           (unsigned long long)cache->total_evictions);
    printf("Order     : %s\n", reverse_order ? "Oldest -> Newest (LRU to MRU via ->prev)"
                                           : "Newest -> Oldest (MRU to LRU via ->next)");

    if (cache->size == 0) {
        printf("\n  (History is empty)\n");
        printf("=====================================================================\n");
        return;
    }

    printf("\n| %3s | %4s | %-28s | %-8s | %5s | %7s | %4s |\n",
           "Cur", "Seq", "Query", "Scoring", "TopK", "Matches", "Hits");
    printf("|-----|------|------------------------------|----------|-------|---------|------|\n");

    const CacheNode *cur = reverse_order ? cache->tail : cache->head;
    while (cur) {
        const char *sc = cur->scoring == SCORE_BM25 ? "BM25"
                       : (cur->scoring == SCORE_TFIDF ? "TF-IDF" : "BasicTF");
        const char *mark = (cur == cache->cursor) ? ">>>" : "   ";
        printf("| %3s | %4llu | %-28.28s | %-8s | %5zu | %7zu | %4llu |\n",
               mark,
               (unsigned long long)cur->seq_id,
               cur->query,
               sc,
               cur->top_k,
               cur->response.total_matches,
               (unsigned long long)cur->hit_count);
        cur = reverse_order ? cur->prev : cur->next;
    }
    printf("=====================================================================\n");
}

bool qcache_verify_invariants(const QueryCache *cache) {
    if (!cache || !cache->buckets) return false;
    if (cache->size == 0) {
        return cache->head == NULL && cache->tail == NULL;
    }
    if (!cache->head || !cache->tail) return false;
    if (cache->head->prev != NULL || cache->tail->next != NULL) return false;

    /* Forward walk via ->next */
    size_t fwd = 0;
    const CacheNode *prev = NULL;
    for (const CacheNode *cur = cache->head; cur; cur = cur->next) {
        if (cur->prev != prev) return false;
        prev = cur;
        fwd++;
        if (fwd > cache->capacity + 1) return false;
    }
    if (prev != cache->tail || fwd != cache->size) return false;

    /* Backward walk via ->prev */
    size_t bwd = 0;
    const CacheNode *next = NULL;
    for (const CacheNode *cur = cache->tail; cur; cur = cur->prev) {
        if (cur->next != next) return false;
        next = cur;
        bwd++;
        if (bwd > cache->capacity + 1) return false;
    }
    if (next != cache->head || bwd != cache->size) return false;

    /* Hash table node count matches doubly linked list size */
    size_t hcount = 0;
    for (size_t b = 0; b < cache->bucket_count; b++) {
        for (const CacheNode *cur = cache->buckets[b]; cur; cur = cur->hash_next) {
            hcount++;
            if (hcount > cache->capacity + 1) return false;
        }
    }
    return hcount == cache->size;
}
