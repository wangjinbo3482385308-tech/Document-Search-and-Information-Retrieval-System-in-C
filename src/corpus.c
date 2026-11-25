#include "corpus.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void corpus_init(Corpus *corpus) {
    if (!corpus) return;
    memset(corpus, 0, sizeof(*corpus));
}

void corpus_free(Corpus *corpus) {
    if (!corpus) return;
    free(corpus->docs);
    memset(corpus, 0, sizeof(*corpus));
}

static void extract_title_from_text(const char *text, const char *fallback_name,
                                    char *out_title, size_t out_size) {
    if (!out_title || out_size == 0) return;
    if (!text || text[0] == '\0') {
        snprintf(out_title, out_size, "%s", fallback_name ? fallback_name : "(empty)");
        return;
    }

    /* Check for TREC <TITLE> tag or Markdown '#' heading or first non-empty line */
    const char *p = text;
    const char *trec_title = strstr(text, "<TITLE>");
    if (trec_title) {
        p = trec_title + 7;
    }

    while (*p && (isspace((unsigned char)*p) || *p == '#')) {
        p++;
    }

    size_t i = 0;
    while (*p && *p != '\n' && *p != '\r' && i + 1 < out_size) {
        if (trec_title && strncmp(p, "</TITLE>", 8) == 0) {
            break;
        }
        if (*p == '\t') {
            out_title[i++] = ' ';
        } else {
            out_title[i++] = *p;
        }
        p++;
    }
    out_title[i] = '\0';
    csir_trim(out_title);

    if (out_title[0] == '\0') {
        snprintf(out_title, out_size, "%s", fallback_name ? fallback_name : "(untitled)");
    }
}

Document *corpus_add_entry(Corpus *corpus, const char *filename, const char *filepath,
                           const char *title, size_t file_size) {
    if (!corpus) return NULL;
    if (corpus->count == corpus->capacity) {
        size_t new_cap = corpus->capacity == 0 ? 64 : corpus->capacity * 2;
        Document *tmp = (Document *)realloc(corpus->docs, new_cap * sizeof(Document));
        if (!tmp) return NULL;
        corpus->docs = tmp;
        corpus->capacity = new_cap;
    }

    Document *doc = &corpus->docs[corpus->count];
    memset(doc, 0, sizeof(*doc));
    doc->doc_id = (uint32_t)corpus->count;
    snprintf(doc->filename, sizeof(doc->filename), "%s", filename ? filename : "");
    snprintf(doc->filepath, sizeof(doc->filepath), "%s", filepath ? filepath : "");
    snprintf(doc->title, sizeof(doc->title), "%s", title ? title : "");
    doc->file_size = file_size;
    doc->is_empty = (file_size == 0);

    corpus->count++;
    return doc;
}

char *corpus_read_document_text(const Document *doc, size_t *out_len) {
    if (out_len) *out_len = 0;
    if (!doc || doc->filepath[0] == '\0') return NULL;

    FILE *fp = fopen(doc->filepath, "rb");
    if (!fp) return NULL;

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    long sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return NULL;
    }
    rewind(fp);

    size_t size = (size_t)sz;
    char *buf = (char *)malloc(size + 1);
    if (!buf) {
        fclose(fp);
        return NULL;
    }

    size_t nread = size > 0 ? fread(buf, 1, size, fp) : 0;
    fclose(fp);

    buf[nread] = '\0';
    if (out_len) *out_len = nread;
    return buf;
}

int corpus_load_dir(Corpus *corpus, const char *dir_path) {
    if (!corpus || !dir_path || dir_path[0] == '\0') return -1;

    corpus_free(corpus);
    corpus_init(corpus);
    snprintf(corpus->source_dir, sizeof(corpus->source_dir), "%s", dir_path);

    size_t file_count = 0;
    char **files = csir_list_text_files(dir_path, &file_count);
    if (!files) {
        return -1;
    }

    for (size_t i = 0; i < file_count; i++) {
        char fullpath[MAX_PATH_LEN];
        csir_join_path(fullpath, sizeof(fullpath), dir_path, files[i]);

        FILE *fp = fopen(fullpath, "rb");
        if (!fp) {
            corpus->skipped_files++;
            free(files[i]);
            continue;
        }

        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            corpus->skipped_files++;
            free(files[i]);
            continue;
        }
        long sz = ftell(fp);
        if (sz < 0) {
            fclose(fp);
            corpus->skipped_files++;
            free(files[i]);
            continue;
        }
        rewind(fp);

        /* Read a small header chunk to extract the document title */
        char header[512];
        size_t to_read = (size_t)sz < sizeof(header) - 1 ? (size_t)sz : sizeof(header) - 1;
        size_t nread = to_read > 0 ? fread(header, 1, to_read, fp) : 0;
        header[nread] = '\0';
        fclose(fp);

        char title[MAX_TITLE_LEN];
        extract_title_from_text(header, files[i], title, sizeof(title));

        Document *doc = corpus_add_entry(corpus, files[i], fullpath, title, (size_t)sz);
        if (doc && doc->is_empty) {
            corpus->empty_files++;
        }

        free(files[i]);
    }
    free(files);

    return (int)corpus->count;
}

const Document *corpus_get_doc(const Corpus *corpus, uint32_t doc_id) {
    if (!corpus || doc_id >= corpus->count) return NULL;
    return &corpus->docs[doc_id];
}

int corpus_find_by_filename(const Corpus *corpus, const char *filename) {
    if (!corpus || !filename) return -1;
    for (size_t i = 0; i < corpus->count; i++) {
        if (strcmp(corpus->docs[i].filename, filename) == 0) {
            return (int)i;
        }
    }
    return -1;
}
