#include "pshell/uuid_simple.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <stdbool.h>

bool uuid_generate(char *buf) {
    if (!buf) return false;
    
    unsigned char bytes[16];
    
    // Read random bytes from /dev/urandom
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd < 0) return false;
    
    ssize_t n = read(fd, bytes, sizeof(bytes));
    close(fd);
    
    if (n != sizeof(bytes)) return false;
    
    // Set version (4) and variant bits
    bytes[6] = (bytes[6] & 0x0F) | 0x40;  // Version 4
    bytes[8] = (bytes[8] & 0x3F) | 0x80;  // Variant 10
    
    // Format as UUID string: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    snprintf(buf, UUID_STRING_LENGTH,
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             bytes[0], bytes[1], bytes[2], bytes[3],
             bytes[4], bytes[5],
             bytes[6], bytes[7],
             bytes[8], bytes[9],
             bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    
    return true;
}

bool uuid_is_valid(const char *uuid) {
    if (!uuid) return false;
    
    // Check length
    if (strlen(uuid) != 36) return false;
    
    // Check format: xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx
    for (int i = 0; i < 36; i++) {
        if (i == 8 || i == 13 || i == 18 || i == 23) {
            if (uuid[i] != '-') return false;
        } else {
            if (!isxdigit((unsigned char)uuid[i])) return false;
        }
    }
    
    // Check version (should be 4)
    if (uuid[14] != '4') return false;
    
    // Check variant (should be 8, 9, a, or b)
    char variant = tolower((unsigned char)uuid[19]);
    if (variant != '8' && variant != '9' && variant != 'a' && variant != 'b') {
        return false;
    }
    
    return true;
}
