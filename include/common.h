#ifndef CSIR_COMMON_H
#define CSIR_COMMON_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#define CSIR_VERSION       "1.0.0"
#define MAX_TERM_LEN       64
#define MAX_PATH_LEN       512
#define MAX_FILENAME_LEN   256
#define MAX_TITLE_LEN      128
#define MAX_QUERY_LEN      512
#define MAX_LINE_LEN       4096

/* High-resolution monotonic wall-clock timer in seconds */
double csir_now_sec(void);

/* Cross-platform safe string duplication */
char *csir_strdup(const char *s);

/* Trim leading and trailing whitespace in-place */
char *csir_trim(char *s);

/* Case-insensitive string comparison */
int csir_strcasecmp(const char *a, const char *b);

/* Safe line input from stdin; returns false on EOF */
bool csir_read_line(char *buf, size_t size);

/* Parse integer safely from string */
bool csir_parse_int(const char *s, int *out);

/* List files matching supported text extensions in a directory.
 * Caller must free each string and the returned array. */
char **csir_list_text_files(const char *dir_path, size_t *out_count);

/* Join directory path and filename cleanly */
void csir_join_path(char *dst, size_t dst_size, const char *dir, const char *name);

#endif /* CSIR_COMMON_H */
