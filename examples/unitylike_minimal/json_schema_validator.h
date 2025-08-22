// json_schema_validator.h
#ifndef JSON_SCHEMA_VALIDATOR_H
#define JSON_SCHEMA_VALIDATOR_H

#include <stdbool.h>
#include <stddef.h>
#include "cjson/cJSON.h"

typedef struct schema_validator_t schema_validator_t;

typedef enum {
    SCHEMA_VALID = 0,
    SCHEMA_ERR_INVALID_SCHEMA,
    SCHEMA_ERR_TYPE_MISMATCH,
    SCHEMA_ERR_MISSING_REQUIRED,
    SCHEMA_ERR_ADDITIONAL_PROPERTIES,
    SCHEMA_ERR_PATTERN_MISMATCH,
    SCHEMA_ERR_ENUM_MISMATCH,
    SCHEMA_ERR_MIN_VALUE,
    SCHEMA_ERR_MAX_VALUE,
    SCHEMA_ERR_MIN_LENGTH,
    SCHEMA_ERR_MAX_LENGTH,
    SCHEMA_ERR_MIN_ITEMS,
    SCHEMA_ERR_MAX_ITEMS,
    SCHEMA_ERR_MIN_PROPERTIES,
    SCHEMA_ERR_MAX_PROPERTIES,
    SCHEMA_ERR_UNIQUE_ITEMS,
    SCHEMA_ERR_ONE_OF,
    SCHEMA_ERR_ANY_OF,
    SCHEMA_ERR_ALL_OF,
    SCHEMA_ERR_NOT,
    SCHEMA_ERR_REF_NOT_FOUND,
    SCHEMA_ERR_FORMAT
} schema_error_code_t;

typedef struct {
    schema_error_code_t code;
    char* path;           // JSON path where error occurred
    char* message;        // Human-readable error message
    char* schema_path;    // Path in schema that failed
    cJSON* instance;      // The JSON value that failed
    cJSON* schema;        // The schema rule that failed
} schema_error_t;

typedef struct {
    schema_error_t* errors;
    size_t count;
    size_t capacity;
} schema_errors_t;

// Create/destroy validator
schema_validator_t* schema_validator_create(const char* schema_json);
schema_validator_t* schema_validator_create_from_file(const char* schema_file);
void schema_validator_destroy(schema_validator_t* validator);

// Validate JSON against schema
bool schema_validate(schema_validator_t* validator, cJSON* json, schema_errors_t* errors);
bool schema_validate_string(schema_validator_t* validator, const char* json_string, schema_errors_t* errors);

// Error handling
schema_errors_t* schema_errors_create(void);
void schema_errors_destroy(schema_errors_t* errors);
void schema_errors_clear(schema_errors_t* errors);
void schema_errors_add(schema_errors_t* errors, schema_error_code_t code, 
                       const char* path, const char* message);

// Convert YAML to JSON for validation
cJSON* yaml_to_json(yaml_document_t* document, yaml_node_t* node);

#endif // JSON_SCHEMA_VALIDATOR_H