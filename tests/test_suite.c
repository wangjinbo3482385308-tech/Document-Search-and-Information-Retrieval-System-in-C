#include "benchmark.h"
#include "common.h"
#include "corpus.h"
#include "history_cache.h"
#include "index.h"
#include "persistence.h"
#include "report.h"
#include "search.h"
#include "tokenizer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT_TRUE(cond, msg)                                                      \
    do {                                                                            \
        if (cond) {                                                                 \
            tests_passed++;                                                         \
        } else {                                                                    \
            tests_failed++;                                                         \
            fprintf(stderr, "  [FAIL] %s (line %d)\n", (msg), __LINE__);            \
        }                                                                           \
    } while (0)

typedef struct TokCapture {
    char   words[32][MAX_TERM_LEN];
    size_t count;
} TokCapture;

static void capture_tok(const char *token, uint32_t pos, void *ud) {
    (void)pos;
    TokCapture *cap = (TokCapture *)ud;
    if (cap->count < 32) {
        snprintf(cap->words[cap->count++], MAX_TERM_LEN, "%s", token);
    }
}

static void test_tokenizer(void) {
    printf("[Test 1] Tokenizer & Normalization...\n");
    TokCapture cap;
    memset(&cap, 0, sizeof(cap));
    uint32_t kept = 0;
    uint32_t raw = tokenizer_process_text(
        "Renewable-Energy!!  CAN reduce dependence on FOSSIL_fuels... Solar-Power?",
        true, capture_tok, &cap, &kept);

    ASSERT_TRUE(raw == 10, "raw token count should be 10");
    ASSERT_TRUE(kept == 8, "kept tokens after stopword removal ('can', 'on') should be 8");
    ASSERT_TRUE(strcmp(cap.words[0], "renewable") == 0, "first token normalized to 'renewable'");
    ASSERT_TRUE(strcmp(cap.words[1], "energy") == 0, "hyphen split 'energy'");
    ASSERT_TRUE(strcmp(cap.words[2], "reduce") == 0, "stopword 'can' skipped");
    ASSERT_TRUE(strcmp(cap.words[4], "fossil") == 0, "underscore split 'fossil'");
    ASSERT_TRUE(strcmp(cap.words[5], "fuels") == 0, "underscore split 'fuels'");

    ASSERT_TRUE(tokenizer_is_stopword("the"), "'the' is stopword");
    ASSERT_TRUE(!tokenizer_is_stopword("renewable"), "'renewable' is not stopword");

    /* TREC SGML tag skipping */
    memset(&cap, 0, sizeof(cap));
    tokenizer_process_text("<DOC><TITLE>Wind Power</TITLE><TEXT>Turbines</TEXT></DOC>",
                           false, capture_tok, &cap, &kept);
    ASSERT_TRUE(kept == 3 && strcmp(cap.words[0], "wind") == 0 &&
                strcmp(cap.words[2], "turbines") == 0,
                "SGML tags stripped cleanly");
}

static void write_temp_file(const char *path, const char *content) {
    FILE *fp = fopen(path, "wb");
    if (fp) {
        if (content) fwrite(content, 1, strlen(content), fp);
        fclose(fp);
    }
}

static void test_out_of_order_posting_insertion(void) {
    printf("[Test 2] Out-of-Order Posting & Position Insertion Invariant...\n");
    InvertedIndex idx;
    index_init(&idx, 32);

    /* Deliberately insert doc_ids and positions in scrambled order */
    uint32_t scrambled_docs[] = { 9, 2, 7, 0, 5, 2, 9, 1, 5 };
    uint32_t scrambled_pos[]  = { 10, 4, 3, 1, 8, 1, 2, 6, 3 };
    size_t n = sizeof(scrambled_docs) / sizeof(scrambled_docs[0]);
    for (size_t i = 0; i < n; i++) {
        index_add_occurrence(&idx, "quantum", scrambled_docs[i], scrambled_pos[i]);
    }
    /* Also add a second term for phrase test where rarest term is the SECOND word */
    index_add_occurrence(&idx, "fusion", 5, 9);
    index_add_occurrence(&idx, "fusion", 2, 5);

    const TermEntry *e = index_lookup(&idx, "quantum");
    ASSERT_TRUE(e != NULL && e->doc_freq == 6, "6 distinct doc_ids indexed");

    /* Verify strict ascending doc_id order across posting list & ascending positions */
    uint32_t expected_ids[] = { 0, 1, 2, 5, 7, 9 };
    size_t k = 0;
    bool sorted_ok = true;
    bool pos_sorted_ok = true;
    const PostingNode *last = NULL;
    for (const PostingNode *p = e->postings_head; p; p = p->next) {
        if (k >= 6 || p->doc_id != expected_ids[k]) sorted_ok = false;
        for (uint32_t j = 1; j < p->term_freq; j++) {
            if (p->positions[j] < p->positions[j - 1]) pos_sorted_ok = false;
        }
        last = p;
        k++;
    }
    ASSERT_TRUE(sorted_ok && k == 6, "posting list maintains strict ascending doc_id order");
    ASSERT_TRUE(last == e->postings_tail && last->doc_id == 9, "postings_tail points to max doc_id");
    ASSERT_TRUE(pos_sorted_ok, "out-of-order positions kept sorted ascending for binary search");

    /* Verify phrase search ("quantum fusion") works on scrambled index with pivot = second word */
    Corpus c;
    corpus_init(&c);
    for (uint32_t d = 0; d < 10; d++) {
        char name[32];
        snprintf(name, sizeof(name), "d%u.txt", d);
        Document *doc = corpus_add_entry(&c, name, "", name, 100);
        doc->total_tokens = 20;
    }
    c.avg_doc_len = 20.0;

    SearchConfig cfg;
    search_config_default(&cfg);
    cfg.show_snippets = false;
    SearchResponse resp;
    search_execute(&c, &idx, "\"quantum fusion\"", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2, "phrase match finds doc 2 (pos 4,5) and doc 5 (pos 8,9)");
    ASSERT_TRUE(resp.returned_count == 2 &&
                ((resp.results[0].doc_id == 2 && resp.results[1].doc_id == 5) ||
                 (resp.results[0].doc_id == 5 && resp.results[1].doc_id == 2)),
                "exact doc_ids matched after out-of-order insertion");
    search_response_free(&resp);

    corpus_free(&c);
    index_free(&idx);
}

static void test_doubly_linked_lru_cache(void) {
    printf("[Test 3] Doubly Linked List + Hash Table LRU Query Cache...\n");

    /* Intentionally small capacity (3) and tiny bucket table (4) to stress hash collisions & evictions */
    QueryCache cache;
    ASSERT_TRUE(qcache_init(&cache, 3, 4), "qcache_init succeeds");
    ASSERT_TRUE(qcache_verify_invariants(&cache), "empty cache invariants hold");

    SearchConfig cfg;
    search_config_default(&cfg);

    SearchResponse r1 = {0}, r2 = {0}, r3 = {0}, r4 = {0}, out = {0};
    r1.total_matches = 11; snprintf(r1.original_query, sizeof(r1.original_query), "solar");
    r2.total_matches = 22; snprintf(r2.original_query, sizeof(r2.original_query), "wind");
    r3.total_matches = 33; snprintf(r3.original_query, sizeof(r3.original_query), "geothermal");
    r4.total_matches = 44; snprintf(r4.original_query, sizeof(r4.original_query), "hydro");

    /* Insert 3 items -> order MRU..LRU: geothermal(3) -> wind(2) -> solar(1) */
    ASSERT_TRUE(qcache_put(&cache, "solar", &cfg, true, &r1), "put solar");
    ASSERT_TRUE(qcache_put(&cache, "wind", &cfg, true, &r2), "put wind");
    ASSERT_TRUE(qcache_put(&cache, "geothermal", &cfg, true, &r3), "put geothermal");
    ASSERT_TRUE(cache.size == 3 && qcache_verify_invariants(&cache), "size=3 & invariants valid");
    ASSERT_TRUE(strcmp(cache.head->query, "geothermal") == 0 &&
                strcmp(cache.tail->query, "solar") == 0,
                "head is MRU 'geothermal', tail is LRU 'solar'");

    /* Cache HIT on 'solar' promotes it to MRU head -> solar -> geothermal -> wind */
    ASSERT_TRUE(qcache_get(&cache, "solar", &cfg, true, &out), "cache hit on 'solar'");
    ASSERT_TRUE(out.total_matches == 11, "cloned response matches original");
    search_response_free(&out);
    ASSERT_TRUE(strcmp(cache.head->query, "solar") == 0 &&
                strcmp(cache.tail->query, "wind") == 0,
                "'solar' promoted to MRU head, 'wind' becomes LRU tail");
    ASSERT_TRUE(qcache_verify_invariants(&cache), "invariants hold after MRU promotion");

    /* Config isolation: same query 'solar' under Basic TF must MISS */
    SearchConfig tf_cfg = cfg;
    tf_cfg.scoring = SCORE_BASIC_TF;
    ASSERT_TRUE(!qcache_get(&cache, "solar", &tf_cfg, true, &out),
                "different scoring method does not falsely hit BM25 cache entry");

    /* Insert 4th item 'hydro' -> evicts LRU tail 'wind'! Order: hydro -> solar -> geothermal */
    ASSERT_TRUE(qcache_put(&cache, "hydro", &cfg, true, &r4), "put hydro triggers eviction");
    ASSERT_TRUE(cache.size == 3 && cache.total_evictions == 1, "1 LRU eviction occurred");
    ASSERT_TRUE(!qcache_get(&cache, "wind", &cfg, true, &out), "'wind' was evicted");
    ASSERT_TRUE(strcmp(cache.head->query, "hydro") == 0 &&
                strcmp(cache.tail->query, "geothermal") == 0,
                "post-eviction DLL order: hydro -> solar -> geothermal");
    ASSERT_TRUE(qcache_verify_invariants(&cache), "invariants hold after eviction");

    /* Bidirectional cursor navigation: head('hydro') -> older('solar') -> older('geothermal') -> newer('solar') */
    qcache_cursor_reset(&cache);
    ASSERT_TRUE(strcmp(qcache_cursor_current(&cache)->query, "hydro") == 0, "cursor starts at head");
    ASSERT_TRUE(strcmp(qcache_cursor_older(&cache)->query, "solar") == 0, "cursor steps older via ->next");
    ASSERT_TRUE(strcmp(qcache_cursor_older(&cache)->query, "geothermal") == 0, "cursor steps to tail");
    ASSERT_TRUE(strcmp(qcache_cursor_older(&cache)->query, "geothermal") == 0, "cursor clamps at tail");
    ASSERT_TRUE(strcmp(qcache_cursor_newer(&cache)->query, "solar") == 0, "cursor steps newer via ->prev");

    /* Remove middle node ('solar'), then head ('hydro'), then tail ('geothermal') */
    uint64_t mid_seq = cache.head->next->seq_id;
    uint64_t head_seq = cache.head->seq_id;
    uint64_t tail_seq = cache.tail->seq_id;
    ASSERT_TRUE(qcache_remove_by_seq(&cache, mid_seq) && cache.size == 2 &&
                qcache_verify_invariants(&cache), "remove middle DLL node preserves links");
    ASSERT_TRUE(qcache_remove_by_seq(&cache, head_seq) && cache.size == 1 &&
                qcache_verify_invariants(&cache), "remove head DLL node preserves links");
    ASSERT_TRUE(qcache_remove_by_seq(&cache, tail_seq) && cache.size == 0 &&
                qcache_verify_invariants(&cache), "remove last DLL node leaves clean empty list");

    qcache_free(&cache);
}

static void test_core_search_and_edge_cases(void) {
    printf("[Test 4] Core Indexing, Ranking, Min-Heap Top-K, Phrase, Boolean, Wildcard & CSV...\n");

    Corpus corpus;
    InvertedIndex idx;
    corpus_init(&corpus);
    index_init(&idx, 64);

    write_temp_file("_t_doc01.txt",
        "Renewable energy can reduce dependence on fossil fuels. Solar power is widely used.");
    write_temp_file("_t_doc02.txt",
        "Wind energy and solar energy are important renewable energy sources. Computation helps grid balance.");
    write_temp_file("_t_doc03.txt",
        "Public transport can reduce traffic congestion in cities. Computers control traffic lights.");
    write_temp_file("_t_doc04.txt", ""); /* empty doc edge case */

    corpus_add_entry(&corpus, "doc01.txt", "_t_doc01.txt", "Doc 01", 81);
    corpus_add_entry(&corpus, "doc02.txt", "_t_doc02.txt", "Doc 02", 99);
    corpus_add_entry(&corpus, "doc03.txt", "_t_doc03.txt", "Doc 03", 90);
    corpus_add_entry(&corpus, "doc04.txt", "_t_doc04.txt", "Empty Doc", 0);

    ASSERT_TRUE(index_build(&idx, &corpus, true), "index_build succeeds");
    ASSERT_TRUE(corpus.empty_files == 1, "empty file counted");
    ASSERT_TRUE(idx.unique_terms > 15, "vocabulary populated");

    SearchConfig cfg;
    search_config_default(&cfg);
    cfg.scoring = SCORE_BASIC_TF;
    cfg.show_snippets = true;

    /* Multi-term query: doc02 has 'renewable'(1) + 'energy'(3) = 4; doc01 has 1 + 1 = 2 */
    SearchResponse resp;
    search_execute(&corpus, &idx, "renewable energy", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2, "renewable energy matches 2 docs");
    ASSERT_TRUE(resp.results[0].doc_id == 1 && fabs(resp.results[0].score - 4.0) < 1e-9,
                "doc02 ranks #1 with Basic TF score 4.0");
    ASSERT_TRUE(resp.results[1].doc_id == 0 && fabs(resp.results[1].score - 2.0) < 1e-9,
                "doc01 ranks #2 with Basic TF score 2.0");
    ASSERT_TRUE(strstr(resp.results[0].snippet, "[energy]") != NULL ||
                strstr(resp.results[0].snippet, "[renewable]") != NULL,
                "snippet highlights matched term");
    search_response_free(&resp);

    /* Min-heap Top-K (top_k = 1 < total_matches = 2) returns exact #1 winner */
    cfg.top_k = 1;
    search_execute(&corpus, &idx, "renewable energy", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2 && resp.returned_count == 1 &&
                resp.results[0].doc_id == 1, "Min-Heap Top-1 matches exact full-sort winner");
    search_response_free(&resp);
    cfg.top_k = 10;

    /* Punctuation & repeated term normalization */
    search_execute(&corpus, &idx, "RENEWABLE-energy!! energy", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2 && fabs(resp.results[0].score - 4.0) < 1e-9,
                "punctuation and repeated terms deduplicated identically");
    search_response_free(&resp);

    /* No match query */
    search_execute(&corpus, &idx, "quantumbanana", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 0 && resp.returned_count == 0,
                "unknown term returns 0 results");
    search_response_free(&resp);

    /* Empty / whitespace / punctuation-only queries */
    search_execute(&corpus, &idx, "   --- !!!   ", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 0, "punctuation-only query handled safely");
    search_response_free(&resp);

    /* Exact phrase query + CSV RFC4180 quote escaping verification */
    search_execute(&corpus, &idx, "\"solar energy\"", &cfg, &resp);
    ASSERT_TRUE(resp.used_phrase && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "exact phrase \"solar energy\" matches only doc02");
    ASSERT_TRUE(report_export_csv("_t_csv.csv", &corpus, &resp, NULL), "CSV export succeeds");
    {
        FILE *cf = fopen("_t_csv.csv", "r");
        char buf[512] = {0};
        if (cf) {
            size_t nr = fread(buf, 1, sizeof(buf) - 1, cf);
            buf[nr] = '\0';
            fclose(cf);
        }
        ASSERT_TRUE(strstr(buf, "\"\"solar energy\"\"") != NULL,
                    "CSV properly escapes embedded double quotes per RFC 4180");
        remove("_t_csv.csv");
    }
    search_response_free(&resp);

    /* Boolean AND / NOT queries */
    search_execute(&corpus, &idx, "solar AND wind", &cfg, &resp);
    ASSERT_TRUE(resp.used_boolean && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "Boolean AND narrows to doc02");
    search_response_free(&resp);

    search_execute(&corpus, &idx, "energy NOT fossil", &cfg, &resp);
    ASSERT_TRUE(resp.used_boolean && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "Boolean NOT excludes doc01");
    search_response_free(&resp);

    /* Wildcard prefix query: comput* matches "computation" (doc02) and "computers" (doc03) */
    search_execute(&corpus, &idx, "comput*", &cfg, &resp);
    ASSERT_TRUE(resp.used_wildcard && resp.total_matches == 2,
                "Wildcard comput* matches doc02 and doc03");
    ASSERT_TRUE(strstr(resp.results[0].matched_terms, "comput") != NULL,
                "Wildcard populates matched_terms on Top-K results");
    search_response_free(&resp);

    /* Deterministic tie-breaking: doc01 & doc03 both contain 'reduce' once -> doc01 (ID 0) first */
    cfg.top_k = 1;
    search_execute(&corpus, &idx, "reduce", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2 && resp.returned_count == 1 && resp.results[0].doc_id == 0,
                "Min-Heap tie-breaking selects lower doc_id (0 over 2)");
    search_response_free(&resp);
    cfg.top_k = 10;

    /* Binary Persistence Save + Reload equivalence */
    double save_s = 0.0, load_s = 0.0;
    ASSERT_TRUE(persistence_save_index("_t_test.idx", &corpus, &idx, &save_s),
                "binary index save succeeds");

    Corpus reloaded_c;
    InvertedIndex reloaded_i;
    corpus_init(&reloaded_c);
    index_init(&reloaded_i, 16);
    ASSERT_TRUE(persistence_load_index("_t_test.idx", &reloaded_c, &reloaded_i, &load_s),
                "binary index load succeeds");
    ASSERT_TRUE(reloaded_c.count == corpus.count && reloaded_i.unique_terms == idx.unique_terms &&
                reloaded_i.total_postings == idx.total_postings,
                "reloaded index metadata matches original");

    search_execute(&reloaded_c, &reloaded_i, "\"solar energy\"", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "positional phrase search works after binary reload");
    search_response_free(&resp);

    ASSERT_TRUE(!persistence_load_index("_nonexistent_file.idx", &reloaded_c, &reloaded_i, NULL),
                "missing binary index returns false safely");

    /* Equivalence across Linear Scan, Linked-List, Hash-Table, and Sorted-Array */
    cfg.scoring = SCORE_BM25;
    cfg.show_snippets = false;

    LinkedListIndex ll;
    ll_index_init(&ll);
    ll_index_build(&ll, &corpus, true);

    SortedArrayIndex sa;
    sa_index_init(&sa);
    sa_index_build_from_hash(&sa, &idx);

    SearchResponse r_lin, r_ll, r_hash, r_sa;
    search_linear_scan(&corpus, "renewable energy", &cfg, &r_lin);
    search_linked_list(&corpus, &ll, "renewable energy", &cfg, &r_ll);
    search_execute(&corpus, &idx, "renewable energy", &cfg, &r_hash);
    search_sorted_array(&corpus, &sa, "renewable energy", &cfg, &r_sa);

    ASSERT_TRUE(r_lin.total_matches == r_hash.total_matches &&
                r_ll.total_matches == r_hash.total_matches &&
                r_sa.total_matches == r_hash.total_matches,
                "all 4 search structures return identical match counts");
    ASSERT_TRUE(r_lin.results[0].doc_id == r_hash.results[0].doc_id &&
                fabs(r_lin.results[0].score - r_hash.results[0].score) < 1e-9 &&
                fabs(r_ll.results[0].score - r_hash.results[0].score) < 1e-9 &&
                fabs(r_sa.results[0].score - r_hash.results[0].score) < 1e-9,
                "all 4 search structures produce identical BM25 scores and ranking");

    search_response_free(&r_lin);
    search_response_free(&r_ll);
    search_response_free(&r_hash);
    search_response_free(&r_sa);
    ll_index_free(&ll);
    sa_index_free(&sa);

    corpus_free(&reloaded_c);
    index_free(&reloaded_i);
    corpus_free(&corpus);
    index_free(&idx);

    remove("_t_doc01.txt");
    remove("_t_doc02.txt");
    remove("_t_doc03.txt");
    remove("_t_doc04.txt");
    remove("_t_test.idx");
}

static void test_missing_directory(void) {
    printf("[Test 5] Invalid/Missing Directory Handling...\n");
    Corpus c;
    corpus_init(&c);
    int res = corpus_load_dir(&c, "definitely_does_not_exist_dir_12345");
    ASSERT_TRUE(res < 0, "missing directory returns -1 without crashing");
    corpus_free(&c);
}

static void test_sample_corpus_if_present(void) {
    printf("[Test 6] Sample Corpus (>=50 docs, >=100,000 words) & Top-K Heap Parity...\n");
    Corpus c;
    InvertedIndex idx;
    corpus_init(&c);
    index_init(&idx, 4096);

    int loaded = corpus_load_dir(&c, "data/sample_corpus");
    if (loaded > 0) {
        ASSERT_TRUE(loaded >= 50, "sample corpus has at least 50 documents");
        ASSERT_TRUE(index_build(&idx, &c, true), "sample corpus indexed cleanly");
        ASSERT_TRUE(c.total_raw_tokens >= 100000, "sample corpus has at least 100,000 raw words");

        SearchConfig full_cfg, heap_cfg;
        search_config_default(&full_cfg);
        full_cfg.show_snippets = false;
        full_cfg.top_k = 0; /* return all -> full qsort */
        heap_cfg = full_cfg;
        heap_cfg.top_k = 5; /* partial Top-5 -> binary min-heap */

        SearchResponse r_full, r_heap;
        search_execute(&c, &idx, "energy solar algorithm network memory", &full_cfg, &r_full);
        search_execute(&c, &idx, "energy solar algorithm network memory", &heap_cfg, &r_heap);

        bool parity = (r_full.total_matches == r_heap.total_matches && r_heap.returned_count == 5);
        for (size_t i = 0; parity && i < 5; i++) {
            if (r_full.results[i].doc_id != r_heap.results[i].doc_id ||
                fabs(r_full.results[i].score - r_heap.results[i].score) > 1e-12) {
                parity = false;
            }
        }
        ASSERT_TRUE(parity, "Min-Heap Top-5 matches full qsort top 5 identically on 109k-word corpus");
        search_response_free(&r_full);
        search_response_free(&r_heap);

        SearchResponse phrase_resp;
        search_execute(&c, &idx, "\"renewable energy\"", &heap_cfg, &phrase_resp);
        ASSERT_TRUE(phrase_resp.total_matches > 0, "phrase search finds results in sample corpus");
        search_response_free(&phrase_resp);
    } else {
        printf("  (Skipping sample corpus check: data/sample_corpus not generated yet)\n");
    }

    corpus_free(&c);
    index_free(&idx);
}

int main(void) {
    printf("=========================================================\n");
    printf("  CSIR Automated Unit & Integration Test Suite\n");
    printf("=========================================================\n");

    test_tokenizer();
    test_out_of_order_posting_insertion();
    test_doubly_linked_lru_cache();
    test_core_search_and_edge_cases();
    test_missing_directory();
    test_sample_corpus_if_present();

    printf("---------------------------------------------------------\n");
    printf("Tests Passed: %d | Tests Failed: %d\n", tests_passed, tests_failed);
    printf("=========================================================\n");
    return tests_failed == 0 ? 0 : 1;
}
