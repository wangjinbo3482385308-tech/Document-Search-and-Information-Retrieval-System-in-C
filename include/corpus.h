#ifndef CSIR_CORPUS_H
#define CSIR_CORPUS_H

#include "common.h"

typedef struct Document {
    uint32_t doc_id;
    char     filename[MAX_FILENAME_LEN];
    char     filepath[MAX_PATH_LEN];
    char     title[MAX_TITLE_LEN];
    size_t   file_size;       /* bytes */
    uint32_t total_tokens;    /* all normalized tokens (after optional stopword filter) */
    uint32_t raw_tokens;      /* all normalized tokens before stopword filter */
    uint32_t unique_terms;    /* distinct indexed terms in this doc */
    bool     is_empty;
} Document;

typedef struct Corpus {
    Document *docs;
    size_t    count;
    size_t    capacity;
    char      source_dir[MAX_PATH_LEN];
    uint64_t  total_raw_tokens;
    uint64_t  total_indexed_tokens;
    double    avg_doc_len;
    size_t    skipped_files;
    size_t    empty_files;
} Corpus;

/* Initialize an empty corpus */
void corpus_init(Corpus *corpus);

/* Free all memory owned by corpus */
void corpus_free(Corpus *corpus);

/* Load all .txt / .md / .trec documents from a directory.
 * Returns number of loaded documents, or -1 on fatal error (e.g., invalid directory). */
int corpus_load_dir(Corpus *corpus, const char *dir_path);

/* Read entire text content of a document from disk (caller must free returned buffer).
 * Sets *out_len if non-NULL. Returns NULL on error. */
char *corpus_read_document_text(const Document *doc, size_t *out_len);

/* Add a document entry manually (used during index reload or testing) */
Document *corpus_add_entry(Corpus *corpus, const char *filename, const char *filepath,
                           const char *title, size_t file_size);

/* Find document by ID (returns NULL if out of range) */
const Document *corpus_get_doc(const Corpus *corpus, uint32_t doc_id);

/* Find document ID by filename (returns -1 if not found) */
int corpus_find_by_filename(const Corpus *corpus, const char *filename);

#endif /* CSIR_CORPUS_H */
