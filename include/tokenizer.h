#ifndef CSIR_TOKENIZER_H
#define CSIR_TOKENIZER_H

#include "common.h"

/* Callback invoked for each extracted normalized token.
 * position is the 0-based token index in the document (before stopword skipping, or sequential). */
typedef void (*TokenCallback)(const char *token, uint32_t position, void *user_data);

/* Check whether a normalized lowercase term is a built-in English stopword */
bool tokenizer_is_stopword(const char *term);

/* Normalize a single raw token in place (lowercase alphanumeric).
 * Returns length of normalized token (0 if empty). */
size_t tokenizer_normalize_word(const char *src, char *dst, size_t dst_size);

/* Tokenize text buffer and invoke callback for every valid normalized term.
 * Returns total raw tokens encountered (before stopword removal).
 * If out_kept != NULL, stores number of tokens passed to callback. */
uint32_t tokenizer_process_text(const char *text,
                                bool remove_stopwords,
                                TokenCallback cb,
                                void *user_data,
                                uint32_t *out_kept);

#endif /* CSIR_TOKENIZER_H */
