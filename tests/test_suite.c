#include "benchmark.h"
#include "common.h"
#include "corpus.h"
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

static void test_core_search_and_edge_cases(void) {
    printf("[Test 2] Core Indexing, Ranking, Phrase, Boolean, Wildcard & Edge Cases...\n");

    Corpus corpus;
    InvertedIndex idx;
    corpus_init(&corpus);
    index_init(&idx, 64);

    /* Create temporary test documents matching the coursework PDF example */
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

    /* 2a. Multi-term query: doc02 has 'renewable'(1) + 'energy'(3) = 4; doc01 has 1 + 1 = 2 */
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

    /* 2b. Punctuation & repeated term normalization */
    search_execute(&corpus, &idx, "RENEWABLE-energy!! energy", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2 && fabs(resp.results[0].score - 4.0) < 1e-9,
                "punctuation and repeated terms deduplicated identically");
    search_response_free(&resp);

    /* 2c. No match query */
    search_execute(&corpus, &idx, "quantumbanana", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 0 && resp.returned_count == 0,
                "unknown term returns 0 results");
    search_response_free(&resp);

    /* 2d. Empty / whitespace / punctuation-only queries */
    search_execute(&corpus, &idx, "   --- !!!   ", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 0, "punctuation-only query handled safely");
    search_response_free(&resp);

    /* 2e. Exact phrase query: "solar energy" appears ONLY in doc02, not doc01 ("Solar power") */
    search_execute(&corpus, &idx, "\"solar energy\"", &cfg, &resp);
    ASSERT_TRUE(resp.used_phrase && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "exact phrase \"solar energy\" matches only doc02");
    search_response_free(&resp);

    /* 2f. Boolean AND / NOT queries */
    search_execute(&corpus, &idx, "solar AND wind", &cfg, &resp);
    ASSERT_TRUE(resp.used_boolean && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "Boolean AND narrows to doc02");
    search_response_free(&resp);

    search_execute(&corpus, &idx, "energy NOT fossil", &cfg, &resp);
    ASSERT_TRUE(resp.used_boolean && resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "Boolean NOT excludes doc01");
    search_response_free(&resp);

    /* 2g. Wildcard prefix query: comput* matches "computation" (doc02) and "computers" (doc03) */
    search_execute(&corpus, &idx, "comput*", &cfg, &resp);
    ASSERT_TRUE(resp.used_wildcard && resp.total_matches == 2,
                "Wildcard comput* matches doc02 and doc03");
    search_response_free(&resp);

    /* 2h. Deterministic tie-breaking: doc01 & doc03 both contain 'reduce' once -> doc01 (ID 0) first */
    search_execute(&corpus, &idx, "reduce", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 2 && resp.results[0].doc_id == 0 && resp.results[1].doc_id == 2,
                "tie-breaking orders by doc_id ascending");
    search_response_free(&resp);

    /* 2i. Binary Persistence Save + Reload equivalence */
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

    /* Phrase search still works on reloaded index (positions preserved!) */
    search_execute(&reloaded_c, &reloaded_i, "\"solar energy\"", &cfg, &resp);
    ASSERT_TRUE(resp.total_matches == 1 && resp.results[0].doc_id == 1,
                "positional phrase search works after binary reload");
    search_response_free(&resp);

    /* Corrupted / missing binary file fails safely */
    ASSERT_TRUE(!persistence_load_index("_nonexistent_file.idx", &reloaded_c, &reloaded_i, NULL),
                "missing binary index returns false safely");

    /* 2j. Equivalence across Linear Scan, Linked-List, Hash-Table, and Sorted-Array */
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
    printf("[Test 3] Invalid/Missing Directory Handling...\n");
    Corpus c;
    corpus_init(&c);
    int res = corpus_load_dir(&c, "definitely_does_not_exist_dir_12345");
    ASSERT_TRUE(res < 0, "missing directory returns -1 without crashing");
    corpus_free(&c);
}

static void test_sample_corpus_if_present(void) {
    printf("[Test 4] Sample Corpus (>=50 docs, >=100,000 words) & IR Eval Check...\n");
    Corpus c;
    InvertedIndex idx;
    corpus_init(&c);
    index_init(&idx, 4096);

    int loaded = corpus_load_dir(&c, "data/sample_corpus");
    if (loaded > 0) {
        ASSERT_TRUE(loaded >= 50, "sample corpus has at least 50 documents");
        ASSERT_TRUE(index_build(&idx, &c, true), "sample corpus indexed cleanly");
        ASSERT_TRUE(c.total_raw_tokens >= 100000, "sample corpus has at least 100,000 raw words");

        SearchConfig cfg;
        search_config_default(&cfg);
        SearchResponse resp;
        search_execute(&c, &idx, "\"renewable energy\"", &cfg, &resp);
        ASSERT_TRUE(resp.total_matches > 0, "phrase search finds results in sample corpus");
        search_response_free(&resp);
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
    test_core_search_and_edge_cases();
    test_missing_directory();
    test_sample_corpus_if_present();

    printf("---------------------------------------------------------\n");
    printf("Tests Passed: %d | Tests Failed: %d\n", tests_passed, tests_failed);
    printf("=========================================================\n");
    return tests_failed == 0 ? 0 : 1;
}
