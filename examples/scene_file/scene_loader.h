// scene_loader.h
#ifndef SCENE_LOADER_H
#define SCENE_LOADER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Forward declarations
typedef struct scene_t scene_t;
typedef struct entity_t entity_t;
typedef struct component_t component_t;
typedef struct hierarchy_node_t hierarchy_node_t;

// Error codes
typedef enum {
    SCENE_OK = 0,
    SCENE_ERR_FILE_NOT_FOUND,
    SCENE_ERR_PARSE_ERROR,
    SCENE_ERR_VALIDATION_ERROR,
    SCENE_ERR_MEMORY,
    SCENE_ERR_MISSING_REQUIRED,
    SCENE_ERR_INVALID_TYPE,
    SCENE_ERR_INVALID_REFERENCE,
    SCENE_ERR_CIRCULAR_DEPENDENCY
} scene_error_t;

// Error context
typedef struct {
    scene_error_t code;
    char message[256];
    char path[256];  // YAML path where error occurred
    int line;
    int column;
} scene_error_info_t;

// Metadata that survives regeneration
typedef struct {
    char* description;
    char** notes;
    size_t notes_count;
    char* author;
    char* modified;
    char* todo;
    bool deprecated;
    int version;
    // Custom fields stored as key-value pairs
    struct {
        char* key;
        char* value;
    }* custom_fields;
    size_t custom_fields_count;
} scene_meta_t;

// Component data (variant type)
typedef enum {
    COMPONENT_TYPE_NULL,
    COMPONENT_TYPE_BOOL,
    COMPONENT_TYPE_INT,
    COMPONENT_TYPE_FLOAT,
    COMPONENT_TYPE_STRING,
    COMPONENT_TYPE_OBJECT,
    COMPONENT_TYPE_ARRAY
} component_value_type_t;

typedef struct component_value_t {
    component_value_type_t type;
    union {
        bool bool_val;
        int64_t int_val;
        double float_val;
        char* string_val;
        struct {
            struct component_value_t* values;
            size_t count;
        } array_val;
        struct {
            char* key;
            struct component_value_t* value;
        }* object_val;
        size_t object_count;
    };
} component_value_t;

// Component instance
struct component_t {
    char* type_name;
    component_value_t data;
};

// Entity definition
struct entity_t {
    char* name;
    scene_meta_t* meta;
    char* prefab;  // Optional prefab reference
    char** tags;
    size_t tags_count;
    component_t* components;
    size_t components_count;
    bool enabled;
    
    // Custom properties
    struct {
        char* key;
        component_value_t value;
    }* properties;
    size_t properties_count;
};

// Hierarchy node
struct hierarchy_node_t {
    char* entity_name;
    hierarchy_node_t** children;
    size_t children_count;
    hierarchy_node_t* parent;  // Back reference
};

// Parent-child relation (alternative format)
typedef struct {
    char* parent;
    char* child;
    int order;
} parent_child_relation_t;

// Edge for graphs
typedef struct {
    char* from;
    char* to;
    double weight;
    bool bidirectional;
    char* type;
    scene_meta_t* meta;
} edge_t;

// Relationship group
typedef struct {
    char* name;
    scene_meta_t* meta;
    edge_t* edges;
    size_t edges_count;
} relationship_group_t;

// System configuration
typedef struct {
    char* name;
    scene_meta_t* meta;
    bool enabled;
    // System-specific config stored as component values
    component_value_t config;
} system_config_t;

// Complete scene
struct scene_t {
    // Metadata
    struct {
        char* name;
        char* version;
        char* author;
        char* description;
    } metadata;
    
    // Includes
    struct {
        char* path;
        char* namespace;
    }* includes;
    size_t includes_count;
    
    // Entities
    entity_t* entities;
    size_t entities_count;
    
    // Hierarchy
    hierarchy_node_t* hierarchy_root;
    parent_child_relation_t* hierarchy_relations;
    size_t hierarchy_relations_count;
    
    // Relationships
    relationship_group_t* relationships;
    size_t relationships_count;
    
    // Systems
    system_config_t* systems;
    size_t systems_count;
    
    // Error info
    scene_error_info_t last_error;
};

// Main API functions
scene_t* scene_load(const char* filename, scene_error_info_t* error);
scene_t* scene_load_from_string(const char* yaml_string, scene_error_info_t* error);
void scene_free(scene_t* scene);

// Validation functions
bool scene_validate(const scene_t* scene, scene_error_info_t* error);
bool scene_validate_schema(const scene_t* scene, const char* schema_path, scene_error_info_t* error);

// Query functions
entity_t* scene_find_entity(const scene_t* scene, const char* name);
component_t* entity_find_component(const entity_t* entity, const char* type_name);
bool entity_has_tag(const entity_t* entity, const char* tag);
hierarchy_node_t* scene_find_hierarchy_node(const scene_t* scene, const char* entity_name);

// Utility functions
char* scene_to_yaml(const scene_t* scene);
scene_meta_t* scene_meta_create(void);
void scene_meta_free(scene_meta_t* meta);
void scene_meta_add_note(scene_meta_t* meta, const char* note);
void scene_meta_set_custom(scene_meta_t* meta, const char* key, const char* value);

#endif // SCENE_LOADER_H