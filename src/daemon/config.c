#include "pshell/config.h"
#include "pshell/string_utils.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <ctype.h>

#define MAX_LINE_LENGTH 1024
#define MAX_ENTRIES 256

typedef struct config_entry {
    char *section;
    char *key;
    char *value;
} config_entry_t;

struct config {
    config_entry_t entries[MAX_ENTRIES];
    int count;
};

static char* trim_whitespace(char *str) {
    // Trim leading whitespace
    while (*str && isspace((unsigned char)*str)) {
        str++;
    }
    
    // Trim trailing whitespace
    char *end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        *end = '\0';
        end--;
    }
    
    return str;
}

config_t* config_load(const char *filename) {
    if (!filename) return NULL;
    
    FILE *fp = fopen(filename, "r");
    if (!fp) return NULL;
    
    config_t *cfg = calloc(1, sizeof(config_t));
    if (!cfg) {
        fclose(fp);
        return NULL;
    }
    
    char line[MAX_LINE_LENGTH];
    char current_section[256] = "";
    
    while (fgets(line, sizeof(line), fp)) {
        char *trimmed = trim_whitespace(line);
        
        // Skip empty lines and comments
        if (*trimmed == '\0' || *trimmed == '#' || *trimmed == ';') {
            continue;
        }
        
        // Check for section header [section]
        if (*trimmed == '[') {
            char *end = strchr(trimmed, ']');
            if (end) {
                *end = '\0';
                str_copy(current_section, trimmed + 1, sizeof(current_section));
                trim_whitespace(current_section);
            }
            continue;
        }
        
        // Parse key=value
        char *equals = strchr(trimmed, '=');
        if (equals && cfg->count < MAX_ENTRIES) {
            *equals = '\0';
            char *key = trim_whitespace(trimmed);
            char *value = trim_whitespace(equals + 1);
            
            // Remove quotes from value if present
            if (*value == '"' || *value == '\'') {
                char quote = *value;
                value++;
                char *end = strrchr(value, quote);
                if (end) *end = '\0';
            }
            
            // Store entry
            cfg->entries[cfg->count].section = str_dup(current_section);
            cfg->entries[cfg->count].key = str_dup(key);
            cfg->entries[cfg->count].value = str_dup(value);
            
            if (cfg->entries[cfg->count].section &&
                cfg->entries[cfg->count].key &&
                cfg->entries[cfg->count].value) {
                cfg->count++;
            } else {
                // Cleanup on allocation failure
                free(cfg->entries[cfg->count].section);
                free(cfg->entries[cfg->count].key);
                free(cfg->entries[cfg->count].value);
            }
        }
    }
    
    fclose(fp);
    return cfg;
}

void config_free(config_t *cfg) {
    if (!cfg) return;
    
    for (int i = 0; i < cfg->count; i++) {
        free(cfg->entries[i].section);
        free(cfg->entries[i].key);
        free(cfg->entries[i].value);
    }
    
    free(cfg);
}

const char* config_get_string(const config_t *cfg, const char *section,
                              const char *key, const char *default_value) {
    if (!cfg || !key) return default_value;
    
    const char *sect = section ? section : "";
    
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].section, sect) == 0 &&
            strcmp(cfg->entries[i].key, key) == 0) {
            return cfg->entries[i].value;
        }
    }
    
    return default_value;
}

int config_get_int(const config_t *cfg, const char *section,
                   const char *key, int default_value) {
    const char *value = config_get_string(cfg, section, key, NULL);
    if (!value) return default_value;
    
    char *endptr;
    /* Auto-detect base: 0 = auto (0x for hex, 0 for octal, else decimal) */
    long result = strtol(value, &endptr, 0);
    
    if (*endptr != '\0') return default_value;
    
    return (int)result;
}

bool config_get_bool(const config_t *cfg, const char *section,
                     const char *key, bool default_value) {
    const char *value = config_get_string(cfg, section, key, NULL);
    if (!value) return default_value;
    
    // Check for true values
    if (strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
        return true;
    }
    
    // Check for false values
    if (strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
        return false;
    }
    
    return default_value;
}

bool config_has_key(const config_t *cfg, const char *section, const char *key) {
    if (!cfg || !key) return false;
    
    const char *sect = section ? section : "";
    
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].section, sect) == 0 &&
            strcmp(cfg->entries[i].key, key) == 0) {
            return true;
        }
    }
    
    return false;
}

int config_get_keys(const config_t *cfg, const char *section,
                    char **keys, int max_keys) {
    if (!cfg || !keys || max_keys <= 0) return 0;
    
    const char *sect = section ? section : "";
    int count = 0;
    
    for (int i = 0; i < cfg->count && count < max_keys; i++) {
        if (strcmp(cfg->entries[i].section, sect) == 0) {
            keys[count] = str_dup(cfg->entries[i].key);
            if (keys[count]) {
                count++;
            }
        }
    }
    
    return count;
}

int config_get_sections(const config_t *cfg, char **sections, int max_sections) {
    if (!cfg || !sections || max_sections <= 0) return 0;
    
    int count = 0;
    
    for (int i = 0; i < cfg->count && count < max_sections; i++) {
        // Check if section already in list
        bool found = false;
        for (int j = 0; j < count; j++) {
            if (strcmp(sections[j], cfg->entries[i].section) == 0) {
                found = true;
                break;
            }
        }
        
        if (!found) {
            sections[count] = str_dup(cfg->entries[i].section);
            if (sections[count]) {
                count++;
            }
        }
    }
    
    return count;
}
