#include "pshell/buffer.h"
#include <stdlib.h>
#include <string.h>

buffer_t* buffer_create(size_t size) {
    if (size == 0) return NULL;
    
    buffer_t *buf = malloc(sizeof(buffer_t));
    if (!buf) return NULL;
    
    buf->data = malloc(size);
    if (!buf->data) {
        free(buf);
        return NULL;
    }
    
    buf->size = size;
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
    buf->overflow = false;
    
    return buf;
}

void buffer_destroy(buffer_t *buf) {
    if (!buf) return;
    
    if (buf->data) {
        free(buf->data);
    }
    free(buf);
}

size_t buffer_write(buffer_t *buf, const void *data, size_t len) {
    if (!buf || !data || len == 0) return 0;
    
    const unsigned char *src = data;
    size_t written = 0;
    size_t available = buf->size - buf->count;
    
    // Check for overflow
    if (len > available) {
        buf->overflow = true;
        len = available;  // Write what we can
    }
    
    while (written < len) {
        buf->data[buf->head] = src[written];
        buf->head = (buf->head + 1) % buf->size;
        buf->count++;
        written++;
    }
    
    return written;
}

size_t buffer_read(buffer_t *buf, void *data, size_t len) {
    if (!buf || !data || len == 0) return 0;
    
    unsigned char *dst = data;
    size_t read = 0;
    
    while (read < len && buf->count > 0) {
        dst[read] = buf->data[buf->tail];
        buf->tail = (buf->tail + 1) % buf->size;
        buf->count--;
        read++;
    }
    
    return read;
}

size_t buffer_peek(buffer_t *buf, void *data, size_t len) {
    if (!buf || !data || len == 0) return 0;
    
    unsigned char *dst = data;
    size_t peeked = 0;
    size_t pos = buf->tail;
    
    while (peeked < len && peeked < buf->count) {
        dst[peeked] = buf->data[pos];
        pos = (pos + 1) % buf->size;
        peeked++;
    }
    
    return peeked;
}

size_t buffer_available(const buffer_t *buf) {
    return buf ? buf->count : 0;
}

size_t buffer_free(const buffer_t *buf) {
    return buf ? (buf->size - buf->count) : 0;
}

bool buffer_is_empty(const buffer_t *buf) {
    return buf ? (buf->count == 0) : true;
}

bool buffer_is_full(const buffer_t *buf) {
    return buf ? (buf->count == buf->size) : false;
}

void buffer_clear(buffer_t *buf) {
    if (!buf) return;
    
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
    buf->overflow = false;
}

size_t buffer_size(const buffer_t *buf) {
    return buf ? buf->size : 0;
}

bool buffer_has_overflowed(const buffer_t *buf) {
    return buf ? buf->overflow : false;
}

void buffer_clear_overflow(buffer_t *buf) {
    if (buf) {
        buf->overflow = false;
    }
}

bool buffer_write_byte(buffer_t *buf, unsigned char byte) {
    if (!buf || buf->count >= buf->size) {
        if (buf) buf->overflow = true;
        return false;
    }
    
    buf->data[buf->head] = byte;
    buf->head = (buf->head + 1) % buf->size;
    buf->count++;
    
    return true;
}

bool buffer_read_byte(buffer_t *buf, unsigned char *byte) {
    if (!buf || !byte || buf->count == 0) {
        return false;
    }
    
    *byte = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % buf->size;
    buf->count--;
    
    return true;
}

ssize_t buffer_find_byte(const buffer_t *buf, unsigned char byte, size_t offset) {
    if (!buf || offset >= buf->count) return -1;
    
    size_t pos = (buf->tail + offset) % buf->size;
    
    for (size_t i = offset; i < buf->count; i++) {
        if (buf->data[pos] == byte) {
            return (ssize_t)i;
        }
        pos = (pos + 1) % buf->size;
    }
    
    return -1;
}

ssize_t buffer_find_pattern(const buffer_t *buf, const void *pattern,
                            size_t pattern_len, size_t offset) {
    if (!buf || !pattern || pattern_len == 0 || 
        offset >= buf->count || pattern_len > buf->count - offset) {
        return -1;
    }
    
    const unsigned char *pat = pattern;
    
    for (size_t i = offset; i <= buf->count - pattern_len; i++) {
        size_t pos = (buf->tail + i) % buf->size;
        bool match = true;
        
        for (size_t j = 0; j < pattern_len; j++) {
            if (buf->data[(pos + j) % buf->size] != pat[j]) {
                match = false;
                break;
            }
        }
        
        if (match) {
            return (ssize_t)i;
        }
    }
    
    return -1;
}
