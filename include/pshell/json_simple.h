#ifndef PSHELL_JSON_SIMPLE_H
#define PSHELL_JSON_SIMPLE_H

#include <stddef.h>
#include <stdbool.h>

/**
 * Simple JSON parser/builder for pshell
 * No external dependencies, minimal but sufficient for our needs
 * Supports: objects, arrays, strings, numbers, booleans, null
 */

typedef enum {
    JSON_NULL,
    JSON_BOOL,
    JSON_NUMBER,
    JSON_STRING,
    JSON_ARRAY,
    JSON_OBJECT
} json_type_t;

typedef struct json_value json_value_t;
typedef struct json_object json_object_t;
typedef struct json_array json_array_t;

struct json_value {
    json_type_t type;
    union {
        bool bool_val;
        double number_val;
        char *string_val;
        json_array_t *array_val;
        json_object_t *object_val;
    } value;
};

/**
 * Parse JSON string
 * @param json JSON string
 * @return Parsed JSON value or NULL on error
 */
json_value_t* json_parse(const char *json);

/**
 * Free JSON value and all children
 * @param val JSON value to free
 */
void json_free(json_value_t *val);

/**
 * Create JSON object
 * @return New JSON object or NULL on error
 */
json_value_t* json_object_create(void);

/**
 * Create JSON array
 * @return New JSON array or NULL on error
 */
json_value_t* json_array_create(void);

/**
 * Create JSON string
 * @param str String value (will be copied)
 * @return New JSON string or NULL on error
 */
json_value_t* json_string_create(const char *str);

/**
 * Create JSON number
 * @param num Number value
 * @return New JSON number or NULL on error
 */
json_value_t* json_number_create(double num);

/**
 * Create JSON boolean
 * @param val Boolean value
 * @return New JSON boolean or NULL on error
 */
json_value_t* json_bool_create(bool val);

/**
 * Create JSON null
 * @return New JSON null or NULL on error
 */
json_value_t* json_null_create(void);

/**
 * Add key-value pair to object
 * @param obj JSON object
 * @param key Key string
 * @param val Value (ownership transferred)
 * @return true on success
 */
bool json_object_set(json_value_t *obj, const char *key, json_value_t *val);

/**
 * Get value from object by key
 * @param obj JSON object
 * @param key Key string
 * @return Value or NULL if not found
 */
json_value_t* json_object_get(const json_value_t *obj, const char *key);

/**
 * Check if object has key
 * @param obj JSON object
 * @param key Key string
 * @return true if key exists
 */
bool json_object_has(const json_value_t *obj, const char *key);

/**
 * Get number of keys in object
 * @param obj JSON object
 * @return Number of keys
 */
size_t json_object_size(const json_value_t *obj);

/**
 * Add value to array
 * @param arr JSON array
 * @param val Value (ownership transferred)
 * @return true on success
 */
bool json_array_add(json_value_t *arr, json_value_t *val);

/**
 * Get value from array by index
 * @param arr JSON array
 * @param index Index
 * @return Value or NULL if out of bounds
 */
json_value_t* json_array_get(const json_value_t *arr, size_t index);

/**
 * Get array length
 * @param arr JSON array
 * @return Array length
 */
size_t json_array_size(const json_value_t *arr);

/**
 * Serialize JSON value to string
 * @param val JSON value
 * @param buf Output buffer
 * @param size Buffer size
 * @return Number of characters written (excluding null terminator)
 */
size_t json_stringify(const json_value_t *val, char *buf, size_t size);

/**
 * Serialize JSON value to string (pretty-printed)
 * @param val JSON value
 * @param buf Output buffer
 * @param size Buffer size
 * @param indent Indentation string (e.g., "  ")
 * @return Number of characters written (excluding null terminator)
 */
size_t json_stringify_pretty(const json_value_t *val, char *buf, size_t size,
                             const char *indent);

/**
 * Get string value (convenience)
 * @param val JSON value
 * @return String value or NULL if not a string
 */
const char* json_get_string(const json_value_t *val);

/**
 * Get number value (convenience)
 * @param val JSON value
 * @param out Output number
 * @return true if value is a number
 */
bool json_get_number(const json_value_t *val, double *out);

/**
 * Get boolean value (convenience)
 * @param val JSON value
 * @param out Output boolean
 * @return true if value is a boolean
 */
bool json_get_bool(const json_value_t *val, bool *out);

/**
 * Check if value is null
 * @param val JSON value
 * @return true if value is null
 */
bool json_is_null(const json_value_t *val);

/**
 * Get type of value
 * @param val JSON value
 * @return JSON type
 */
json_type_t json_get_type(const json_value_t *val);

/**
 * Get string from object (convenience)
 * @param obj JSON object
 * @param key Key string
 * @return String value or NULL
 */
const char* json_object_get_string(const json_value_t *obj, const char *key);

/**
 * Get number from object (convenience)
 * @param obj JSON object
 * @param key Key string
 * @param default_val Default value if not found
 * @return Number value or default
 */
int json_object_get_int(const json_value_t *obj, const char *key, int default_val);

/**
 * Get double from object (convenience)
 * @param obj JSON object
 * @param key Key string
 * @param default_val Default value if not found
 * @return Double value or default
 */
double json_object_get_double(const json_value_t *obj, const char *key, double default_val);

/**
 * Get boolean from object (convenience)
 * @param obj JSON object
 * @param key Key string
 * @param default_val Default value if not found
 * @return Boolean value or default
 */
bool json_object_get_bool(const json_value_t *obj, const char *key, bool default_val);

#endif /* PSHELL_JSON_SIMPLE_H */
