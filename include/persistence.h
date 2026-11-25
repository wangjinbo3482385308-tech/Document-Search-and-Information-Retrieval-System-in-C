#ifndef CSIR_PERSISTENCE_H
#define CSIR_PERSISTENCE_H

#include "common.h"
#include "corpus.h"
#include "index.h"

#define CSIR_IDX_MAGIC   "CSIR"
#define CSIR_IDX_VERSION 1

/* Save corpus metadata and full inverted index (including positions) to binary file.
 * Returns true on success. */
bool persistence_save_index(const char *filepath, const Corpus *corpus,
                            const InvertedIndex *idx, double *out_elapsed_sec);

/* Load corpus metadata and full inverted index from binary file.
 * Replaces existing corpus and index contents. Returns true on success. */
bool persistence_load_index(const char *filepath, Corpus *corpus,
                            InvertedIndex *idx, double *out_elapsed_sec);

#endif /* CSIR_PERSISTENCE_H */
