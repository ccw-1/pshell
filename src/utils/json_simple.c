#include "pshell/json_simple.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <math.h>

/* Internal structures */
typedef struct json_pair {
    char *key;
    json_value_t *value;
    struct json_pair *next;
} json_pair_t;

struct json_object {
    json_pair_t *pairs;
    size_t count;
};

typedef struct json_element {
    json_value_t *value;
    struct json_element *next;
} json_element_t;

struct json_array {
    json_element_t *elements;
    size_t count;
};

/* Parser state */
typedef struct {
    const char *json;
    size_t pos;
    size_t len;
    int depth;
} parser_t;

/* Forward declarations */
static json_value_t* parse_value(parser_t *p);
static void skip_whitespace(parser_t *p);

/* Helper functions */
static void skip_whitespace(parser_t *p) {
    while (p->pos < p->len && isspace((unsigned char)p->json[p->pos])) {
        p->pos++;
    }
}

static bool expect_char(parser_t *p, char c) {
    skip_whitespace(p);
    if (p->pos >= p->len || p->json[p->pos] != c) {
        return false;
    }
    p->pos++;
    return true;
}

static char* parse_string_value(parser_t *p) {
    if (!expect_char(p, '"')) return NULL;
    
    size_t start = p->pos;
    size_t end = start;
    
    // Find end quote and calculate actual string length
    while (end < p->len && p->json[end] != '"') {
        if (p->json[end] == '\\') {
            end++; // Skip escape char
            if (end >= p->len) return NULL;
        }
        end++;
    }
    
    if (end >= p->len || p->json[end] != '"') return NULL;
    
    size_t raw_len = end - start;
    char *str = malloc(raw_len + 1);
    if (!str) return NULL;
    
    // Copy and unescape
    size_t j = 0;
    for (size_t i = start; i < end; i++) {
        if (p->json[i] == '\\' && i + 1 < end) {
            i++;
            switch (p->json[i]) {
                case 'n': str[j++] = '\n'; break;
                case 't': str[j++] = '\t'; break;
                case 'r': str[j++] = '\r'; break;
                case 'b': str[j++] = '\b'; break;
                case 'f': str[j++] = '\f'; break;
                case '\\': str[j++] = '\\'; break;
                case '"': str[j++] = '"'; break;
                case 'u': {
                    /* Decode \uXXXX (4 hex digits) */
                    if (i + 4 < end) {
                        unsigned int codepoint = 0;
                        for (int k = 0; k < 4; k++) {
                            char c = p->json[i + 1 + k];
                            codepoint <<= 4;
                            if (c >= '0' && c <= '9') codepoint |= (unsigned int)(c - '0');
                            else if (c >= 'a' && c <= 'f') codepoint |= (unsigned int)(c - 'a' + 10);
                            else if (c >= 'A' && c <= 'F') codepoint |= (unsigned int)(c - 'A' + 10);
                            else { /* Invalid hex - output as-is */ str[j++] = p->json[i + k]; continue; }
                        }
                        i += 4;
                        /* Encode as UTF-8 */
                        if (codepoint < 0x80) {
                            str[j++] = (char)codepoint;
                        } else if (codepoint < 0x800) {
                            str[j++] = (char)(0xC0 | (codepoint >> 6));
                            str[j++] = (char)(0x80 | (codepoint & 0x3F));
                        } else {
                            str[j++] = (char)(0xE0 | (codepoint >> 12));
                            str[j++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
                            str[j++] = (char)(0x80 | (codepoint & 0x3F));
                        }
                    } else {
                        str[j++] = p->json[i];
                    }
                    break;
                }
                default: str[j++] = p->json[i]; break;
            }
        } else {
            str[j++] = p->json[i];
        }
    }
    str[j] = '\0';
    
    p->pos = end + 1; // Move past closing quote
    
    return str;
}

static json_value_t* parse_number(parser_t *p) {
    size_t start = p->pos;

    if (p->json[p->pos] == '-') p->pos++;

    size_t digits_start = p->pos;
    while (p->pos < p->len && isdigit((unsigned char)p->json[p->pos])) {
        p->pos++;
    }

    /* Must have at least one digit */
    if (p->pos == digits_start) return NULL;
    
    if (p->pos < p->len && p->json[p->pos] == '.') {
        p->pos++;
        while (p->pos < p->len && isdigit((unsigned char)p->json[p->pos])) {
            p->pos++;
        }
    }
    
    if (p->pos < p->len && (p->json[p->pos] == 'e' || p->json[p->pos] == 'E')) {
        p->pos++;
        if (p->pos < p->len && (p->json[p->pos] == '+' || p->json[p->pos] == '-')) {
            p->pos++;
        }
        while (p->pos < p->len && isdigit((unsigned char)p->json[p->pos])) {
            p->pos++;
        }
    }
    
    char *endptr;
    double num = strtod(p->json + start, &endptr);
    
    return json_number_create(num);
}

static json_value_t* parse_object(parser_t *p) {
    if (!expect_char(p, '{')) return NULL;

    p->depth++;
    if (p->depth > 200) { p->depth--; return NULL; }

    json_value_t *obj = json_object_create();
    if (!obj) { p->depth--; return NULL; }

    skip_whitespace(p);
    if (p->pos < p->len && p->json[p->pos] == '}') {
        p->pos++;
        p->depth--;
        return obj;
    }

    while (p->pos < p->len) {
        char *key = parse_string_value(p);
        if (!key) {
            json_free(obj);
            p->depth--;
            return NULL;
        }

        if (!expect_char(p, ':')) {
            free(key);
            json_free(obj);
            p->depth--;
            return NULL;
        }

        json_value_t *val = parse_value(p);
        if (!val) {
            free(key);
            json_free(obj);
            p->depth--;
            return NULL;
        }

        json_object_set(obj, key, val);
        free(key);

        skip_whitespace(p);
        if (p->pos < p->len && p->json[p->pos] == ',') {
            p->pos++;
            continue;
        }
        break;
    }

    if (!expect_char(p, '}')) {
        p->depth--;
        json_free(obj);
        return NULL;
    }

    p->depth--;
    return obj;
}

static json_value_t* parse_array(parser_t *p) {
    if (!expect_char(p, '[')) return NULL;

    p->depth++;
    if (p->depth > 200) { p->depth--; return NULL; }

    json_value_t *arr = json_array_create();
    if (!arr) { p->depth--; return NULL; }

    skip_whitespace(p);
    if (p->pos < p->len && p->json[p->pos] == ']') {
        p->pos++;
        p->depth--;
        return arr;
    }

    while (p->pos < p->len) {
        json_value_t *val = parse_value(p);
        if (!val) {
            json_free(arr);
            p->depth--;
            return NULL;
        }

        json_array_add(arr, val);

        skip_whitespace(p);
        if (p->pos < p->len && p->json[p->pos] == ',') {
            p->pos++;
            continue;
        }
        break;
    }

    if (!expect_char(p, ']')) {
        p->depth--;
        json_free(arr);
        return NULL;
    }

    p->depth--;
    return arr;
}

static json_value_t* parse_value(parser_t *p) {
    skip_whitespace(p);

    if (p->pos >= p->len) return NULL;

    /* Limit nesting depth to prevent stack overflow */
    if (p->depth > 200) return NULL;
    
    char c = p->json[p->pos];
    
    if (c == '"') {
        char *str = parse_string_value(p);
        if (!str) return NULL;
        /* json_string_create() strdups its argument; free the parsed copy so
         * each parsed string does not leak one heap block. */
        json_value_t *val = json_string_create(str);
        free(str);
        return val;
    } else if (c == '{') {
        return parse_object(p);
    } else if (c == '[') {
        return parse_array(p);
    } else if (c == '-' || isdigit((unsigned char)c)) {
        return parse_number(p);
    } else if (strncmp(p->json + p->pos, "true", 4) == 0) {
        p->pos += 4;
        return json_bool_create(true);
    } else if (strncmp(p->json + p->pos, "false", 5) == 0) {
        p->pos += 5;
        return json_bool_create(false);
    } else if (strncmp(p->json + p->pos, "null", 4) == 0) {
        p->pos += 4;
        return json_null_create();
    }
    
    return NULL;
}

json_value_t* json_parse(const char *json) {
    if (!json) return NULL;

    parser_t p = {
        .json = json,
        .pos = 0,
        .len = strlen(json),
        .depth = 0
    };

    json_value_t *result = parse_value(&p);
    if (!result) return NULL;

    /* Reject trailing garbage */
    skip_whitespace(&p);
    if (p.pos != p.len) {
        json_free(result);
        return NULL;
    }

    return result;
}

void json_free(json_value_t *val) {
    if (!val) return;
    
    switch (val->type) {
        case JSON_STRING:
            free(val->value.string_val);
            break;
        case JSON_ARRAY: {
            json_element_t *elem = val->value.array_val->elements;
            while (elem) {
                json_element_t *next = elem->next;
                json_free(elem->value);
                free(elem);
                elem = next;
            }
            free(val->value.array_val);
            break;
        }
        case JSON_OBJECT: {
            json_pair_t *pair = val->value.object_val->pairs;
            while (pair) {
                json_pair_t *next = pair->next;
                free(pair->key);
                json_free(pair->value);
                free(pair);
                pair = next;
            }
            free(val->value.object_val);
            break;
        }
        default:
            break;
    }
    
    free(val);
}

json_value_t* json_object_create(void) {
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_OBJECT;
    val->value.object_val = malloc(sizeof(json_object_t));
    if (!val->value.object_val) {
        free(val);
        return NULL;
    }
    
    val->value.object_val->pairs = NULL;
    val->value.object_val->count = 0;
    
    return val;
}

json_value_t* json_array_create(void) {
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_ARRAY;
    val->value.array_val = malloc(sizeof(json_array_t));
    if (!val->value.array_val) {
        free(val);
        return NULL;
    }
    
    val->value.array_val->elements = NULL;
    val->value.array_val->count = 0;
    
    return val;
}

json_value_t* json_string_create(const char *str) {
    if (!str) return NULL;
    
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_STRING;
    val->value.string_val = strdup(str);
    if (!val->value.string_val) {
        free(val);
        return NULL;
    }
    
    return val;
}

json_value_t* json_number_create(double num) {
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_NUMBER;
    val->value.number_val = num;
    
    return val;
}

json_value_t* json_bool_create(bool b) {
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_BOOL;
    val->value.bool_val = b;
    
    return val;
}

json_value_t* json_null_create(void) {
    json_value_t *val = malloc(sizeof(json_value_t));
    if (!val) return NULL;
    
    val->type = JSON_NULL;
    
    return val;
}

bool json_object_set(json_value_t *obj, const char *key, json_value_t *val) {
    if (!obj || obj->type != JSON_OBJECT || !key || !val) return false;
    
    json_pair_t *pair = malloc(sizeof(json_pair_t));
    if (!pair) return false;
    
    pair->key = strdup(key);
    if (!pair->key) {
        free(pair);
        return false;
    }
    
    pair->value = val;
    pair->next = obj->value.object_val->pairs;
    obj->value.object_val->pairs = pair;
    obj->value.object_val->count++;
    
    return true;
}

json_value_t* json_object_get(const json_value_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    
    json_pair_t *pair = obj->value.object_val->pairs;
    while (pair) {
        if (strcmp(pair->key, key) == 0) {
            return pair->value;
        }
        pair = pair->next;
    }
    
    return NULL;
}

bool json_object_has(const json_value_t *obj, const char *key) {
    return json_object_get(obj, key) != NULL;
}

size_t json_object_size(const json_value_t *obj) {
    if (!obj || obj->type != JSON_OBJECT) return 0;
    return obj->value.object_val->count;
}

bool json_array_add(json_value_t *arr, json_value_t *val) {
    if (!arr || arr->type != JSON_ARRAY || !val) return false;
    
    json_element_t *elem = malloc(sizeof(json_element_t));
    if (!elem) return false;
    
    elem->value = val;
    elem->next = NULL;
    
    if (!arr->value.array_val->elements) {
        arr->value.array_val->elements = elem;
    } else {
        json_element_t *last = arr->value.array_val->elements;
        while (last->next) last = last->next;
        last->next = elem;
    }
    
    arr->value.array_val->count++;
    return true;
}

json_value_t* json_array_get(const json_value_t *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY) return NULL;
    
    json_element_t *elem = arr->value.array_val->elements;
    for (size_t i = 0; i < index && elem; i++) {
        elem = elem->next;
    }
    
    return elem ? elem->value : NULL;
}

size_t json_array_size(const json_value_t *arr) {
    if (!arr || arr->type != JSON_ARRAY) return 0;
    return arr->value.array_val->count;
}

static size_t stringify_internal(const json_value_t *val, char *buf, size_t size, size_t pos) {
    if (!val || pos >= size - 1) return pos;
    
    switch (val->type) {
        case JSON_NULL:
            pos += snprintf(buf + pos, size - pos, "null");
            break;
        case JSON_BOOL:
            pos += snprintf(buf + pos, size - pos, val->value.bool_val ? "true" : "false");
            break;
        case JSON_NUMBER:
            pos += snprintf(buf + pos, size - pos, "%.17g", val->value.number_val);
            break;
        case JSON_STRING: {
            pos += snprintf(buf + pos, size - pos, "\"");
            if (val->value.string_val) {
                for (const char *p = val->value.string_val; *p && pos < size - 8; p++) {
                    char c = *p;
                    switch (c) {
                        case '"':  buf[pos++] = '\\'; buf[pos++] = '"';  break;
                        case '\\': buf[pos++] = '\\'; buf[pos++] = '\\'; break;
                        case '\n': buf[pos++] = '\\'; buf[pos++] = 'n';  break;
                        case '\r': buf[pos++] = '\\'; buf[pos++] = 'r';  break;
                        case '\t': buf[pos++] = '\\'; buf[pos++] = 't';  break;
                        default:
                            if ((unsigned char)c < 0x20) {
                                int n = snprintf(buf + pos, size - pos, "\\u%04x", (unsigned char)c);
                                if (n < 0 || (size_t)n >= size - pos) {
                                    pos = size - 1;
                                    buf[pos] = '\0';
                                    return pos;
                                }
                                pos += (size_t)n;
                            } else {
                                buf[pos++] = c;
                            }
                            break;
                    }
                }
            }
            pos += snprintf(buf + pos, size - pos, "\"");
            break;
        }
        case JSON_ARRAY: {
            pos += snprintf(buf + pos, size - pos, "[");
            json_element_t *elem = val->value.array_val->elements;
            bool first = true;
            while (elem && pos < size - 1) {
                if (!first) pos += snprintf(buf + pos, size - pos, ",");
                pos = stringify_internal(elem->value, buf, size, pos);
                elem = elem->next;
                first = false;
            }
            pos += snprintf(buf + pos, size - pos, "]");
            break;
        }
        case JSON_OBJECT: {
            pos += snprintf(buf + pos, size - pos, "{");
            json_pair_t *pair = val->value.object_val->pairs;
            bool first = true;
            while (pair && pos < size - 1) {
                if (!first) pos += snprintf(buf + pos, size - pos, ",");
                pos += snprintf(buf + pos, size - pos, "\"%s\":", pair->key);
                pos = stringify_internal(pair->value, buf, size, pos);
                pair = pair->next;
                first = false;
            }
            pos += snprintf(buf + pos, size - pos, "}");
            break;
        }
    }
    
    return pos;
}

size_t json_stringify(const json_value_t *val, char *buf, size_t size) {
    if (!val || !buf || size == 0) return 0;
    
    size_t pos = stringify_internal(val, buf, size, 0);
    if (pos >= size) pos = size - 1;
    buf[pos] = '\0';
    
    return pos;
}

size_t json_stringify_pretty(const json_value_t *val, char *buf, size_t size,
                             const char *indent) {
    // Simple implementation - just use regular stringify for now
    return json_stringify(val, buf, size);
}

const char* json_get_string(const json_value_t *val) {
    if (!val || val->type != JSON_STRING) return NULL;
    return val->value.string_val;
}

bool json_get_number(const json_value_t *val, double *out) {
    if (!val || val->type != JSON_NUMBER || !out) return false;
    *out = val->value.number_val;
    return true;
}

bool json_get_bool(const json_value_t *val, bool *out) {
    if (!val || val->type != JSON_BOOL || !out) return false;
    *out = val->value.bool_val;
    return true;
}

bool json_is_null(const json_value_t *val) {
    return val && val->type == JSON_NULL;
}

json_type_t json_get_type(const json_value_t *val) {
    return val ? val->type : JSON_NULL;
}

/* Convenience functions for object access */

const char* json_object_get_string(const json_value_t *obj, const char *key) {
    json_value_t *val = json_object_get(obj, key);
    return val ? json_get_string(val) : NULL;
}

int json_object_get_int(const json_value_t *obj, const char *key, int default_val) {
    json_value_t *val = json_object_get(obj, key);
    if (!val) return default_val;
    
    double num;
    if (json_get_number(val, &num)) {
        return (int)num;
    }
    return default_val;
}

double json_object_get_double(const json_value_t *obj, const char *key, double default_val) {
    json_value_t *val = json_object_get(obj, key);
    if (!val) return default_val;
    
    double num;
    if (json_get_number(val, &num)) {
        return num;
    }
    return default_val;
}

bool json_object_get_bool(const json_value_t *obj, const char *key, bool default_val) {
    json_value_t *val = json_object_get(obj, key);
    if (!val) return default_val;
    
    bool b;
    if (json_get_bool(val, &b)) {
        return b;
    }
    return default_val;
}
