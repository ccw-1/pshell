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

size_t str_json_escape(char *dst, size_t dst_cap, const char *src) {
    if (!dst || dst_cap == 0) return 0;
    if (!src) src = "";
    size_t pos = 0;
    for (const unsigned char *c = (const unsigned char *)src; *c; c++) {
        char esc[8];
        size_t n;
        switch (*c) {
            case '"':  memcpy(esc, "\\\"", 2); n = 2; break;
            case '\\': memcpy(esc, "\\\\", 2); n = 2; break;
            case '\n': memcpy(esc, "\\n", 2);  n = 2; break;
            case '\r': memcpy(esc, "\\r", 2);  n = 2; break;
            case '\t': memcpy(esc, "\\t", 2);  n = 2; break;
            default:
                if (*c < 0x20) {
                    n = (size_t)snprintf(esc, sizeof(esc), "\\u%04x", *c);
                } else {
                    esc[0] = (char)*c;
                    n = 1;
                }
        }
        if (pos + n > dst_cap - 1) break;
        memcpy(dst + pos, esc, n);
        pos += n;
    }
    dst[pos] = '\0';
    return pos;
}

/* Bounded single-byte write for str_unescape: never writes past dst_end
 * (the byte reserved for the terminating NUL). */
#define PUTC(c) do { if (dst < dst_end) { *dst++ = (char)(c); } } while (0)

size_t str_unescape(char *dst, size_t dst_cap, const char *src) {
    if (!dst || dst_cap == 0) return 0;
    if (!src) { dst[0] = '\0'; return 0; }
    
    char *dst_start = dst;
    /* Reserve one byte for the terminating NUL; PUTC never writes past it. */
    char *dst_end = dst + dst_cap - 1;
    
    while (*src) {
        if (*src == '\\' && *(src + 1)) {
            src++;  /* Skip backslash */
            
            switch (*src) {
                case 'n':  PUTC('\n'); break;
                case 'r':  PUTC('\r'); break;
                case 't':  PUTC('\t'); break;
                case '\\': PUTC('\\'); break;
                case '0':  PUTC('\0'); break;
                case 'a':  PUTC('\a'); break;
                case 'b':  PUTC('\b'); break;
                case 'f':  PUTC('\f'); break;
                case 'v':  PUTC('\v'); break;
                case 'e':  PUTC('\033'); break;  /* ESC */
                
                /* Function keys: \F1 through \F12 */
                case 'F':
                    src++;
                    const char *save = src;  /* Save position for error recovery */
                    if (*src >= '1' && *src <= '9') {
                        int fkey = *src - '0';
                        src++;
                        if (*src >= '0' && *src <= '9') {
                            fkey = fkey * 10 + (*src - '0');
                        } else {
                            src--;  /* Only one digit */
                        }
                        
                        if (fkey >= 1 && fkey <= 12) {
                            /* Send xterm-style function key sequences */
                            if (fkey <= 4) {
                                /* F1-F4: ESC O P/Q/R/S */
                                PUTC('\033');
                                PUTC('O');
                                PUTC('P' + (fkey - 1));
                            } else {
                                /* F5-F12: ESC [ 15~24 ~ */
                                const char *seq[] = {
                                    "[15~", "[17~", "[18~", "[19~",  /* F5-F8 */
                                    "[20~", "[21~", "[23~", "[24~"   /* F9-F12 */
                                };
                                PUTC('\033');
                                const char *s = seq[fkey - 5];
                                while (*s) { PUTC(*s); s++; }
                            }
                        } else {
                            /* Invalid function key number - restore to first digit */
                            PUTC('F');
                            src = save;
                        }
                    } else {
                        /* Not a function key, copy literal */
                        PUTC('F');
                        src--;
                    }
                    break;
                
                /* Special keys: \UP \DOWN \LEFT \RIGHT \HOME \END \INS \DEL \PGUP \PGDN */
                /* Shift modifier: \SHIFT_UP \SHIFT_F1 \SHIFT_TAB etc. */
                case 'S':  /* SHIFT_* keys */
                    if (src[1] == 'H' && src[2] == 'I' && src[3] == 'F' && src[4] == 'T' && src[5] == '_') {
                        src += 5;  /* Skip "HIFT_" */
                        
                        /* Shift+Arrow keys */
                        if (src[1] == 'U' && src[2] == 'P' && (src[3] == '\0' || src[3] == '\\')) {
                            PUTC('\033'); PUTC('['); PUTC('1'); PUTC(';'); PUTC('2'); PUTC('A');
                            src += 2;
                        } else if (src[1] == 'D' && src[2] == 'O' && src[3] == 'W' && src[4] == 'N' && (src[5] == '\0' || src[5] == '\\')) {
                            PUTC('\033'); PUTC('['); PUTC('1'); PUTC(';'); PUTC('2'); PUTC('B');
                            src += 4;
                        } else if (src[1] == 'L' && src[2] == 'E' && src[3] == 'F' && src[4] == 'T' && (src[5] == '\0' || src[5] == '\\')) {
                            PUTC('\033'); PUTC('['); PUTC('1'); PUTC(';'); PUTC('2'); PUTC('D');
                            src += 4;
                        } else if (src[1] == 'R' && src[2] == 'I' && src[3] == 'G' && src[4] == 'H' && src[5] == 'T' && (src[6] == '\0' || src[6] == '\\')) {
                            PUTC('\033'); PUTC('['); PUTC('1'); PUTC(';'); PUTC('2'); PUTC('C');
                            src += 5;
                        }
                        /* Shift+Tab (backtab) */
                        else if (src[1] == 'T' && src[2] == 'A' && src[3] == 'B' && (src[4] == '\0' || src[4] == '\\')) {
                            PUTC('\033'); PUTC('['); PUTC('Z');
                            src += 3;
                        }
                        /* Shift+Function keys (F13-F24) */
                        else if (src[1] == 'F' && src[2] >= '1' && src[2] <= '9') {
                            src++;  /* Skip 'F' */
                            int fkey = *src - '0';
                            src++;
                            if (*src >= '0' && *src <= '9') {
                                fkey = fkey * 10 + (*src - '0');
                            } else {
                                src--;
                            }
                            
                            if (fkey >= 1 && fkey <= 12) {
                                /* Shift+F1-F12 = F13-F24 */
                                const char *seq[] = {
                                    "[25~", "[26~", "[28~", "[29~",  /* F13-F16 */
                                    "[31~", "[32~", "[33~", "[34~",  /* F17-F20 */
                                    "[23;2~", "[24;2~", "[25;2~", "[26;2~"  /* F21-F24 */
                                };
                                PUTC('\033');
                                const char *s = seq[fkey - 1];
                                while (*s) { PUTC(*s); s++; }
                            } else {
                                src -= (fkey >= 10) ? 1 : 0;
                                PUTC('S'); PUTC('H'); PUTC('I'); PUTC('F'); PUTC('T'); PUTC('_'); PUTC('F');
                            }
                        } else {
                            /* Unknown SHIFT_ combination */
                            PUTC('S'); PUTC('H'); PUTC('I'); PUTC('F'); PUTC('T'); PUTC('_');
                            src--;
                        }
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'U':  /* UP */
                    if (src[1] == 'P' && (src[2] == '\0' || src[2] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('A');
                        src++;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'D':  /* DOWN or DEL */
                    if (src[1] == 'O' && src[2] == 'W' && src[3] == 'N' && (src[4] == '\0' || src[4] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('B');
                        src += 3;
                    } else if (src[1] == 'E' && src[2] == 'L' && (src[3] == '\0' || src[3] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('3'); PUTC('~');
                        src += 2;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'L':  /* LEFT */
                    if (src[1] == 'E' && src[2] == 'F' && src[3] == 'T' && (src[4] == '\0' || src[4] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('D');
                        src += 3;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'R':  /* RIGHT */
                    if (src[1] == 'I' && src[2] == 'G' && src[3] == 'H' && src[4] == 'T' && (src[5] == '\0' || src[5] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('C');
                        src += 4;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'H':  /* HOME */
                    if (src[1] == 'O' && src[2] == 'M' && src[3] == 'E' && (src[4] == '\0' || src[4] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('H');
                        src += 3;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'E':  /* END */
                    if (src[1] == 'N' && src[2] == 'D' && (src[3] == '\0' || src[3] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('F');
                        src += 2;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'I':  /* INS */
                    if (src[1] == 'N' && src[2] == 'S' && (src[3] == '\0' || src[3] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('2'); PUTC('~');
                        src += 2;
                    } else {
                        PUTC(*src);
                    }
                    break;
                case 'P':  /* PGUP or PGDN */
                    if (src[1] == 'G' && src[2] == 'U' && src[3] == 'P' && (src[4] == '\0' || src[4] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('5'); PUTC('~');
                        src += 3;
                    } else if (src[1] == 'G' && src[2] == 'D' && src[3] == 'N' && (src[4] == '\0' || src[4] == '\\')) {
                        PUTC('\033'); PUTC('['); PUTC('6'); PUTC('~');
                        src += 3;
                    } else {
                        PUTC(*src);
                    }
                    break;
                
                /* Control characters: \cX -> Ctrl-X */
                case 'c':
                case 'C':
                    src++;
                    if (*src == '\0') {
                        /* Trailing \c with no control char: back up so the
                         * loop's trailing src++ lands on the NUL and the
                         * loop terminates instead of reading past it. */
                        src--;
                    } else if (*src >= '@' && *src <= '_') {
                        PUTC(*src - '@');  /* Ctrl-A = 1, Ctrl-C = 3, etc. */
                    } else if (*src >= 'a' && *src <= 'z') {
                        PUTC(*src - 'a' + 1);  /* Ctrl-a = 1, Ctrl-c = 3, etc. */
                    } else {
                        PUTC(*src);  /* Invalid, copy as-is */
                    }
                    break;
                
                /* Hex escape: \xHH */
                case 'x':
                    src++;
                    if (isxdigit((unsigned char)*src)) {
                        int val = 0;
                        if (isdigit((unsigned char)*src)) {
                            val = *src - '0';
                        } else {
                            val = tolower((unsigned char)*src) - 'a' + 10;
                        }
                        src++;
                        if (isxdigit((unsigned char)*src)) {
                            val = val * 16;
                            if (isdigit((unsigned char)*src)) {
                                val += *src - '0';
                            } else {
                                val += tolower((unsigned char)*src) - 'a' + 10;
                            }
                        } else {
                            src--;  /* Only one hex digit */
                        }
                        PUTC((char)val);
                    } else {
                        src--;  /* No hex digits, treat as literal */
                        PUTC('x');
                    }
                    break;
                
                /* Octal escape: \NNN */
                default:
                    if (*src >= '0' && *src <= '7') {
                        int val = *src - '0';
                        src++;
                        if (*src >= '0' && *src <= '7') {
                            val = val * 8 + (*src - '0');
                            src++;
                            if (*src >= '0' && *src <= '7') {
                                val = val * 8 + (*src - '0');
                            } else {
                                src--;
                            }
                        } else {
                            src--;
                        }
                        PUTC((char)val);
                    } else {
                        /* Unknown escape, copy literal */
                        PUTC(*src);
                    }
                    break;
            }
            src++;
        } else {
            PUTC(*src);
            src++;
        }
    }
    
    *dst = '\0';
    return (size_t)(dst - dst_start);
}
