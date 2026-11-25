#include "tokenizer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

/* Strictly sorted (ASCII ascending) deterministic English stopword list for binary search */
static const char *const ENGLISH_STOPWORDS[] = {
    "a", "about", "above", "after", "again", "against", "all", "am", "an", "and",
    "any", "are", "as", "at", "be", "because", "been", "before", "being", "below",
    "between", "both", "but", "by", "can", "could", "did", "do", "does", "doing",
    "down", "during", "each", "few", "for", "from", "further", "had", "has", "have",
    "having", "he", "her", "here", "hers", "herself", "him", "himself", "his", "how",
    "i", "if", "in", "into", "is", "it", "its", "itself", "just", "may",
    "me", "might", "more", "most", "must", "my", "myself", "no", "nor", "not",
    "now", "of", "off", "on", "once", "only", "or", "other", "our", "ours",
    "ourselves", "out", "over", "own", "s", "same", "shall", "she", "should", "so",
    "some", "such", "t", "than", "that", "the", "their", "theirs", "them", "themselves",
    "then", "there", "these", "they", "this", "those", "through", "to", "too", "under",
    "until", "up", "very", "was", "we", "were", "what", "when", "where", "which",
    "while", "who", "whom", "why", "will", "with", "would", "you", "your", "yours",
    "yourself", "yourselves"
};

static const size_t STOPWORD_COUNT = sizeof(ENGLISH_STOPWORDS) / sizeof(ENGLISH_STOPWORDS[0]);

static int cmp_stopword(const void *key, const void *elem) {
    const char *k = (const char *)key;
    const char *e = *(const char *const *)elem;
    return strcmp(k, e);
}

bool tokenizer_is_stopword(const char *term) {
    if (!term || term[0] == '\0') return false;
    return bsearch(term, ENGLISH_STOPWORDS, STOPWORD_COUNT,
                   sizeof(ENGLISH_STOPWORDS[0]), cmp_stopword) != NULL;
}

size_t tokenizer_normalize_word(const char *src, char *dst, size_t dst_size) {
    if (!src || !dst || dst_size == 0) return 0;
    size_t len = 0;
    for (const unsigned char *p = (const unsigned char *)src; *p && len + 1 < dst_size; p++) {
        if (isalnum(*p)) {
            dst[len++] = (char)tolower(*p);
        }
    }
    dst[len] = '\0';
    return len;
}

uint32_t tokenizer_process_text(const char *text,
                                bool remove_stopwords,
                                TokenCallback cb,
                                void *user_data,
                                uint32_t *out_kept) {
    if (out_kept) *out_kept = 0;
    if (!text) return 0;

    char token[MAX_TERM_LEN];
    size_t tlen = 0;
    uint32_t raw_count = 0;
    uint32_t kept_count = 0;
    bool in_tag = false; /* gracefully skip simple SGML/TREC tags like <DOC>, <TEXT> */

    const unsigned char *p = (const unsigned char *)text;
    while (true) {
        unsigned char c = *p;

        if (c == '<') {
            /* Flush any active token before tag */
            if (tlen > 0) {
                token[tlen] = '\0';
                uint32_t pos = raw_count++;
                if (!remove_stopwords || !tokenizer_is_stopword(token)) {
                    kept_count++;
                    if (cb) cb(token, pos, user_data);
                }
                tlen = 0;
            }
            /* Check if this looks like an SGML/HTML tag (e.g., <TEXT>, </DOC>) */
            const unsigned char *q = p + 1;
            if (*q == '/' || isalpha(*q)) {
                while (*q && *q != '>' && *q != '\n' && (size_t)(q - p) < 32) q++;
                if (*q == '>') {
                    in_tag = true;
                    p = q + 1;
                    in_tag = false;
                    continue;
                }
            }
            p++;
            continue;
        }

        if (!in_tag && isalnum(c)) {
            if (tlen + 1 < sizeof(token)) {
                token[tlen++] = (char)tolower(c);
            }
        } else {
            if (tlen > 0) {
                token[tlen] = '\0';
                uint32_t pos = raw_count++;
                if (!remove_stopwords || !tokenizer_is_stopword(token)) {
                    kept_count++;
                    if (cb) cb(token, pos, user_data);
                }
                tlen = 0;
            }
            if (c == '\0') break;
        }

        if (c == '\0') break;
        p++;
    }

    if (out_kept) *out_kept = kept_count;
    return raw_count;
}
