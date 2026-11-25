#include "persistence.h"

#include <stdlib.h>
#include <string.h>

static bool write_u32(FILE *fp, uint32_t v) {
    return fwrite(&v, sizeof(v), 1, fp) == 1;
}

static bool write_u64(FILE *fp, uint64_t v) {
    return fwrite(&v, sizeof(v), 1, fp) == 1;
}

static bool write_double(FILE *fp, double v) {
    return fwrite(&v, sizeof(v), 1, fp) == 1;
}

static bool write_str(FILE *fp, const char *s) {
    uint32_t len = s ? (uint32_t)strlen(s) : 0;
    if (!write_u32(fp, len)) return false;
    if (len > 0 && fwrite(s, 1, len, fp) != len) return false;
    return true;
}

static bool read_u32(FILE *fp, uint32_t *v) {
    return fread(v, sizeof(*v), 1, fp) == 1;
}

static bool read_u64(FILE *fp, uint64_t *v) {
    return fread(v, sizeof(*v), 1, fp) == 1;
}

static bool read_double(FILE *fp, double *v) {
    return fread(v, sizeof(*v), 1, fp) == 1;
}

static bool read_str_into(FILE *fp, char *buf, size_t buf_size) {
    uint32_t len = 0;
    if (!read_u32(fp, &len)) return false;
    if (len >= buf_size) return false;
    if (len > 0 && fread(buf, 1, len, fp) != len) return false;
    buf[len] = '\0';
    return true;
}

bool persistence_save_index(const char *filepath, const Corpus *corpus,
                            const InvertedIndex *idx, double *out_elapsed_sec) {
    if (!filepath || !corpus || !idx) return false;

    double t0 = csir_now_sec();
    FILE *fp = fopen(filepath, "wb");
    if (!fp) return false;

    bool ok = true;
    /* Header: magic(4) + version(u32) + flags(u32) + doc_count(u32) + unique_terms(u32)
     *         + total_raw_tokens(u64) + total_indexed_tokens(u64) + avg_doc_len(double)
     *         + source_dir(str) */
    uint32_t version = CSIR_IDX_VERSION;
    uint32_t flags = idx->stopwords_removed ? 1U : 0U;

    ok = ok && fwrite(CSIR_IDX_MAGIC, 1, 4, fp) == 4;
    ok = ok && write_u32(fp, version);
    ok = ok && write_u32(fp, flags);
    ok = ok && write_u32(fp, (uint32_t)corpus->count);
    ok = ok && write_u32(fp, (uint32_t)idx->unique_terms);
    ok = ok && write_u64(fp, corpus->total_raw_tokens);
    ok = ok && write_u64(fp, corpus->total_indexed_tokens);
    ok = ok && write_double(fp, corpus->avg_doc_len);
    ok = ok && write_str(fp, corpus->source_dir);

    /* Document table */
    for (size_t i = 0; ok && i < corpus->count; i++) {
        const Document *d = &corpus->docs[i];
        ok = ok && write_u32(fp, d->doc_id);
        ok = ok && write_u64(fp, (uint64_t)d->file_size);
        ok = ok && write_u32(fp, d->total_tokens);
        ok = ok && write_u32(fp, d->raw_tokens);
        ok = ok && write_u32(fp, d->unique_terms);
        ok = ok && write_u32(fp, d->is_empty ? 1U : 0U);
        ok = ok && write_str(fp, d->filename);
        ok = ok && write_str(fp, d->filepath);
        ok = ok && write_str(fp, d->title);
    }

    /* Vocabulary + Posting lists */
    for (size_t b = 0; ok && b < idx->bucket_count; b++) {
        for (const TermEntry *e = idx->buckets[b]; ok && e; e = e->next) {
            ok = ok && write_str(fp, e->term);
            ok = ok && write_u32(fp, e->doc_freq);
            for (const PostingNode *p = e->postings_head; ok && p; p = p->next) {
                ok = ok && write_u32(fp, p->doc_id);
                ok = ok && write_u32(fp, p->term_freq);
                if (p->term_freq > 0) {
                    ok = ok && fwrite(p->positions, sizeof(uint32_t), p->term_freq, fp) == p->term_freq;
                }
            }
        }
    }

    fclose(fp);
    if (out_elapsed_sec) {
        *out_elapsed_sec = csir_now_sec() - t0;
    }
    return ok;
}

bool persistence_load_index(const char *filepath, Corpus *corpus,
                            InvertedIndex *idx, double *out_elapsed_sec) {
    if (!filepath || !corpus || !idx) return false;

    double t0 = csir_now_sec();
    FILE *fp = fopen(filepath, "rb");
    if (!fp) return false;

    char magic[4];
    uint32_t version = 0, flags = 0, doc_count = 0, unique_terms = 0;
    uint64_t total_raw = 0, total_indexed = 0;
    double avg_doc_len = 0.0;
    char source_dir[MAX_PATH_LEN];

    bool ok = true;
    ok = ok && fread(magic, 1, 4, fp) == 4 && memcmp(magic, CSIR_IDX_MAGIC, 4) == 0;
    ok = ok && read_u32(fp, &version) && version == CSIR_IDX_VERSION;
    ok = ok && read_u32(fp, &flags);
    ok = ok && read_u32(fp, &doc_count);
    ok = ok && read_u32(fp, &unique_terms);
    ok = ok && read_u64(fp, &total_raw);
    ok = ok && read_u64(fp, &total_indexed);
    ok = ok && read_double(fp, &avg_doc_len);
    ok = ok && read_str_into(fp, source_dir, sizeof(source_dir));

    if (!ok) {
        fclose(fp);
        return false;
    }

    Corpus new_corpus;
    InvertedIndex new_idx;
    corpus_init(&new_corpus);
    index_init(&new_idx, unique_terms * 2 + 64);

    snprintf(new_corpus.source_dir, sizeof(new_corpus.source_dir), "%s", source_dir);
    new_corpus.total_raw_tokens = total_raw;
    new_corpus.total_indexed_tokens = total_indexed;
    new_corpus.avg_doc_len = avg_doc_len;
    new_idx.stopwords_removed = (flags & 1U) != 0;

    for (uint32_t i = 0; ok && i < doc_count; i++) {
        uint32_t doc_id = 0, total_tok = 0, raw_tok = 0, uniq_tok = 0, is_empty = 0;
        uint64_t file_size = 0;
        char filename[MAX_FILENAME_LEN];
        char fpath[MAX_PATH_LEN];
        char title[MAX_TITLE_LEN];

        ok = ok && read_u32(fp, &doc_id);
        ok = ok && read_u64(fp, &file_size);
        ok = ok && read_u32(fp, &total_tok);
        ok = ok && read_u32(fp, &raw_tok);
        ok = ok && read_u32(fp, &uniq_tok);
        ok = ok && read_u32(fp, &is_empty);
        ok = ok && read_str_into(fp, filename, sizeof(filename));
        ok = ok && read_str_into(fp, fpath, sizeof(fpath));
        ok = ok && read_str_into(fp, title, sizeof(title));

        if (ok) {
            Document *d = corpus_add_entry(&new_corpus, filename, fpath, title, (size_t)file_size);
            if (!d) {
                ok = false;
            } else {
                d->doc_id = doc_id;
                d->total_tokens = total_tok;
                d->raw_tokens = raw_tok;
                d->unique_terms = uniq_tok;
                d->is_empty = (is_empty != 0);
                if (d->is_empty) new_corpus.empty_files++;
            }
        }
    }

    uint32_t *pos_buf = NULL;
    size_t pos_buf_cap = 0;

    for (uint32_t t = 0; ok && t < unique_terms; t++) {
        char term[MAX_TERM_LEN];
        uint32_t doc_freq = 0;
        ok = ok && read_str_into(fp, term, sizeof(term));
        ok = ok && read_u32(fp, &doc_freq);

        for (uint32_t p = 0; ok && p < doc_freq; p++) {
            uint32_t doc_id = 0, term_freq = 0;
            ok = ok && read_u32(fp, &doc_id);
            ok = ok && read_u32(fp, &term_freq);
            if (!ok) break;

            if (term_freq > pos_buf_cap) {
                uint32_t *tmp = (uint32_t *)realloc(pos_buf, (size_t)term_freq * sizeof(uint32_t));
                if (!tmp) {
                    ok = false;
                    break;
                }
                pos_buf = tmp;
                pos_buf_cap = term_freq;
            }
            if (term_freq > 0) {
                ok = ok && fread(pos_buf, sizeof(uint32_t), term_freq, fp) == term_freq;
            }
            if (ok) {
                if (!index_insert_posting(&new_idx, term, doc_id, term_freq, pos_buf)) {
                    ok = false;
                }
            }
        }
    }

    free(pos_buf);
    fclose(fp);

    if (!ok) {
        corpus_free(&new_corpus);
        index_free(&new_idx);
        return false;
    }

    corpus_free(corpus);
    index_free(idx);
    *corpus = new_corpus;
    *idx = new_idx;
    idx->build_time_sec = csir_now_sec() - t0;

    if (out_elapsed_sec) {
        *out_elapsed_sec = idx->build_time_sec;
    }
    return true;
}
