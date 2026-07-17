#ifndef PSHELL_CONFIG_H
#define PSHELL_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

/**
 * Simple INI-style configuration parser for pshell
 * Format: [section]\nkey=value
 * No external dependencies
 */

typedef struct config config_t;

/**
 * Load configuration from file
 * @param filename Path to config file
 * @return Configuration object or NULL on error
 */
config_t* config_load(const char *filename);

/**
 * Free configuration
 * @param cfg Configuration to free
 */
void config_free(config_t *cfg);

/**
 * Get string value
 * @param cfg Configuration
 * @param section Section name
 * @param key Key name
 * @param default_value Default value if not found
 * @return Value or default_value
 */
const char* config_get_string(const config_t *cfg, const char *section,
                              const char *key, const char *default_value);

/**
 * Get integer value
 * @param cfg Configuration
 * @param section Section name
 * @param key Key name
 * @param default_value Default value if not found
 * @return Value or default_value
 */
int config_get_int(const config_t *cfg, const char *section,
                   const char *key, int default_value);

/**
 * Get boolean value
 * @param cfg Configuration
 * @param section Section name
 * @param key Key name
 * @param default_value Default value if not found
 * @return Value or default_value
 */
bool config_get_bool(const config_t *cfg, const char *section,
                     const char *key, bool default_value);

/**
 * Check if key exists
 * @param cfg Configuration
 * @param section Section name
 * @param key Key name
 * @return true if key exists
 */
bool config_has_key(const config_t *cfg, const char *section, const char *key);

/**
 * Get all keys in a section
 * @param cfg Configuration
 * @param section Section name
 * @param keys Output array of key names (caller must free)
 * @param max_keys Maximum number of keys to return
 * @return Number of keys found
 */
int config_get_keys(const config_t *cfg, const char *section,
                    char **keys, int max_keys);

/**
 * Get all sections
 * @param cfg Configuration
 * @param sections Output array of section names (caller must free)
 * @param max_sections Maximum number of sections to return
 * @return Number of sections found
 */
int config_get_sections(const config_t *cfg, char **sections, int max_sections);

#endif /* PSHELL_CONFIG_H */
