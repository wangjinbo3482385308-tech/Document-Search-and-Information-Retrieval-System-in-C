#include "common.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

double csir_now_sec(void) {
#ifdef _WIN32
    static LARGE_INTEGER freq;
    static int initialized = 0;
    LARGE_INTEGER counter;
    if (!initialized) {
        QueryPerformanceFrequency(&freq);
        initialized = 1;
    }
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

char *csir_strdup(const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s) + 1;
    char *copy = (char *)malloc(len);
    if (!copy) return NULL;
    memcpy(copy, s, len);
    return copy;
}

char *csir_trim(char *s) {
    if (!s) return NULL;
    while (*s && isspace((unsigned char)*s)) {
        s++;
    }
    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
    return s;
}

int csir_strcasecmp(const char *a, const char *b) {
    if (!a && !b) return 0;
    if (!a) return -1;
    if (!b) return 1;
    while (*a && *b) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca != cb) return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char)*a) - tolower((unsigned char)*b);
}

bool csir_read_line(char *buf, size_t size) {
    if (!buf || size == 0) return false;
    if (!fgets(buf, (int)size, stdin)) {
        buf[0] = '\0';
        return false;
    }
    size_t len = strlen(buf);
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
        buf[--len] = '\0';
    }
    return true;
}

bool csir_parse_int(const char *s, int *out) {
    if (!s || !out) return false;
    while (*s && isspace((unsigned char)*s)) s++;
    if (*s == '\0') return false;
    char *end = NULL;
    errno = 0;
    long val = strtol(s, &end, 10);
    if (errno != 0 || end == s) return false;
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end != '\0') return false;
    if (val < INT_MIN || val > INT_MAX) return false;
    *out = (int)val;
    return true;
}

void csir_join_path(char *dst, size_t dst_size, const char *dir, const char *name) {
    if (!dst || dst_size == 0) return;
    size_t dlen = dir ? strlen(dir) : 0;
    if (dlen == 0) {
        snprintf(dst, dst_size, "%s", name ? name : "");
        return;
    }
    char last = dir[dlen - 1];
    if (last == '/' || last == '\\') {
        snprintf(dst, dst_size, "%s%s", dir, name ? name : "");
    } else {
        snprintf(dst, dst_size, "%s/%s", dir, name ? name : "");
    }
}

static bool has_supported_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    if (!dot) return false;
    return csir_strcasecmp(dot, ".txt") == 0 ||
           csir_strcasecmp(dot, ".md") == 0 ||
           csir_strcasecmp(dot, ".trec") == 0;
}

static int cmp_str_ptr(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

char **csir_list_text_files(const char *dir_path, size_t *out_count) {
    if (out_count) *out_count = 0;
    if (!dir_path || dir_path[0] == '\0') return NULL;

    size_t cap = 64;
    size_t count = 0;
    char **files = (char **)malloc(cap * sizeof(char *));
    if (!files) return NULL;

#ifdef _WIN32
    char pattern[MAX_PATH_LEN];
    size_t dlen = strlen(dir_path);
    if (dlen > 0 && (dir_path[dlen - 1] == '/' || dir_path[dlen - 1] == '\\')) {
        snprintf(pattern, sizeof(pattern), "%s*", dir_path);
    } else {
        snprintf(pattern, sizeof(pattern), "%s/*", dir_path);
    }

    WIN32_FIND_DATAA fd;
    HANDLE hFind = FindFirstFileA(pattern, &fd);
    if (hFind == INVALID_HANDLE_VALUE) {
        free(files);
        return NULL;
    }

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            continue;
        }
        if (!has_supported_extension(fd.cFileName)) {
            continue;
        }
        if (count == cap) {
            cap *= 2;
            char **tmp = (char **)realloc(files, cap * sizeof(char *));
            if (!tmp) {
                for (size_t i = 0; i < count; i++) free(files[i]);
                free(files);
                FindClose(hFind);
                return NULL;
            }
            files = tmp;
        }
        files[count] = csir_strdup(fd.cFileName);
        if (!files[count]) {
            for (size_t i = 0; i < count; i++) free(files[i]);
            free(files);
            FindClose(hFind);
            return NULL;
        }
        count++;
    } while (FindNextFileA(hFind, &fd));

    FindClose(hFind);
#else
    DIR *dir = opendir(dir_path);
    if (!dir) {
        free(files);
        return NULL;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        if (!has_supported_extension(entry->d_name)) continue;

        char full[MAX_PATH_LEN];
        csir_join_path(full, sizeof(full), dir_path, entry->d_name);
        struct stat st;
        if (stat(full, &st) != 0 || !S_ISREG(st.st_mode)) continue;

        if (count == cap) {
            cap *= 2;
            char **tmp = (char **)realloc(files, cap * sizeof(char *));
            if (!tmp) {
                for (size_t i = 0; i < count; i++) free(files[i]);
                free(files);
                closedir(dir);
                return NULL;
            }
            files = tmp;
        }
        files[count] = csir_strdup(entry->d_name);
        if (!files[count]) {
            for (size_t i = 0; i < count; i++) free(files[i]);
            free(files);
            closedir(dir);
            return NULL;
        }
        count++;
    }
    closedir(dir);
#endif

    if (count > 1) {
        qsort(files, count, sizeof(char *), cmp_str_ptr);
    }

    if (out_count) *out_count = count;
    return files;
}
