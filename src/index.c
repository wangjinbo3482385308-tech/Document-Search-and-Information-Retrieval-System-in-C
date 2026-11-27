#include "index.h"
#include "tokenizer.h"

#include <stdlib.h>
#include <string.h>

/* 64-bit FNV-1a 哈希函数：分布均匀、计算开销低，适合短字符串词项 */
static uint64_t fnv1a_hash(const char *str) {
    uint64_t hash = 14695981039346656037ULL; /* FNV offset basis */
    for (const unsigned char *p = (const unsigned char *)str; *p; p++) {
        hash ^= (uint64_t)(*p);
        hash *= 1099511628211ULL;            /* FNV prime */
    }
    return hash;
}

static size_t next_power_of_two(size_t n) {
    if (n < 64) return 64;
    size_t p = 64;
    while (p < n) p <<= 1;
    return p;
}

static void free_posting_list(PostingNode *head) {
    PostingNode *cur = head;
    while (cur) {
        PostingNode *next = cur->next;
        free(cur->positions);
        free(cur);
        cur = next;
    }
}

void index_init(InvertedIndex *idx, size_t initial_buckets) {
    if (!idx) return;
    memset(idx, 0, sizeof(*idx));
    idx->bucket_count = next_power_of_two(initial_buckets == 0 ? 4096 : initial_buckets);
    idx->buckets = (TermEntry **)calloc(idx->bucket_count, sizeof(TermEntry *));
}

void index_free(InvertedIndex *idx) {
    if (!idx) return;
    if (idx->buckets) {
        for (size_t i = 0; i < idx->bucket_count; i++) {
            TermEntry *entry = idx->buckets[i];
            while (entry) {
                TermEntry *next = entry->next;
                free(entry->term);
                free_posting_list(entry->postings_head);
                free(entry);
                entry = next;
            }
        }
        free(idx->buckets);
    }
    memset(idx, 0, sizeof(*idx));
}

/* 动态扩容（Rehash）：当负载因子超过 0.75 时将桶数翻倍（保持 2 的幂），
 * 利用位运算 (hash & mask) 替代取模，将全部词项重新散列到新桶中。 */
static bool index_rehash(InvertedIndex *idx, size_t new_bucket_count) {
    TermEntry **new_buckets = (TermEntry **)calloc(new_bucket_count, sizeof(TermEntry *));
    if (!new_buckets) return false;

    size_t mask = new_bucket_count - 1;
    for (size_t i = 0; i < idx->bucket_count; i++) {
        TermEntry *entry = idx->buckets[i];
        while (entry) {
            TermEntry *next = entry->next;
            size_t b = (size_t)(fnv1a_hash(entry->term) & mask);
            entry->next = new_buckets[b]; /* 头插法移入新桶单向链表 */
            new_buckets[b] = entry;
            entry = next;
        }
    }

    free(idx->buckets);
    idx->buckets = new_buckets;
    idx->bucket_count = new_bucket_count;
    return true;
}

static TermEntry *find_or_create_term(InvertedIndex *idx, const char *term) {
    if (!idx->buckets) {
        index_init(idx, 4096);
        if (!idx->buckets) return NULL;
    }

    /* Grow hash table when load factor exceeds 0.75 */
    if ((idx->unique_terms + 1) * 4 > idx->bucket_count * 3) {
        index_rehash(idx, idx->bucket_count * 2);
    }

    size_t mask = idx->bucket_count - 1;
    size_t b = (size_t)(fnv1a_hash(term) & mask);
    for (TermEntry *cur = idx->buckets[b]; cur; cur = cur->next) {
        if (strcmp(cur->term, term) == 0) {
            return cur;
        }
    }

    TermEntry *entry = (TermEntry *)calloc(1, sizeof(TermEntry));
    if (!entry) return NULL;
    entry->term = csir_strdup(term);
    if (!entry->term) {
        free(entry);
        return NULL;
    }
    entry->next = idx->buckets[b];
    idx->buckets[b] = entry;
    idx->unique_terms++;
    return entry;
}

/* 将词位置插入动态数组并保持升序（顺序写入 O(1)，乱序写入做插入移位），
 * 以便短语检索时对 positions[] 直接执行 O(log tf) 二分查找。 */
static void insert_position_sorted(PostingNode *p, uint32_t position) {
    if (p->term_freq == p->pos_capacity) {
        uint32_t new_cap = p->pos_capacity * 2; /* 倍增扩容，均摊 O(1) */
        uint32_t *tmp = (uint32_t *)realloc(p->positions, new_cap * sizeof(uint32_t));
        if (!tmp) return;
        p->positions = tmp;
        p->pos_capacity = new_cap;
    }
    /* 快路径：绝大多数场景下位置单调递增 */
    if (p->term_freq == 0 || position >= p->positions[p->term_freq - 1]) {
        p->positions[p->term_freq] = position;
        return;
    }
    /* 乱序回退：右移较大元素以维持升序不变量 */
    uint32_t i = p->term_freq;
    while (i > 0 && p->positions[i - 1] > position) {
        p->positions[i] = p->positions[i - 1];
        i--;
    }
    p->positions[i] = position;
}

/* 将倒排节点按 doc_id 升序链入单向链表：
 * 正常按序建索引时走 O(1) 尾插快路径；乱序插入时定位有序位置防止破坏升序不变量。 */
static void link_posting_sorted(TermEntry *entry, PostingNode *node) {
    /* Fast path: empty list or monotonically increasing doc_id */
    if (!entry->postings_head) {
        entry->postings_head = node;
        entry->postings_tail = node;
        node->next = NULL;
        return;
    }
    if (node->doc_id > entry->postings_tail->doc_id) {
        entry->postings_tail->next = node;
        entry->postings_tail = node;
        node->next = NULL;
        return;
    }
    /* Out-of-order doc_id: insert at sorted position to preserve ascending doc_id invariant */
    PostingNode *prev = NULL;
    PostingNode *cur = entry->postings_head;
    while (cur && cur->doc_id < node->doc_id) {
        prev = cur;
        cur = cur->next;
    }
    node->next = cur;
    if (prev) {
        prev->next = node;
    } else {
        entry->postings_head = node;
    }
    if (!cur) {
        entry->postings_tail = node;
    }
}

TermEntry *index_add_occurrence(InvertedIndex *idx, const char *term,
                                uint32_t doc_id, uint32_t position) {
    if (!idx || !term || term[0] == '\0') return NULL;
    TermEntry *entry = find_or_create_term(idx, term);
    if (!entry) return NULL;

    /* Fast path: documents are indexed in monotonically increasing doc_id order,
     * so checking postings_tail is O(1). */
    PostingNode *p = entry->postings_tail;
    if (!p || p->doc_id != doc_id) {
        p = NULL;
        if (entry->postings_tail && doc_id < entry->postings_tail->doc_id) {
            for (PostingNode *cur = entry->postings_head; cur && cur->doc_id <= doc_id; cur = cur->next) {
                if (cur->doc_id == doc_id) {
                    p = cur;
                    break;
                }
            }
        }
        if (!p) {
            p = (PostingNode *)calloc(1, sizeof(PostingNode));
            if (!p) return entry;
            p->doc_id = doc_id;
            p->pos_capacity = 2;
            p->positions = (uint32_t *)malloc(p->pos_capacity * sizeof(uint32_t));
            if (!p->positions) {
                free(p);
                return entry;
            }
            link_posting_sorted(entry, p);
            entry->doc_freq++;
            idx->total_postings++;
        }
    }

    uint32_t cap_before = p->pos_capacity;
    insert_position_sorted(p, position);
    if (p->term_freq < p->pos_capacity || p->pos_capacity > cap_before) {
        idx->total_positions++;
    }
    p->term_freq++;
    entry->total_freq++;
    return entry;
}

TermEntry *index_insert_posting(InvertedIndex *idx, const char *term,
                                uint32_t doc_id, uint32_t term_freq,
                                const uint32_t *positions) {
    if (!idx || !term || term_freq == 0) return NULL;
    TermEntry *entry = find_or_create_term(idx, term);
    if (!entry) return NULL;

    PostingNode *p = (PostingNode *)calloc(1, sizeof(PostingNode));
    if (!p) return NULL;
    p->doc_id = doc_id;
    p->term_freq = term_freq;
    p->pos_capacity = term_freq;
    p->positions = (uint32_t *)malloc(term_freq * sizeof(uint32_t));
    if (!p->positions) {
        free(p);
        return NULL;
    }
    if (positions) {
        memcpy(p->positions, positions, term_freq * sizeof(uint32_t));
    }

    link_posting_sorted(entry, p);

    entry->doc_freq++;
    entry->total_freq += term_freq;
    idx->total_postings++;
    idx->total_positions += term_freq;
    return entry;
}

typedef struct BuildCtx {
    InvertedIndex *idx;
    uint32_t       doc_id;
    uint32_t       unique_in_doc;
} BuildCtx;

static void on_build_token(const char *token, uint32_t position, void *user_data) {
    BuildCtx *ctx = (BuildCtx *)user_data;
    size_t before = ctx->idx->total_postings;
    index_add_occurrence(ctx->idx, token, ctx->doc_id, position);
    if (ctx->idx->total_postings > before) {
        ctx->unique_in_doc++;
    }
}

bool index_build(InvertedIndex *idx, Corpus *corpus, bool remove_stopwords) {
    if (!idx || !corpus) return false;

    index_free(idx);
    index_init(idx, 4096);
    if (!idx->buckets) return false;
    idx->stopwords_removed = remove_stopwords;

    double t0 = csir_now_sec();
    corpus->total_raw_tokens = 0;
    corpus->total_indexed_tokens = 0;
    corpus->empty_files = 0;

    for (size_t i = 0; i < corpus->count; i++) {
        Document *doc = &corpus->docs[i];
        doc->raw_tokens = 0;
        doc->total_tokens = 0;
        doc->unique_terms = 0;

        char *text = corpus_read_document_text(doc, NULL);
        if (!text) {
            doc->is_empty = true;
            corpus->empty_files++;
            continue;
        }

        BuildCtx ctx = { idx, doc->doc_id, 0 };
        uint32_t kept = 0;
        uint32_t raw = tokenizer_process_text(text, remove_stopwords,
                                              on_build_token, &ctx, &kept);
        free(text);

        doc->raw_tokens = raw;
        doc->total_tokens = kept;
        doc->unique_terms = ctx.unique_in_doc;
        doc->is_empty = (raw == 0);
        if (doc->is_empty) {
            corpus->empty_files++;
        }

        corpus->total_raw_tokens += raw;
        corpus->total_indexed_tokens += kept;
    }

    corpus->avg_doc_len = corpus->count > 0
        ? (double)corpus->total_indexed_tokens / (double)corpus->count
        : 0.0;

    idx->build_time_sec = csir_now_sec() - t0;
    return true;
}

const TermEntry *index_lookup(const InvertedIndex *idx, const char *term) {
    if (!idx || !idx->buckets || !term || term[0] == '\0') return NULL;
    size_t mask = idx->bucket_count - 1;
    size_t b = (size_t)(fnv1a_hash(term) & mask);
    for (const TermEntry *cur = idx->buckets[b]; cur; cur = cur->next) {
        if (strcmp(cur->term, term) == 0) {
            return cur;
        }
    }
    return NULL;
}

size_t index_lookup_prefix(const InvertedIndex *idx, const char *prefix,
                           const TermEntry ***out_matches) {
    if (out_matches) *out_matches = NULL;
    if (!idx || !idx->buckets || !prefix || prefix[0] == '\0') return 0;

    size_t plen = strlen(prefix);
    size_t cap = 32;
    size_t count = 0;
    const TermEntry **matches = (const TermEntry **)malloc(cap * sizeof(TermEntry *));
    if (!matches) return 0;

    for (size_t i = 0; i < idx->bucket_count; i++) {
        for (const TermEntry *cur = idx->buckets[i]; cur; cur = cur->next) {
            if (strncmp(cur->term, prefix, plen) == 0) {
                if (count == cap) {
                    cap *= 2;
                    const TermEntry **tmp = (const TermEntry **)realloc(matches, cap * sizeof(TermEntry *));
                    if (!tmp) {
                        free(matches);
                        return 0;
                    }
                    matches = tmp;
                }
                matches[count++] = cur;
            }
        }
    }

    if (count == 0) {
        free(matches);
        return 0;
    }
    if (out_matches) {
        *out_matches = matches;
    } else {
        free(matches);
    }
    return count;
}

size_t index_memory_usage(const InvertedIndex *idx) {
    if (!idx) return 0;
    size_t bytes = sizeof(InvertedIndex);
    bytes += idx->bucket_count * sizeof(TermEntry *);
    for (size_t i = 0; i < idx->bucket_count; i++) {
        for (const TermEntry *e = idx->buckets[i]; e; e = e->next) {
            bytes += sizeof(TermEntry) + strlen(e->term) + 1;
            for (const PostingNode *p = e->postings_head; p; p = p->next) {
                bytes += sizeof(PostingNode) + (size_t)p->pos_capacity * sizeof(uint32_t);
            }
        }
    }
    return bytes;
}

static int cmp_term_by_freq_desc(const void *a, const void *b) {
    const TermEntry *ea = *(const TermEntry *const *)a;
    const TermEntry *eb = *(const TermEntry *const *)b;
    if (eb->total_freq > ea->total_freq) return 1;
    if (eb->total_freq < ea->total_freq) return -1;
    return strcmp(ea->term, eb->term);
}

const TermEntry **index_top_terms(const InvertedIndex *idx, size_t top_n, size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!idx || idx->unique_terms == 0 || top_n == 0) return NULL;

    const TermEntry **all = (const TermEntry **)malloc(idx->unique_terms * sizeof(TermEntry *));
    if (!all) return NULL;

    size_t k = 0;
    for (size_t i = 0; i < idx->bucket_count; i++) {
        for (const TermEntry *e = idx->buckets[i]; e; e = e->next) {
            all[k++] = e;
        }
    }

    qsort(all, k, sizeof(TermEntry *), cmp_term_by_freq_desc);
    size_t result_count = k < top_n ? k : top_n;
    if (out_count) *out_count = result_count;
    return all;
}

/* ==================== Linked-List Vocabulary Index (Experiment B) ==================== */

void ll_index_init(LinkedListIndex *ll) {
    if (!ll) return;
    memset(ll, 0, sizeof(*ll));
}

void ll_index_free(LinkedListIndex *ll) {
    if (!ll) return;
    ListTermNode *cur = ll->head;
    while (cur) {
        ListTermNode *next = cur->next;
        free(cur->term);
        free_posting_list(cur->postings_head);
        free(cur);
        cur = next;
    }
    memset(ll, 0, sizeof(*ll));
}

static void ll_add_occurrence(LinkedListIndex *ll, const char *term, uint32_t doc_id) {
    ListTermNode *node = NULL;
    for (ListTermNode *cur = ll->head; cur; cur = cur->next) {
        if (strcmp(cur->term, term) == 0) {
            node = cur;
            break;
        }
    }
    if (!node) {
        node = (ListTermNode *)calloc(1, sizeof(ListTermNode));
        if (!node) return;
        node->term = csir_strdup(term);
        if (!node->term) {
            free(node);
            return;
        }
        node->next = ll->head;
        ll->head = node;
        ll->unique_terms++;
    }

    PostingNode *p = node->postings_tail;
    if (!p || p->doc_id != doc_id) {
        p = (PostingNode *)calloc(1, sizeof(PostingNode));
        if (!p) return;
        p->doc_id = doc_id;
        if (!node->postings_head) {
            node->postings_head = p;
            node->postings_tail = p;
        } else {
            node->postings_tail->next = p;
            node->postings_tail = p;
        }
        node->doc_freq++;
        ll->total_postings++;
    }
    p->term_freq++;
}

typedef struct LLBuildCtx {
    LinkedListIndex *ll;
    uint32_t         doc_id;
} LLBuildCtx;

static void on_ll_token(const char *token, uint32_t position, void *user_data) {
    (void)position;
    LLBuildCtx *ctx = (LLBuildCtx *)user_data;
    ll_add_occurrence(ctx->ll, token, ctx->doc_id);
}

bool ll_index_build(LinkedListIndex *ll, const Corpus *corpus, bool remove_stopwords) {
    if (!ll || !corpus) return false;
    ll_index_free(ll);
    ll_index_init(ll);

    double t0 = csir_now_sec();
    for (size_t i = 0; i < corpus->count; i++) {
        char *text = corpus_read_document_text(&corpus->docs[i], NULL);
        if (!text) continue;
        LLBuildCtx ctx = { ll, corpus->docs[i].doc_id };
        tokenizer_process_text(text, remove_stopwords, on_ll_token, &ctx, NULL);
        free(text);
    }
    ll->build_time_sec = csir_now_sec() - t0;
    return true;
}

const ListTermNode *ll_index_lookup(const LinkedListIndex *ll, const char *term) {
    if (!ll || !term) return NULL;
    for (const ListTermNode *cur = ll->head; cur; cur = cur->next) {
        if (strcmp(cur->term, term) == 0) {
            return cur;
        }
    }
    return NULL;
}

size_t ll_index_memory_usage(const LinkedListIndex *ll) {
    if (!ll) return 0;
    size_t bytes = sizeof(LinkedListIndex);
    for (const ListTermNode *cur = ll->head; cur; cur = cur->next) {
        bytes += sizeof(ListTermNode) + strlen(cur->term) + 1;
        for (const PostingNode *p = cur->postings_head; p; p = p->next) {
            bytes += sizeof(PostingNode);
        }
    }
    return bytes;
}

/* ==================== Sorted-Array Binary-Search Index (Experiment D) ==================== */

void sa_index_init(SortedArrayIndex *sa) {
    if (!sa) return;
    memset(sa, 0, sizeof(*sa));
}

void sa_index_free(SortedArrayIndex *sa) {
    if (!sa) return;
    free(sa->entries);
    memset(sa, 0, sizeof(*sa));
}

static int cmp_term_alpha(const void *a, const void *b) {
    const TermEntry *ea = *(const TermEntry *const *)a;
    const TermEntry *eb = *(const TermEntry *const *)b;
    return strcmp(ea->term, eb->term);
}

bool sa_index_build_from_hash(SortedArrayIndex *sa, const InvertedIndex *idx) {
    if (!sa || !idx) return false;
    sa_index_free(sa);
    sa_index_init(sa);

    double t0 = csir_now_sec();
    if (idx->unique_terms == 0) {
        sa->build_time_sec = 0.0;
        return true;
    }

    sa->entries = (TermEntry **)malloc(idx->unique_terms * sizeof(TermEntry *));
    if (!sa->entries) return false;

    size_t k = 0;
    for (size_t i = 0; i < idx->bucket_count; i++) {
        for (TermEntry *e = idx->buckets[i]; e; e = e->next) {
            sa->entries[k++] = e;
        }
    }
    sa->count = k;
    qsort(sa->entries, sa->count, sizeof(TermEntry *), cmp_term_alpha);
    sa->build_time_sec = idx->build_time_sec + (csir_now_sec() - t0);
    return true;
}

const TermEntry *sa_index_lookup(const SortedArrayIndex *sa, const char *term) {
    if (!sa || !sa->entries || sa->count == 0 || !term) return NULL;
    size_t lo = 0;
    size_t hi = sa->count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        int cmp = strcmp(sa->entries[mid]->term, term);
        if (cmp == 0) return sa->entries[mid];
        if (cmp < 0) lo = mid + 1;
        else hi = mid;
    }
    return NULL;
}

size_t sa_index_memory_usage(const SortedArrayIndex *sa) {
    if (!sa) return 0;
    size_t bytes = sizeof(SortedArrayIndex) + sa->count * sizeof(TermEntry *);
    for (size_t i = 0; i < sa->count; i++) {
        const TermEntry *e = sa->entries[i];
        bytes += sizeof(TermEntry) + strlen(e->term) + 1;
        for (const PostingNode *p = e->postings_head; p; p = p->next) {
            bytes += sizeof(PostingNode) + (size_t)p->pos_capacity * sizeof(uint32_t);
        }
    }
    return bytes;
}
