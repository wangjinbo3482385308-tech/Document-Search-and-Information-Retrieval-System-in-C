#include "search.h"
#include "tokenizer.h"

#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define MAX_QUERY_CLAUSES 32
#define MAX_PHRASE_WORDS  8

typedef enum ClauseKind {
    CLAUSE_TERM     = 0,
    CLAUSE_WILDCARD = 1,
    CLAUSE_PHRASE   = 2
} ClauseKind;

typedef enum ClauseOp {
    OP_SHOULD   = 0,
    OP_MUST     = 1,
    OP_MUST_NOT = 2
} ClauseOp;

typedef struct QueryClause {
    ClauseKind kind;
    ClauseOp   op;
    char       term[MAX_TERM_LEN]; /* for TERM or WILDCARD prefix */
    char       phrase_words[MAX_PHRASE_WORDS][MAX_TERM_LEN];
    uint32_t   phrase_offsets[MAX_PHRASE_WORDS];
    size_t     phrase_len;
} QueryClause;

typedef struct ParsedQuery {
    QueryClause clauses[MAX_QUERY_CLAUSES];
    size_t      count;
    bool        used_phrase;
    bool        used_boolean;
    bool        used_wildcard;
    char        summary[MAX_QUERY_LEN];
} ParsedQuery;

void search_config_default(SearchConfig *cfg) {
    if (!cfg) return;
    cfg->scoring = SCORE_BM25;
    cfg->top_k = 10;
    cfg->show_snippets = true;
    cfg->remove_stopwords = true;
    cfg->bm25_k1 = 1.5;
    cfg->bm25_b = 0.75;
}

const char *search_scoring_name(ScoringMethod method) {
    switch (method) {
        case SCORE_BASIC_TF: return "Basic TF (Term Frequency Sum)";
        case SCORE_TFIDF:    return "TF-IDF (Sublinear TF * Smooth IDF)";
        case SCORE_BM25:     return "Okapi BM25 (k1=1.5, b=0.75)";
        default:             return "Unknown";
    }
}

void search_response_free(SearchResponse *resp) {
    if (!resp) return;
    free(resp->results);
    memset(resp, 0, sizeof(*resp));
}

static double compute_term_score(ScoringMethod method, uint32_t tf, uint32_t df,
                                 size_t num_docs, uint32_t doc_len, double avg_doc_len,
                                 double k1, double b) {
    if (tf == 0 || num_docs == 0) return 0.0;
    switch (method) {
        case SCORE_BASIC_TF:
            return (double)tf;
        case SCORE_TFIDF: {
            double idf = log(((double)num_docs + 1.0) / ((double)df + 1.0)) + 1.0;
            double sub_tf = 1.0 + log((double)tf);
            return sub_tf * idf;
        }
        case SCORE_BM25:
        default: {
            double n = (double)num_docs;
            double d = (double)df;
            double idf = log(((n - d + 0.5) / (d + 0.5)) + 1.0);
            double dl = doc_len > 0 ? (double)doc_len : 1.0;
            double avgdl = avg_doc_len > 0.0 ? avg_doc_len : 1.0;
            double denom = (double)tf + k1 * (1.0 - b + b * (dl / avgdl));
            return idf * (((double)tf * (k1 + 1.0)) / denom);
        }
    }
}

static bool has_duplicate_clause(const ParsedQuery *pq, const QueryClause *c) {
    for (size_t i = 0; i < pq->count; i++) {
        const QueryClause *e = &pq->clauses[i];
        if (e->kind != c->kind || e->op != c->op) continue;
        if (e->kind == CLAUSE_TERM || e->kind == CLAUSE_WILDCARD) {
            if (strcmp(e->term, c->term) == 0) return true;
        } else if (e->kind == CLAUSE_PHRASE && e->phrase_len == c->phrase_len) {
            bool same = true;
            for (size_t j = 0; j < e->phrase_len; j++) {
                if (strcmp(e->phrase_words[j], c->phrase_words[j]) != 0) {
                    same = false;
                    break;
                }
            }
            if (same) return true;
        }
    }
    return false;
}

typedef struct PhraseTokenCtx {
    QueryClause *clause;
    bool         has_first;
    uint32_t     first_pos;
} PhraseTokenCtx;

static void on_phrase_token(const char *token, uint32_t pos, void *user_data) {
    PhraseTokenCtx *ctx = (PhraseTokenCtx *)user_data;
    if (ctx->clause->phrase_len >= MAX_PHRASE_WORDS) return;
    if (!ctx->has_first) {
        ctx->first_pos = pos;
        ctx->has_first = true;
    }
    size_t idx = ctx->clause->phrase_len++;
    snprintf(ctx->clause->phrase_words[idx], MAX_TERM_LEN, "%s", token);
    ctx->clause->phrase_offsets[idx] = pos - ctx->first_pos;
}

typedef struct SplitTermCtx {
    ParsedQuery *pq;
    ClauseOp     op;
    bool         promote_prev_and;
} SplitTermCtx;

static void add_term_clause_cb(const char *token, uint32_t pos, void *user_data) {
    (void)pos;
    SplitTermCtx *ctx = (SplitTermCtx *)user_data;
    if (ctx->pq->count >= MAX_QUERY_CLAUSES) return;

    QueryClause c;
    memset(&c, 0, sizeof(c));
    c.kind = CLAUSE_TERM;
    c.op = ctx->op;
    snprintf(c.term, sizeof(c.term), "%s", token);

    if (!has_duplicate_clause(ctx->pq, &c)) {
        ctx->pq->clauses[ctx->pq->count++] = c;
    }
}

static void parse_query(const char *raw_query, bool remove_stopwords, ParsedQuery *pq) {
    memset(pq, 0, sizeof(*pq));
    if (!raw_query) return;

    const char *p = raw_query;
    ClauseOp next_op = OP_SHOULD;
    bool prev_was_and = false;

    while (*p && pq->count < MAX_QUERY_CLAUSES) {
        while (*p && isspace((unsigned char)*p)) p++;
        if (!*p) break;

        /* Quoted phrase */
        if (*p == '"') {
            p++;
            const char *start = p;
            while (*p && *p != '"') p++;
            size_t len = (size_t)(p - start);
            if (*p == '"') p++;

            char buf[MAX_QUERY_LEN];
            if (len >= sizeof(buf)) len = sizeof(buf) - 1;
            memcpy(buf, start, len);
            buf[len] = '\0';

            QueryClause c;
            memset(&c, 0, sizeof(c));
            c.kind = CLAUSE_PHRASE;
            c.op = next_op;

            PhraseTokenCtx pctx = { &c, false, 0 };
            tokenizer_process_text(buf, remove_stopwords, on_phrase_token, &pctx, NULL);

            if (c.phrase_len > 0) {
                if (prev_was_and && pq->count > 0 && pq->clauses[pq->count - 1].op == OP_SHOULD) {
                    pq->clauses[pq->count - 1].op = OP_MUST;
                }
                if (c.phrase_len == 1) {
                    c.kind = CLAUSE_TERM;
                    snprintf(c.term, sizeof(c.term), "%s", c.phrase_words[0]);
                } else {
                    pq->used_phrase = true;
                }
                if (!has_duplicate_clause(pq, &c)) {
                    pq->clauses[pq->count++] = c;
                }
            }
            next_op = OP_SHOULD;
            prev_was_and = false;
            continue;
        }

        /* Read whitespace-delimited raw token */
        const char *start = p;
        while (*p && !isspace((unsigned char)*p) && *p != '"') p++;
        size_t len = (size_t)(p - start);
        char raw_tok[MAX_TERM_LEN * 2];
        if (len >= sizeof(raw_tok)) len = sizeof(raw_tok) - 1;
        memcpy(raw_tok, start, len);
        raw_tok[len] = '\0';

        /* Boolean operators (exact uppercase keywords) */
        if (strcmp(raw_tok, "AND") == 0) {
            pq->used_boolean = true;
            next_op = OP_MUST;
            prev_was_and = true;
            continue;
        }
        if (strcmp(raw_tok, "OR") == 0) {
            pq->used_boolean = true;
            next_op = OP_SHOULD;
            prev_was_and = false;
            continue;
        }
        if (strcmp(raw_tok, "NOT") == 0) {
            pq->used_boolean = true;
            next_op = OP_MUST_NOT;
            prev_was_and = false;
            continue;
        }

        /* Wildcard prefix token (e.g., comput*) */
        if (len >= 2 && raw_tok[len - 1] == '*') {
            raw_tok[len - 1] = '\0';
            char norm[MAX_TERM_LEN];
            if (tokenizer_normalize_word(raw_tok, norm, sizeof(norm)) > 0) {
                if (prev_was_and && pq->count > 0 && pq->clauses[pq->count - 1].op == OP_SHOULD) {
                    pq->clauses[pq->count - 1].op = OP_MUST;
                }
                QueryClause c;
                memset(&c, 0, sizeof(c));
                c.kind = CLAUSE_WILDCARD;
                c.op = next_op;
                snprintf(c.term, sizeof(c.term), "%s", norm);
                pq->used_wildcard = true;
                if (!has_duplicate_clause(pq, &c)) {
                    pq->clauses[pq->count++] = c;
                }
            }
            next_op = OP_SHOULD;
            prev_was_and = false;
            continue;
        }

        /* Regular word or hyphenated/punctuated token (e.g., renewable-energy) */
        if (prev_was_and && pq->count > 0 && pq->clauses[pq->count - 1].op == OP_SHOULD) {
            pq->clauses[pq->count - 1].op = OP_MUST;
        }
        SplitTermCtx sctx = { pq, next_op, prev_was_and };
        tokenizer_process_text(raw_tok, remove_stopwords, add_term_clause_cb, &sctx, NULL);

        next_op = OP_SHOULD;
        prev_was_and = false;
    }

    /* Build human-readable normalized query summary */
    size_t pos = 0;
    for (size_t i = 0; i < pq->count; i++) {
        const QueryClause *c = &pq->clauses[i];
        const char *prefix = "";
        if (c->op == OP_MUST) prefix = "+";
        else if (c->op == OP_MUST_NOT) prefix = "-";

        if (i > 0 && pos + 2 < sizeof(pq->summary)) {
            pq->summary[pos++] = ' ';
        }
        if (c->kind == CLAUSE_TERM) {
            pos += (size_t)snprintf(pq->summary + pos, sizeof(pq->summary) - pos,
                                    "%s%s", prefix, c->term);
        } else if (c->kind == CLAUSE_WILDCARD) {
            pos += (size_t)snprintf(pq->summary + pos, sizeof(pq->summary) - pos,
                                    "%s%s*", prefix, c->term);
        } else if (c->kind == CLAUSE_PHRASE) {
            pos += (size_t)snprintf(pq->summary + pos, sizeof(pq->summary) - pos,
                                    "%s\"", prefix);
            for (size_t j = 0; j < c->phrase_len; j++) {
                pos += (size_t)snprintf(pq->summary + pos, sizeof(pq->summary) - pos,
                                        "%s%s", j > 0 ? " " : "", c->phrase_words[j]);
            }
            pos += (size_t)snprintf(pq->summary + pos, sizeof(pq->summary) - pos, "\"");
        }
        if (pos >= sizeof(pq->summary)) break;
    }
}

static bool has_position(const uint32_t *positions, uint32_t count, uint32_t target) {
    uint32_t lo = 0, hi = count;
    while (lo < hi) {
        uint32_t mid = lo + (hi - lo) / 2;
        if (positions[mid] == target) return true;
        if (positions[mid] < target) lo = mid + 1;
        else hi = mid;
    }
    return false;
}

static const PostingNode *find_doc_posting(const TermEntry *entry, uint32_t doc_id) {
    if (!entry) return NULL;
    for (const PostingNode *p = entry->postings_head; p; p = p->next) {
        if (p->doc_id == doc_id) return p;
        if (p->doc_id > doc_id) break; /* postings are stored in ascending doc_id order */
    }
    return NULL;
}

/* Count exact positional phrase occurrences in a document */
static uint32_t count_phrase_matches_in_doc(const TermEntry **entries,
                                            const uint32_t *offsets,
                                            size_t phrase_len,
                                            const PostingNode *first_posting) {
    const PostingNode *postings[MAX_PHRASE_WORDS];
    postings[0] = first_posting;
    for (size_t k = 1; k < phrase_len; k++) {
        postings[k] = find_doc_posting(entries[k], first_posting->doc_id);
        if (!postings[k]) return 0;
    }

    uint32_t matches = 0;
    for (uint32_t i = 0; i < first_posting->term_freq; i++) {
        uint32_t base_pos = first_posting->positions[i];
        bool ok = true;
        for (size_t k = 1; k < phrase_len; k++) {
            uint32_t expected = base_pos + offsets[k];
            if (!has_position(postings[k]->positions, postings[k]->term_freq, expected)) {
                ok = false;
                break;
            }
        }
        if (ok) matches++;
    }
    return matches;
}

typedef struct DocAccum {
    double   score;
    uint32_t must_matched;
    bool     excluded;
    bool     matched_any;
    uint32_t matched_count;
    char     matched_terms[256];
} DocAccum;

static void append_matched_term(DocAccum *acc, const char *label) {
    if (!label || label[0] == '\0') return;
    /* Avoid duplicating same label in display string */
    if (acc->matched_terms[0] != '\0') {
        const char *found = strstr(acc->matched_terms, label);
        if (found) {
            bool left_ok = (found == acc->matched_terms) || (*(found - 1) == ' ' && *(found - 2) == ',');
            char after = found[strlen(label)];
            bool right_ok = (after == '\0' || after == ',');
            if (left_ok && right_ok) return;
        }
    }
    size_t cur_len = strlen(acc->matched_terms);
    size_t add_len = strlen(label) + (cur_len > 0 ? 2 : 0);
    if (cur_len + add_len + 1 < sizeof(acc->matched_terms)) {
        if (cur_len > 0) {
            strcat(acc->matched_terms, ", ");
        }
        strcat(acc->matched_terms, label);
    }
    acc->matched_count++;
}

static int cmp_search_result(const void *a, const void *b) {
    const SearchResult *ra = (const SearchResult *)a;
    const SearchResult *rb = (const SearchResult *)b;
    double diff = rb->score - ra->score;
    if (diff > 1e-12) return 1;
    if (diff < -1e-12) return -1;
    /* Deterministic tie-breaking: doc_id ascending */
    if (ra->doc_id < rb->doc_id) return -1;
    if (ra->doc_id > rb->doc_id) return 1;
    return 0;
}

static bool term_matches_pattern(const char *word, const char *pattern) {
    if (!word || !pattern) return false;
    size_t plen = strlen(pattern);
    if (plen > 1 && pattern[plen - 1] == '*') {
        return strncmp(word, pattern, plen - 1) == 0;
    }
    return strcmp(word, pattern) == 0;
}

void search_extract_snippet(const Document *doc, const char *terms[],
                            size_t term_count, char *out_buf, size_t buf_size) {
    if (!out_buf || buf_size == 0) return;
    out_buf[0] = '\0';
    if (!doc || term_count == 0) return;

    size_t len = 0;
    char *text = corpus_read_document_text(doc, &len);
    if (!text || len == 0) {
        free(text);
        return;
    }

    /* Find the earliest match position of any query term (case-insensitive) */
    size_t best_pos = len;
    for (size_t i = 0; i < len; ) {
        if (!isalnum((unsigned char)text[i])) {
            i++;
            continue;
        }
        size_t wstart = i;
        char word[MAX_TERM_LEN];
        size_t wlen = 0;
        while (i < len && isalnum((unsigned char)text[i])) {
            if (wlen + 1 < sizeof(word)) {
                word[wlen++] = (char)tolower((unsigned char)text[i]);
            }
            i++;
        }
        word[wlen] = '\0';
        for (size_t t = 0; t < term_count; t++) {
            if (term_matches_pattern(word, terms[t])) {
                if (wstart < best_pos) best_pos = wstart;
                break;
            }
        }
        if (best_pos < len) break;
    }

    if (best_pos >= len) best_pos = 0;
    size_t start = best_pos > 70 ? best_pos - 70 : 0;
    /* Align start to word boundary */
    if (start > 0) {
        while (start < best_pos && !isspace((unsigned char)text[start])) start++;
        while (start < best_pos && isspace((unsigned char)text[start])) start++;
    }

    size_t out_pos = 0;
    if (start > 0 && out_pos + 4 < buf_size) {
        memcpy(out_buf + out_pos, "...", 3);
        out_pos += 3;
    }

    size_t max_window = buf_size > 16 ? buf_size - 8 : buf_size - 1;
    size_t i = start;
    bool prev_space = false;

    while (i < len && out_pos < max_window) {
        unsigned char c = (unsigned char)text[i];
        if (isspace(c)) {
            if (!prev_space && out_pos > 0 && out_pos + 1 < max_window) {
                out_buf[out_pos++] = ' ';
            }
            prev_space = true;
            i++;
            continue;
        }
        prev_space = false;

        if (isalnum(c)) {
            size_t wstart = i;
            char word[MAX_TERM_LEN];
            size_t wlen = 0;
            while (i < len && isalnum((unsigned char)text[i])) {
                if (wlen + 1 < sizeof(word)) {
                    word[wlen++] = (char)tolower((unsigned char)text[i]);
                }
                i++;
            }
            word[wlen] = '\0';
            size_t orig_len = i - wstart;

            bool highlight = false;
            for (size_t t = 0; t < term_count; t++) {
                if (term_matches_pattern(word, terms[t])) {
                    highlight = true;
                    break;
                }
            }

            if (highlight && out_pos + orig_len + 2 < max_window) {
                out_buf[out_pos++] = '[';
                memcpy(out_buf + out_pos, text + wstart, orig_len);
                out_pos += orig_len;
                out_buf[out_pos++] = ']';
            } else {
                size_t copy_len = orig_len;
                if (out_pos + copy_len >= max_window) copy_len = max_window - out_pos;
                memcpy(out_buf + out_pos, text + wstart, copy_len);
                out_pos += copy_len;
            }
        } else {
            out_buf[out_pos++] = (char)c;
            i++;
        }
    }

    if (i < len && out_pos + 4 < buf_size) {
        memcpy(out_buf + out_pos, "...", 3);
        out_pos += 3;
    }
    out_buf[out_pos] = '\0';
    free(text);
}

static void collect_snippet_terms(const ParsedQuery *pq, char storage[][MAX_TERM_LEN + 2],
                                  const char *terms[], size_t *out_n, size_t max_n) {
    size_t n = 0;
    for (size_t i = 0; i < pq->count && n < max_n; i++) {
        const QueryClause *c = &pq->clauses[i];
        if (c->op == OP_MUST_NOT) continue;
        if (c->kind == CLAUSE_TERM) {
            snprintf(storage[n], MAX_TERM_LEN + 2, "%s", c->term);
            terms[n] = storage[n];
            n++;
        } else if (c->kind == CLAUSE_WILDCARD) {
            snprintf(storage[n], MAX_TERM_LEN + 2, "%s*", c->term);
            terms[n] = storage[n];
            n++;
        } else if (c->kind == CLAUSE_PHRASE) {
            for (size_t j = 0; j < c->phrase_len && n < max_n; j++) {
                snprintf(storage[n], MAX_TERM_LEN + 2, "%s", c->phrase_words[j]);
                terms[n] = storage[n];
                n++;
            }
        }
    }
    *out_n = n;
}

static bool finalize_results(const Corpus *corpus, const ParsedQuery *pq,
                             const DocAccum *accum, uint32_t must_total,
                             const SearchConfig *cfg, double t0,
                             SearchResponse *out_resp) {
    size_t match_count = 0;
    for (size_t d = 0; d < corpus->count; d++) {
        if (accum[d].excluded) continue;
        if (must_total > 0) {
            if (accum[d].must_matched < must_total) continue;
        } else {
            if (!accum[d].matched_any) continue;
        }
        match_count++;
    }

    out_resp->total_matches = match_count;
    if (match_count == 0) {
        out_resp->results = NULL;
        out_resp->returned_count = 0;
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    SearchResult *all = (SearchResult *)calloc(match_count, sizeof(SearchResult));
    if (!all) return false;

    size_t k = 0;
    for (size_t d = 0; d < corpus->count; d++) {
        if (accum[d].excluded) continue;
        if (must_total > 0) {
            if (accum[d].must_matched < must_total) continue;
        } else {
            if (!accum[d].matched_any) continue;
        }
        all[k].doc_id = (uint32_t)d;
        all[k].score = accum[d].score;
        all[k].matched_term_count = accum[d].matched_count;
        snprintf(all[k].matched_terms, sizeof(all[k].matched_terms), "%s", accum[d].matched_terms);
        k++;
    }

    qsort(all, match_count, sizeof(SearchResult), cmp_search_result);

    size_t top_k = (cfg->top_k == 0 || cfg->top_k > match_count) ? match_count : cfg->top_k;
    out_resp->results = all;
    out_resp->returned_count = top_k;
    out_resp->query_time_sec = csir_now_sec() - t0;

    if (cfg->show_snippets && top_k > 0) {
        char snip_storage[32][MAX_TERM_LEN + 2];
        const char *snip_terms[32];
        size_t snip_n = 0;
        collect_snippet_terms(pq, snip_storage, snip_terms, &snip_n, 32);
        if (snip_n > 0) {
            for (size_t i = 0; i < top_k; i++) {
                const Document *doc = corpus_get_doc(corpus, all[i].doc_id);
                search_extract_snippet(doc, snip_terms, snip_n,
                                       all[i].snippet, sizeof(all[i].snippet));
            }
        }
    }

    return true;
}

bool search_execute(const Corpus *corpus, const InvertedIndex *idx,
                    const char *query, const SearchConfig *cfg,
                    SearchResponse *out_resp) {
    if (!corpus || !idx || !cfg || !out_resp) return false;
    memset(out_resp, 0, sizeof(*out_resp));
    snprintf(out_resp->original_query, sizeof(out_resp->original_query), "%s", query ? query : "");

    double t0 = csir_now_sec();
    if (corpus->count == 0 || !query || query[0] == '\0') {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    ParsedQuery pq;
    parse_query(query, idx->stopwords_removed, &pq);
    snprintf(out_resp->normalized_summary, sizeof(out_resp->normalized_summary), "%s", pq.summary);
    out_resp->used_phrase = pq.used_phrase;
    out_resp->used_boolean = pq.used_boolean;
    out_resp->used_wildcard = pq.used_wildcard;

    if (pq.count == 0) {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    DocAccum *accum = (DocAccum *)calloc(corpus->count, sizeof(DocAccum));
    if (!accum) return false;

    uint32_t must_total = 0;

    for (size_t i = 0; i < pq.count; i++) {
        const QueryClause *c = &pq.clauses[i];
        if (c->op == OP_MUST) must_total++;

        if (c->kind == CLAUSE_TERM) {
            const TermEntry *e = index_lookup(idx, c->term);
            if (!e) continue;

            for (const PostingNode *p = e->postings_head; p; p = p->next) {
                if (p->doc_id >= corpus->count) continue;
                DocAccum *acc = &accum[p->doc_id];
                if (c->op == OP_MUST_NOT) {
                    acc->excluded = true;
                    continue;
                }
                double s = compute_term_score(cfg->scoring, p->term_freq, e->doc_freq,
                                              corpus->count, corpus->docs[p->doc_id].total_tokens,
                                              corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
                acc->score += s;
                acc->matched_any = true;
                if (c->op == OP_MUST) acc->must_matched++;
                append_matched_term(acc, e->term);
            }
        } else if (c->kind == CLAUSE_WILDCARD) {
            const TermEntry **matches = NULL;
            size_t mcount = index_lookup_prefix(idx, c->term, &matches);
            if (mcount == 0) continue;

            /* Track per-doc hit so OP_MUST increments at most once for this wildcard clause */
            bool *doc_hit = (bool *)calloc(corpus->count, sizeof(bool));
            if (!doc_hit) {
                free(matches);
                free(accum);
                return false;
            }

            for (size_t m = 0; m < mcount; m++) {
                const TermEntry *e = matches[m];
                for (const PostingNode *p = e->postings_head; p; p = p->next) {
                    if (p->doc_id >= corpus->count) continue;
                    DocAccum *acc = &accum[p->doc_id];
                    if (c->op == OP_MUST_NOT) {
                        acc->excluded = true;
                        continue;
                    }
                    double s = compute_term_score(cfg->scoring, p->term_freq, e->doc_freq,
                                                  corpus->count, corpus->docs[p->doc_id].total_tokens,
                                                  corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
                    acc->score += s;
                    acc->matched_any = true;
                    if (c->op == OP_MUST && !doc_hit[p->doc_id]) {
                        acc->must_matched++;
                        doc_hit[p->doc_id] = true;
                    }
                    append_matched_term(acc, e->term);
                }
            }
            free(doc_hit);
            free(matches);
        } else if (c->kind == CLAUSE_PHRASE) {
            const TermEntry *entries[MAX_PHRASE_WORDS];
            bool all_found = true;
            for (size_t w = 0; w < c->phrase_len; w++) {
                entries[w] = index_lookup(idx, c->phrase_words[w]);
                if (!entries[w]) {
                    all_found = false;
                    break;
                }
            }
            if (!all_found) continue;

            /* Count phrase document frequency first for deterministic IDF */
            uint32_t phrase_df = 0;
            for (const PostingNode *p = entries[0]->postings_head; p; p = p->next) {
                if (count_phrase_matches_in_doc(entries, c->phrase_offsets, c->phrase_len, p) > 0) {
                    phrase_df++;
                }
            }
            if (phrase_df == 0) continue;

            char phrase_label[128];
            size_t lpos = (size_t)snprintf(phrase_label, sizeof(phrase_label), "\"");
            for (size_t w = 0; w < c->phrase_len; w++) {
                lpos += (size_t)snprintf(phrase_label + lpos, sizeof(phrase_label) - lpos,
                                         "%s%s", w > 0 ? " " : "", c->phrase_words[w]);
            }
            snprintf(phrase_label + lpos, sizeof(phrase_label) - lpos, "\"");

            for (const PostingNode *p = entries[0]->postings_head; p; p = p->next) {
                if (p->doc_id >= corpus->count) continue;
                uint32_t ptf = count_phrase_matches_in_doc(entries, c->phrase_offsets, c->phrase_len, p);
                if (ptf == 0) continue;

                DocAccum *acc = &accum[p->doc_id];
                if (c->op == OP_MUST_NOT) {
                    acc->excluded = true;
                    continue;
                }
                double s = compute_term_score(cfg->scoring, ptf, phrase_df,
                                              corpus->count, corpus->docs[p->doc_id].total_tokens,
                                              corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
                /* Phrase bonus (1.5x) rewards exact phrase proximity */
                acc->score += (cfg->scoring == SCORE_BASIC_TF) ? (double)ptf : (s * 1.5);
                acc->matched_any = true;
                if (c->op == OP_MUST) acc->must_matched++;
                append_matched_term(acc, phrase_label);
            }
        }
    }

    bool ok = finalize_results(corpus, &pq, accum, must_total, cfg, t0, out_resp);
    free(accum);
    return ok;
}

/* ==================== Baseline Linear Scan (Experiment A) ==================== */

typedef struct LinearScanCtx {
    const ParsedQuery *pq;
    uint32_t          *term_freqs;
} LinearScanCtx;

static void on_linear_token(const char *token, uint32_t pos, void *user_data) {
    (void)pos;
    LinearScanCtx *ctx = (LinearScanCtx *)user_data;
    for (size_t i = 0; i < ctx->pq->count; i++) {
        if (ctx->pq->clauses[i].kind == CLAUSE_TERM &&
            strcmp(token, ctx->pq->clauses[i].term) == 0) {
            ctx->term_freqs[i]++;
        }
    }
}

bool search_linear_scan(const Corpus *corpus, const char *query,
                        const SearchConfig *cfg, SearchResponse *out_resp) {
    if (!corpus || !cfg || !out_resp) return false;
    memset(out_resp, 0, sizeof(*out_resp));
    snprintf(out_resp->original_query, sizeof(out_resp->original_query), "%s", query ? query : "");

    double t0 = csir_now_sec();
    if (corpus->count == 0 || !query || query[0] == '\0') {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    ParsedQuery pq;
    parse_query(query, cfg->remove_stopwords, &pq);
    snprintf(out_resp->normalized_summary, sizeof(out_resp->normalized_summary), "%s", pq.summary);
    if (pq.count == 0) {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    /* Two-pass linear scan: pass 1 reads every document from disk to count tf[d][q] and df[q] */
    uint32_t *doc_term_tf = (uint32_t *)calloc(corpus->count * pq.count, sizeof(uint32_t));
    uint32_t *df = (uint32_t *)calloc(pq.count, sizeof(uint32_t));
    DocAccum *accum = (DocAccum *)calloc(corpus->count, sizeof(DocAccum));
    if (!doc_term_tf || !df || !accum) {
        free(doc_term_tf);
        free(df);
        free(accum);
        return false;
    }

    for (size_t d = 0; d < corpus->count; d++) {
        char *text = corpus_read_document_text(&corpus->docs[d], NULL);
        if (!text) continue;
        LinearScanCtx lctx = { &pq, &doc_term_tf[d * pq.count] };
        tokenizer_process_text(text, cfg->remove_stopwords, on_linear_token, &lctx, NULL);
        free(text);
        for (size_t i = 0; i < pq.count; i++) {
            if (doc_term_tf[d * pq.count + i] > 0) {
                df[i]++;
            }
        }
    }

    uint32_t must_total = 0;
    for (size_t i = 0; i < pq.count; i++) {
        if (pq.clauses[i].op == OP_MUST) must_total++;
    }

    for (size_t d = 0; d < corpus->count; d++) {
        for (size_t i = 0; i < pq.count; i++) {
            uint32_t tf = doc_term_tf[d * pq.count + i];
            if (tf == 0) continue;
            if (pq.clauses[i].op == OP_MUST_NOT) {
                accum[d].excluded = true;
                continue;
            }
            double s = compute_term_score(cfg->scoring, tf, df[i], corpus->count,
                                          corpus->docs[d].total_tokens,
                                          corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
            accum[d].score += s;
            accum[d].matched_any = true;
            if (pq.clauses[i].op == OP_MUST) accum[d].must_matched++;
            append_matched_term(&accum[d], pq.clauses[i].term);
        }
    }

    SearchConfig no_snip = *cfg;
    no_snip.show_snippets = false;
    bool ok = finalize_results(corpus, &pq, accum, must_total, &no_snip, t0, out_resp);
    free(doc_term_tf);
    free(df);
    free(accum);
    return ok;
}

/* ==================== Linked-List Index Search (Experiment B) ==================== */

bool search_linked_list(const Corpus *corpus, const LinkedListIndex *ll,
                        const char *query, const SearchConfig *cfg,
                        SearchResponse *out_resp) {
    if (!corpus || !ll || !cfg || !out_resp) return false;
    memset(out_resp, 0, sizeof(*out_resp));

    double t0 = csir_now_sec();
    ParsedQuery pq;
    parse_query(query, cfg->remove_stopwords, &pq);
    if (pq.count == 0 || corpus->count == 0) {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    DocAccum *accum = (DocAccum *)calloc(corpus->count, sizeof(DocAccum));
    if (!accum) return false;

    uint32_t must_total = 0;
    for (size_t i = 0; i < pq.count; i++) {
        const QueryClause *c = &pq.clauses[i];
        if (c->op == OP_MUST) must_total++;
        if (c->kind != CLAUSE_TERM) continue;

        const ListTermNode *e = ll_index_lookup(ll, c->term);
        if (!e) continue;
        for (const PostingNode *p = e->postings_head; p; p = p->next) {
            if (p->doc_id >= corpus->count) continue;
            if (c->op == OP_MUST_NOT) {
                accum[p->doc_id].excluded = true;
                continue;
            }
            double s = compute_term_score(cfg->scoring, p->term_freq, e->doc_freq,
                                          corpus->count, corpus->docs[p->doc_id].total_tokens,
                                          corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
            accum[p->doc_id].score += s;
            accum[p->doc_id].matched_any = true;
            if (c->op == OP_MUST) accum[p->doc_id].must_matched++;
            append_matched_term(&accum[p->doc_id], e->term);
        }
    }

    SearchConfig no_snip = *cfg;
    no_snip.show_snippets = false;
    bool ok = finalize_results(corpus, &pq, accum, must_total, &no_snip, t0, out_resp);
    free(accum);
    return ok;
}

/* ==================== Sorted-Array Binary Search (Experiment D) ==================== */

bool search_sorted_array(const Corpus *corpus, const SortedArrayIndex *sa,
                         const char *query, const SearchConfig *cfg,
                         SearchResponse *out_resp) {
    if (!corpus || !sa || !cfg || !out_resp) return false;
    memset(out_resp, 0, sizeof(*out_resp));

    double t0 = csir_now_sec();
    ParsedQuery pq;
    parse_query(query, cfg->remove_stopwords, &pq);
    if (pq.count == 0 || corpus->count == 0) {
        out_resp->query_time_sec = csir_now_sec() - t0;
        return true;
    }

    DocAccum *accum = (DocAccum *)calloc(corpus->count, sizeof(DocAccum));
    if (!accum) return false;

    uint32_t must_total = 0;
    for (size_t i = 0; i < pq.count; i++) {
        const QueryClause *c = &pq.clauses[i];
        if (c->op == OP_MUST) must_total++;
        if (c->kind != CLAUSE_TERM) continue;

        const TermEntry *e = sa_index_lookup(sa, c->term);
        if (!e) continue;
        for (const PostingNode *p = e->postings_head; p; p = p->next) {
            if (p->doc_id >= corpus->count) continue;
            if (c->op == OP_MUST_NOT) {
                accum[p->doc_id].excluded = true;
                continue;
            }
            double s = compute_term_score(cfg->scoring, p->term_freq, e->doc_freq,
                                          corpus->count, corpus->docs[p->doc_id].total_tokens,
                                          corpus->avg_doc_len, cfg->bm25_k1, cfg->bm25_b);
            accum[p->doc_id].score += s;
            accum[p->doc_id].matched_any = true;
            if (c->op == OP_MUST) accum[p->doc_id].must_matched++;
            append_matched_term(&accum[p->doc_id], e->term);
        }
    }

    SearchConfig no_snip = *cfg;
    no_snip.show_snippets = false;
    bool ok = finalize_results(corpus, &pq, accum, must_total, &no_snip, t0, out_resp);
    free(accum);
    return ok;
}
