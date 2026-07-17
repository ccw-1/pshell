#include "pshell/string_utils.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdio.h>

char* str_trim(char *str) {
    if (!str) return NULL;
    str = str_ltrim(str);
    return str_rtrim(str);
}

char* str_ltrim(char *str) {
    if (!str) return NULL;
    
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    return str;
}

char* str_rtrim(char *str) {
    if (!str) return NULL;
    
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';
    return str;
}

size_t str_copy(char *dst, const char *src, size_t size) {
    if (!dst || !src || size == 0) return 0;
    
    size_t i;
    for (i = 0; i < size - 1 && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    dst[i] = '\0';
    return i;
}

bool str_starts_with(const char *str, const char *prefix) {
    if (!str || !prefix) return false;
    
    size_t str_len = strlen(str);
    size_t prefix_len = strlen(prefix);
    
    if (prefix_len > str_len) return false;
    
    return strncmp(str, prefix, prefix_len) == 0;
}

bool str_ends_with(const char *str, const char *suffix) {
    if (!str || !suffix) return false;
    
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    
    if (suffix_len > str_len) return false;
    
    return strcmp(str + str_len - suffix_len, suffix) == 0;
}

int str_split(char *str, char delim, char **tokens, int max_tokens) {
    if (!str || !tokens || max_tokens <= 0) return 0;
    
    int count = 0;
    char *start = str;
    char *p = str;
    
    while (*p && count < max_tokens) {
        if (*p == delim) {
            *p = '\0';
            tokens[count++] = start;
            start = p + 1;
        }
        p++;
    }
    
    // Add last token if there's room
    if (count < max_tokens && *start) {
        tokens[count++] = start;
    }
    
    return count;
}

size_t str_join(const char **tokens, int count, const char *delim,
                char *buf, size_t size) {
    if (!tokens || !buf || size == 0 || count <= 0) return 0;
    
    size_t written = 0;
    size_t delim_len = delim ? strlen(delim) : 0;
    
    for (int i = 0; i < count && written < size - 1; i++) {
        if (!tokens[i]) continue;
        
        // Add delimiter before all but first token
        if (i > 0 && delim && written < size - 1) {
            size_t to_copy = delim_len;
            if (written + to_copy >= size) {
                to_copy = size - written - 1;
            }
            memcpy(buf + written, delim, to_copy);
            written += to_copy;
        }
        
        // Add token
        size_t token_len = strlen(tokens[i]);
        size_t to_copy = token_len;
        if (written + to_copy >= size) {
            to_copy = size - written - 1;
        }
        memcpy(buf + written, tokens[i], to_copy);
        written += to_copy;
    }
    
    buf[written] = '\0';
    return written;
}

int str_casecmp(const char *s1, const char *s2) {
    if (!s1 || !s2) return s1 ? 1 : (s2 ? -1 : 0);
    
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

int str_ncasecmp(const char *s1, const char *s2, size_t n) {
    if (!s1 || !s2 || n == 0) return s1 ? 1 : (s2 ? -1 : 0);
    
    for (size_t i = 0; i < n && *s1 && *s2; i++, s1++, s2++) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
    }
    return 0;
}

char* str_dup(const char *str) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    char *dup = malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, str, len + 1);
    return dup;
}

char* str_ndup(const char *str, size_t n) {
    if (!str) return NULL;
    
    size_t len = strlen(str);
    if (n < len) len = n;
    
    char *dup = malloc(len + 1);
    if (!dup) return NULL;
    
    memcpy(dup, str, len);
    dup[len] = '\0';
    return dup;
}

int str_replace_char(char *str, char old_char, char new_char) {
    if (!str) return 0;
    
    int count = 0;
    while (*str) {
        if (*str == old_char) {
            *str = new_char;
            count++;
        }
        str++;
    }
    return count;
}

char* str_tolower(char *str) {
    if (!str) return NULL;
    
    char *p = str;
    while (*p) {
        *p = tolower((unsigned char)*p);
        p++;
    }
    return str;
}

char* str_toupper(char *str) {
    if (!str) return NULL;
    
    char *p = str;
    while (*p) {
        *p = toupper((unsigned char)*p);
        p++;
    }
    return str;
}

bool str_is_blank(const char *str) {
    if (!str) return true;
    
    while (*str) {
        if (!isspace((unsigned char)*str)) {
            return false;
        }
        str++;
    }
    return true;
}

int str_format(char *buf, size_t size, const char *fmt, ...) {
    if (!buf || !fmt || size == 0) return 0;
    
    va_list args;
    va_start(args, fmt);
    int written = vsnprintf(buf, size, fmt, args);
    va_end(args);
    
    // Ensure null termination
    if (written >= (int)size) {
        buf[size - 1] = '\0';
        return size - 1;
    }
    
    return written > 0 ? written : 0;
}
