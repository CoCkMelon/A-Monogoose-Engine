// scene_loader.c
#include "scene_loader.h"
#include <yaml.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

// Internal structures
typedef struct {
    yaml_parser_t parser;
    yaml_document_t* document;
    scene_t* scene;
    scene_error_info_t* error;
    
    // Current parsing context
    char current_path[256];
    int path_depth;
} parse_context_t;

// Helper macros
#define SET_ERROR(ctx, err_code, fmt, ...) do { \
    if ((ctx)->error) { \
        (ctx)->error->code = (err_code); \
        snprintf((ctx)->error->message, sizeof((ctx)->error->message), fmt, ##__VA_ARGS__); \
        strncpy((ctx)->error->path, (ctx)->current_path, sizeof((ctx)->error->path) - 1); \
    } \
} while(0)

#define SAFE_STRDUP(s) ((s) ? strdup(s) : NULL)

// Forward declarations
static bool parse_document(parse_context_t* ctx);
static bool parse_metadata(parse_context_t* ctx, yaml_node_t* node);
static bool parse_entities(parse_context_t* ctx, yaml_node_t* node);
static bool parse_entity(parse_context_t* ctx, const char* name, yaml_node_t* node, entity_t* entity);
static bool parse_hierarchy(parse_context_t* ctx, yaml_node_t* node);
static bool parse_relationships(parse_context_t* ctx, yaml_node_t* node);
static bool parse_systems(parse_context_t* ctx, yaml_node_t* node);
static scene_meta_t* parse_meta(parse_context_t* ctx, yaml_node_t* node);
static component_value_t parse_component_value(parse_context_t* ctx, yaml_node_t* node);
static void free_component_value(component_value_t* value);
static char* get_scalar_value(yaml_document_t* doc, yaml_node_t* node);
static yaml_node_t* get_map_value(yaml_document_t* doc, yaml_node_t* map, const char* key);

// Implementation

scene_t* scene_load(const char* filename, scene_error_info_t* error) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        if (error) {
            error->code = SCENE_ERR_FILE_NOT_FOUND;
            snprintf(error->message, sizeof(error->message), 
                     "Cannot open file: %s", filename);
        }
        return NULL;
    }
    
    scene_t* scene = calloc(1, sizeof(scene_t));
    if (!scene) {
        fclose(file);
        if (error) {
            error->code = SCENE_ERR_MEMORY;
            strcpy(error->message, "Out of memory");
        }
        return NULL;
    }
    
    parse_context_t ctx = {0};
    ctx.scene = scene;
    ctx.error = error;
    
    yaml_parser_initialize(&ctx.parser);
    yaml_parser_set_input_file(&ctx.parser, file);
    
    yaml_document_t document;
    if (!yaml_parser_load(&ctx.parser, &document)) {
        if (error) {
            error->code = SCENE_ERR_PARSE_ERROR;
            snprintf(error->message, sizeof(error->message),
                     "YAML parse error at line %zu: %s",
                     ctx.parser.mark.line, ctx.parser.problem);
            error->line = ctx.parser.mark.line;
            error->column = ctx.parser.mark.column;
        }
        yaml_parser_delete(&ctx.parser);
        fclose(file);
        free(scene);
        return NULL;
    }
    
    ctx.document = &document;
    bool success = parse_document(&ctx);
    
    yaml_document_delete(&document);
    yaml_parser_delete(&ctx.parser);
    fclose(file);
    
    if (!success) {
        scene_free(scene);
        return NULL;
    }
    
    // Validate the loaded scene
    if (!scene_validate(scene, error)) {
        scene_free(scene);
        return NULL;
    }
    
    return scene;
}

static bool parse_document(parse_context_t* ctx) {
    yaml_node_t* root = yaml_document_get_root_node(ctx->document);
    if (!root || root->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_PARSE_ERROR, "Root must be a mapping");
        return false;
    }
    
    // Parse metadata (required)
    yaml_node_t* metadata = get_map_value(ctx->document, root, "metadata");
    if (!metadata) {
        SET_ERROR(ctx, SCENE_ERR_MISSING_REQUIRED, "Missing required 'metadata' section");
        return false;
    }
    if (!parse_metadata(ctx, metadata)) {
        return false;
    }
    
    // Parse entities (required)
    yaml_node_t* entities = get_map_value(ctx->document, root, "entities");
    if (!entities) {
        SET_ERROR(ctx, SCENE_ERR_MISSING_REQUIRED, "Missing required 'entities' section");
        return false;
    }
    if (!parse_entities(ctx, entities)) {
        return false;
    }
    
    // Parse optional sections
    yaml_node_t* hierarchy = get_map_value(ctx->document, root, "hierarchy");
    if (hierarchy && !parse_hierarchy(ctx, hierarchy)) {
        return false;
    }
    
    yaml_node_t* relationships = get_map_value(ctx->document, root, "relationships");
    if (relationships && !parse_relationships(ctx, relationships)) {
        return false;
    }
    
    yaml_node_t* systems = get_map_value(ctx->document, root, "systems");
    if (systems && !parse_systems(ctx, systems)) {
        return false;
    }
    
    return true;
}

static bool parse_metadata(parse_context_t* ctx, yaml_node_t* node) {
    if (node->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_INVALID_TYPE, "Metadata must be a mapping");
        return false;
    }
    
    yaml_node_t* name = get_map_value(ctx->document, node, "name");
    if (!name) {
        SET_ERROR(ctx, SCENE_ERR_MISSING_REQUIRED, "Missing required metadata.name");
        return false;
    }
    ctx->scene->metadata.name = SAFE_STRDUP(get_scalar_value(ctx->document, name));
    
    yaml_node_t* version = get_map_value(ctx->document, node, "version");
    if (!version) {
        SET_ERROR(ctx, SCENE_ERR_MISSING_REQUIRED, "Missing required metadata.version");
        return false;
    }
    ctx->scene->metadata.version = SAFE_STRDUP(get_scalar_value(ctx->document, version));
    
    // Optional fields
    yaml_node_t* author = get_map_value(ctx->document, node, "author");
    if (author) {
        ctx->scene->metadata.author = SAFE_STRDUP(get_scalar_value(ctx->document, author));
    }
    
    yaml_node_t* description = get_map_value(ctx->document, node, "description");
    if (description) {
        ctx->scene->metadata.description = SAFE_STRDUP(get_scalar_value(ctx->document, description));
    }
    
    return true;
}

static bool parse_entities(parse_context_t* ctx, yaml_node_t* node) {
    if (node->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_INVALID_TYPE, "Entities must be a mapping");
        return false;
    }
    
    yaml_node_pair_t* pair;
    size_t count = 0;
    
    // Count entities
    for (pair = node->data.mapping.pairs.start; 
         pair < node->data.mapping.pairs.top; pair++) {
        count++;
    }
    
    ctx->scene->entities = calloc(count, sizeof(entity_t));
    ctx->scene->entities_count = count;
    
    size_t idx = 0;
    for (pair = node->data.mapping.pairs.start; 
         pair < node->data.mapping.pairs.top; pair++) {
        yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
        yaml_node_t* value = yaml_document_get_node(ctx->document, pair->value);
        
        const char* entity_name = get_scalar_value(ctx->document, key);
        if (!parse_entity(ctx, entity_name, value, &ctx->scene->entities[idx])) {
            return false;
        }
        idx++;
    }
    
    return true;
}

static bool parse_entity(parse_context_t* ctx, const char* name, yaml_node_t* node, entity_t* entity) {
    entity->name = strdup(name);
    entity->enabled = true;  // Default
    
    if (node->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_INVALID_TYPE, "Entity '%s' must be a mapping", name);
        return false;
    }
    
    // Parse _meta if present
    yaml_node_t* meta_node = get_map_value(ctx->document, node, "_meta");
    if (meta_node) {
        entity->meta = parse_meta(ctx, meta_node);
    }
    
    // Parse prefab reference
    yaml_node_t* prefab = get_map_value(ctx->document, node, "prefab");
    if (prefab) {
        entity->prefab = SAFE_STRDUP(get_scalar_value(ctx->document, prefab));
    }
    
    // Parse tags
    yaml_node_t* tags = get_map_value(ctx->document, node, "tags");
    if (tags && tags->type == YAML_SEQUENCE_NODE) {
        entity->tags_count = tags->data.sequence.items.top - tags->data.sequence.items.start;
        entity->tags = calloc(entity->tags_count, sizeof(char*));
        
        size_t i = 0;
        for (yaml_node_item_t* item = tags->data.sequence.items.start;
             item < tags->data.sequence.items.top; item++) {
            yaml_node_t* tag_node = yaml_document_get_node(ctx->document, *item);
            entity->tags[i++] = SAFE_STRDUP(get_scalar_value(ctx->document, tag_node));
        }
    }
    
    // Parse components
    yaml_node_t* components = get_map_value(ctx->document, node, "components");
    if (components && components->type == YAML_MAPPING_NODE) {
        size_t comp_count = 0;
        yaml_node_pair_t* pair;
        
        // Count components
        for (pair = components->data.mapping.pairs.start; 
             pair < components->data.mapping.pairs.top; pair++) {
            comp_count++;
        }
        
        entity->components = calloc(comp_count, sizeof(component_t));
        entity->components_count = comp_count;
        
        size_t idx = 0;
        for (pair = components->data.mapping.pairs.start; 
             pair < components->data.mapping.pairs.top; pair++) {
            yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
            yaml_node_t* value = yaml_document_get_node(ctx->document, pair->value);
            
            entity->components[idx].type_name = SAFE_STRDUP(get_scalar_value(ctx->document, key));
            entity->components[idx].data = parse_component_value(ctx, value);
            idx++;
        }
    }
    
    // Parse enabled flag
    yaml_node_t* enabled = get_map_value(ctx->document, node, "enabled");
    if (enabled) {
        const char* val = get_scalar_value(ctx->document, enabled);
        entity->enabled = (val && (strcmp(val, "true") == 0 || strcmp(val, "1") == 0));
    }
    
    return true;
}

static scene_meta_t* parse_meta(parse_context_t* ctx, yaml_node_t* node) {
    if (node->type != YAML_MAPPING_NODE) {
        return NULL;
    }
    
    scene_meta_t* meta = scene_meta_create();
    
    yaml_node_t* description = get_map_value(ctx->document, node, "description");
    if (description) {
        meta->description = SAFE_STRDUP(get_scalar_value(ctx->document, description));
    }
    
    yaml_node_t* author = get_map_value(ctx->document, node, "author");
    if (author) {
        meta->author = SAFE_STRDUP(get_scalar_value(ctx->document, author));
    }
    
    yaml_node_t* notes = get_map_value(ctx->document, node, "notes");
    if (notes) {
        if (notes->type == YAML_SCALAR_NODE) {
            // Single note as string
            scene_meta_add_note(meta, get_scalar_value(ctx->document, notes));
        } else if (notes->type == YAML_SEQUENCE_NODE) {
            // Multiple notes as array
            for (yaml_node_item_t* item = notes->data.sequence.items.start;
                 item < notes->data.sequence.items.top; item++) {
                yaml_node_t* note_node = yaml_document_get_node(ctx->document, *item);
                scene_meta_add_note(meta, get_scalar_value(ctx->document, note_node));
            }
        }
    }
    
    yaml_node_t* todo = get_map_value(ctx->document, node, "todo");
    if (todo) {
        meta->todo = SAFE_STRDUP(get_scalar_value(ctx->document, todo));
    }
    
    yaml_node_t* deprecated = get_map_value(ctx->document, node, "deprecated");
    if (deprecated) {
        const char* val = get_scalar_value(ctx->document, deprecated);
        meta->deprecated = (val && (strcmp(val, "true") == 0));
    }
    
    yaml_node_t* version = get_map_value(ctx->document, node, "version");
    if (version) {
        const char* val = get_scalar_value(ctx->document, version);
        if (val) meta->version = atoi(val);
    }
    
    // Parse any other fields as custom
    yaml_node_pair_t* pair;
    for (pair = node->data.mapping.pairs.start; 
         pair < node->data.mapping.pairs.top; pair++) {
        yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
        const char* key_str = get_scalar_value(ctx->document, key);
        
        // Skip known fields
        if (strcmp(key_str, "description") == 0 ||
            strcmp(key_str, "author") == 0 ||
            strcmp(key_str, "notes") == 0 ||
            strcmp(key_str, "todo") == 0 ||
            strcmp(key_str, "deprecated") == 0 ||
            strcmp(key_str, "version") == 0) {
            continue;
        }
        
        yaml_node_t* value = yaml_document_get_node(ctx->document, pair->value);
        if (value->type == YAML_SCALAR_NODE) {
            scene_meta_set_custom(meta, key_str, get_scalar_value(ctx->document, value));
        }
    }
    
    return meta;
}

static component_value_t parse_component_value(parse_context_t* ctx, yaml_node_t* node) {
    component_value_t value = {0};
    
    switch (node->type) {
        case YAML_NO_NODE:
            value.type = COMPONENT_TYPE_NULL;
            break;
            
        case YAML_SCALAR_NODE: {
            const char* scalar = (const char*)node->data.scalar.value;
            
            // Try to parse as boolean
            if (strcmp(scalar, "true") == 0 || strcmp(scalar, "false") == 0) {
                value.type = COMPONENT_TYPE_BOOL;
                value.bool_val = (strcmp(scalar, "true") == 0);
            }
            // Try to parse as number
            else if (strspn(scalar, "-0123456789.eE") == strlen(scalar)) {
                if (strchr(scalar, '.') || strchr(scalar, 'e') || strchr(scalar, 'E')) {
                    value.type = COMPONENT_TYPE_FLOAT;
                    value.float_val = strtod(scalar, NULL);
                } else {
                    value.type = COMPONENT_TYPE_INT;
                    value.int_val = strtoll(scalar, NULL, 10);
                }
            }
            // Otherwise it's a string
            else {
                value.type = COMPONENT_TYPE_STRING;
                value.string_val = strdup(scalar);
            }
            break;
        }
        
        case YAML_SEQUENCE_NODE: {
            value.type = COMPONENT_TYPE_ARRAY;
            size_t count = node->data.sequence.items.top - node->data.sequence.items.start;
            value.array_val.count = count;
            value.array_val.values = calloc(count, sizeof(component_value_t));
            
            size_t i = 0;
            for (yaml_node_item_t* item = node->data.sequence.items.start;
                 item < node->data.sequence.items.top; item++) {
                yaml_node_t* item_node = yaml_document_get_node(ctx->document, *item);
                value.array_val.values[i++] = parse_component_value(ctx, item_node);
            }
            break;
        }
        
        case YAML_MAPPING_NODE: {
            value.type = COMPONENT_TYPE_OBJECT;
            size_t count = 0;
            yaml_node_pair_t* pair;
            
            // Count pairs
            for (pair = node->data.mapping.pairs.start; 
                 pair < node->data.mapping.pairs.top; pair++) {
                count++;
            }
            
            value.object_val.count = count;
            value.object_val.items = calloc(count, sizeof(*value.object_val.items));
            
            size_t i = 0;
            for (pair = node->data.mapping.pairs.start; 
                 pair < node->data.mapping.pairs.top; pair++) {
                yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
                yaml_node_t* val = yaml_document_get_node(ctx->document, pair->value);
                
                value.object_val.items[i].key = SAFE_STRDUP(get_scalar_value(ctx->document, key));
                value.object_val.items[i].value = malloc(sizeof(component_value_t));
                *value.object_val.items[i].value = parse_component_value(ctx, val);
                i++;
            }
            break;
        }
        
        default:
            value.type = COMPONENT_TYPE_NULL;
            break;
    }
    
    return value;
}

static bool parse_hierarchy(parse_context_t* ctx, yaml_node_t* node) {
    // Parse tree format if present
    yaml_node_t* tree = get_map_value(ctx->document, node, "tree");
    if (tree) {
        // TODO: Implement tree parsing
        // This would build the hierarchy_root structure
    }
    
    // Parse flat relations if present
    yaml_node_t* relations = get_map_value(ctx->document, node, "relations");
    if (relations && relations->type == YAML_SEQUENCE_NODE) {
        size_t count = relations->data.sequence.items.top - relations->data.sequence.items.start;
        ctx->scene->hierarchy_relations = calloc(count, sizeof(parent_child_relation_t));
        ctx->scene->hierarchy_relations_count = count;
        
        size_t i = 0;
        for (yaml_node_item_t* item = relations->data.sequence.items.start;
             item < relations->data.sequence.items.top; item++) {
            yaml_node_t* rel_node = yaml_document_get_node(ctx->document, *item);
            
            yaml_node_t* parent = get_map_value(ctx->document, rel_node, "parent");
            yaml_node_t* child = get_map_value(ctx->document, rel_node, "child");
            
            if (parent && child) {
                ctx->scene->hierarchy_relations[i].parent = 
                    SAFE_STRDUP(get_scalar_value(ctx->document, parent));
                ctx->scene->hierarchy_relations[i].child = 
                    SAFE_STRDUP(get_scalar_value(ctx->document, child));
                
                yaml_node_t* order = get_map_value(ctx->document, rel_node, "order");
                if (order) {
                    const char* val = get_scalar_value(ctx->document, order);
                    if (val) ctx->scene->hierarchy_relations[i].order = atoi(val);
                }
                i++;
            }
        }
    }
    
    return true;
}

static bool parse_relationships(parse_context_t* ctx, yaml_node_t* node) {
    // Parse constraints.joints if present
    yaml_node_t* constraints = get_map_value(ctx->document, node, "constraints");
    if (constraints && constraints->type == YAML_MAPPING_NODE) {
        yaml_node_t* joints = get_map_value(ctx->document, constraints, "joints");
        if (joints && joints->type == YAML_SEQUENCE_NODE) {
            size_t count = joints->data.sequence.items.top - joints->data.sequence.items.start;
            ctx->scene->constraints.joints = calloc(count, sizeof(*ctx->scene->constraints.joints));
            ctx->scene->constraints.joints_count = count;
            size_t i = 0;
            for (yaml_node_item_t* item = joints->data.sequence.items.start; item < joints->data.sequence.items.top; item++) {
                yaml_node_t* jn = yaml_document_get_node(ctx->document, *item);
                if (jn && jn->type == YAML_MAPPING_NODE) {
                    yaml_node_t* type = get_map_value(ctx->document, jn, "type");
                    yaml_node_t* a = get_map_value(ctx->document, jn, "entity_a");
                    yaml_node_t* b = get_map_value(ctx->document, jn, "entity_b");
                    ctx->scene->constraints.joints[i].type = SAFE_STRDUP(get_scalar_value(ctx->document, type));
                    ctx->scene->constraints.joints[i].entity_a = SAFE_STRDUP(get_scalar_value(ctx->document, a));
                    ctx->scene->constraints.joints[i].entity_b = SAFE_STRDUP(get_scalar_value(ctx->document, b));
                    i++;
                }
            }
        }
    }
    return true;
}

static bool parse_systems(parse_context_t* ctx, yaml_node_t* node) {
    if (node->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_INVALID_TYPE, "Systems must be a mapping");
        return false;
    }
    
    size_t count = 0;
    yaml_node_pair_t* pair;
    
    // Count systems
    for (pair = node->data.mapping.pairs.start; 
         pair < node->data.mapping.pairs.top; pair++) {
        count++;
    }
    
    ctx->scene->systems = calloc(count, sizeof(system_config_t));
    ctx->scene->systems_count = count;
    
    size_t idx = 0;
    for (pair = node->data.mapping.pairs.start; 
         pair < node->data.mapping.pairs.top; pair++) {
        yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
        yaml_node_t* value = yaml_document_get_node(ctx->document, pair->value);
        
        ctx->scene->systems[idx].name = SAFE_STRDUP(get_scalar_value(ctx->document, key));
        ctx->scene->systems[idx].enabled = true;  // Default
        
        // Parse system config
        if (value->type == YAML_MAPPING_NODE) {
            yaml_node_t* meta_node = get_map_value(ctx->document, value, "_meta");
            if (meta_node) {
                ctx->scene->systems[idx].meta = parse_meta(ctx, meta_node);
            }
            
            ctx->scene->systems[idx].config = parse_component_value(ctx, value);
        }
        
        idx++;
    }
    
    return true;
}

// Validation
bool scene_validate(const scene_t* scene, scene_error_info_t* error) {
    if (!scene) {
        if (error) {
            error->code = SCENE_ERR_VALIDATION_ERROR;
            strcpy(error->message, "Scene is NULL");
        }
        return false;
    }
    
    // Check required metadata
    if (!scene->metadata.name || !scene->metadata.version) {
        if (error) {
            error->code = SCENE_ERR_MISSING_REQUIRED;
            strcpy(error->message, "Missing required metadata fields");
        }
        return false;
    }
    
    // Validate version format (simple check)
    const char* ver = scene->metadata.version;
    int dots = 0;
    for (const char* p = ver; *p; p++) {
        if (*p == '.') dots++;
        else if (*p < '0' || *p > '9') {
            if (error) {
                error->code = SCENE_ERR_VALIDATION_ERROR;
                snprintf(error->message, sizeof(error->message),
                         "Invalid version format: %s", ver);
            }
            return false;
        }
    }
    if (dots != 2) {
        if (error) {
            error->code = SCENE_ERR_VALIDATION_ERROR;
            snprintf(error->message, sizeof(error->message),
                     "Version must be in format X.Y.Z: %s", ver);
        }
        return false;
    }
    
    // Validate entity names are unique
    for (size_t i = 0; i < scene->entities_count; i++) {
        for (size_t j = i + 1; j < scene->entities_count; j++) {
            if (strcmp(scene->entities[i].name, scene->entities[j].name) == 0) {
                if (error) {
                    error->code = SCENE_ERR_VALIDATION_ERROR;
                    snprintf(error->message, sizeof(error->message),
                             "Duplicate entity name: %s", scene->entities[i].name);
                }
                return false;
            }
        }
    }
    
    // Validate hierarchy references exist
    for (size_t i = 0; i < scene->hierarchy_relations_count; i++) {
        const parent_child_relation_t* rel = &scene->hierarchy_relations[i];
        
        if (!scene_find_entity(scene, rel->parent)) {
            if (error) {
                error->code = SCENE_ERR_INVALID_REFERENCE;
                snprintf(error->message, sizeof(error->message),
                         "Hierarchy parent '%s' not found", rel->parent);
            }
            return false;
        }
        
        if (!scene_find_entity(scene, rel->child)) {
            if (error) {
                error->code = SCENE_ERR_INVALID_REFERENCE;
                snprintf(error->message, sizeof(error->message),
                         "Hierarchy child '%s' not found", rel->child);
            }
            return false;
        }
    }
    
    // Check for circular dependencies in hierarchy
    // TODO: Implement cycle detection
    
    return true;
}

// Query functions
entity_t* scene_find_entity(const scene_t* scene, const char* name) {
    if (!scene || !name) return NULL;
    
    for (size_t i = 0; i < scene->entities_count; i++) {
        if (strcmp(scene->entities[i].name, name) == 0) {
            return &scene->entities[i];
        }
    }
    return NULL;
}

component_t* entity_find_component(const entity_t* entity, const char* type_name) {
    if (!entity || !type_name) return NULL;
    
    for (size_t i = 0; i < entity->components_count; i++) {
        if (strcmp(entity->components[i].type_name, type_name) == 0) {
            return &entity->components[i];
        }
    }
    return NULL;
}

bool entity_has_tag(const entity_t* entity, const char* tag) {
    if (!entity || !tag) return false;
    
    for (size_t i = 0; i < entity->tags_count; i++) {
        if (strcmp(entity->tags[i], tag) == 0) {
            return true;
        }
    }
    return false;
}

// Utility functions
static char* get_scalar_value(yaml_document_t* doc, yaml_node_t* node) {
    if (!node || node->type != YAML_SCALAR_NODE) return NULL;
    return (char*)node->data.scalar.value;
}

static yaml_node_t* get_map_value(yaml_document_t* doc, yaml_node_t* map, const char* key) {
    if (!map || map->type != YAML_MAPPING_NODE) return NULL;
    
    for (yaml_node_pair_t* pair = map->data.mapping.pairs.start;
         pair < map->data.mapping.pairs.top; pair++) {
        yaml_node_t* key_node = yaml_document_get_node(doc, pair->key);
        if (key_node && key_node->type == YAML_SCALAR_NODE) {
            if (strcmp((char*)key_node->data.scalar.value, key) == 0) {
                return yaml_document_get_node(doc, pair->value);
            }
        }
    }
    return NULL;
}

scene_meta_t* scene_meta_create(void) {
    return calloc(1, sizeof(scene_meta_t));
}

void scene_meta_add_note(scene_meta_t* meta, const char* note) {
    if (!meta || !note) return;
    
    meta->notes = realloc(meta->notes, (meta->notes_count + 1) * sizeof(char*));
    meta->notes[meta->notes_count++] = strdup(note);
}

void scene_meta_set_custom(scene_meta_t* meta, const char* key, const char* value) {
    if (!meta || !key || !value) return;
    
    // Check if key already exists
    for (size_t i = 0; i < meta->custom_fields_count; i++) {
        if (strcmp(meta->custom_fields[i].key, key) == 0) {
            free(meta->custom_fields[i].value);
            meta->custom_fields[i].value = strdup(value);
            return;
        }
    }
    
    // Add new field
    meta->custom_fields = realloc(meta->custom_fields, 
                                  (meta->custom_fields_count + 1) * sizeof(*meta->custom_fields));
    meta->custom_fields[meta->custom_fields_count].key = strdup(key);
    meta->custom_fields[meta->custom_fields_count].value = strdup(value);
    meta->custom_fields_count++;
}

void scene_meta_free(scene_meta_t* meta) {
    if (!meta) return;
    
    free(meta->description);
    free(meta->author);
    free(meta->modified);
    free(meta->todo);
    
    for (size_t i = 0; i < meta->notes_count; i++) {
        free(meta->notes[i]);
    }
    free(meta->notes);
    
    for (size_t i = 0; i < meta->custom_fields_count; i++) {
        free(meta->custom_fields[i].key);
        free(meta->custom_fields[i].value);
    }
    free(meta->custom_fields);
    
    free(meta);
}

static void free_component_value(component_value_t* value) {
    if (!value) return;
    
    switch (value->type) {
        case COMPONENT_TYPE_STRING:
            free(value->string_val);
            break;
            
        case COMPONENT_TYPE_ARRAY:
            for (size_t i = 0; i < value->array_val.count; i++) {
                free_component_value(&value->array_val.values[i]);
            }
            free(value->array_val.values);
            break;
            
        case COMPONENT_TYPE_OBJECT:
            for (size_t i = 0; i < value->object_val.count; i++) {
                free(value->object_val.items[i].key);
                free_component_value(value->object_val.items[i].value);
                free(value->object_val.items[i].value);
            }
            free(value->object_val.items);
            break;
            
        default:
            break;
    }
}

void scene_free(scene_t* scene) {
    if (!scene) return;
    
    // Free metadata
    free(scene->metadata.name);
    free(scene->metadata.version);
    free(scene->metadata.author);
    free(scene->metadata.description);
    
    // Free entities
    for (size_t i = 0; i < scene->entities_count; i++) {
        entity_t* entity = &scene->entities[i];
        free(entity->name);
        free(entity->prefab);
        scene_meta_free(entity->meta);
        
        for (size_t j = 0; j < entity->tags_count; j++) {
            free(entity->tags[j]);
        }
        free(entity->tags);
        
        for (size_t j = 0; j < entity->components_count; j++) {
            free(entity->components[j].type_name);
            free_component_value(&entity->components[j].data);
        }
        free(entity->components);
        
        for (size_t j = 0; j < entity->properties_count; j++) {
            free(entity->properties[j].key);
            free_component_value(&entity->properties[j].value);
            // No extra free for entity->properties[j].value since it's not heap-allocated as a pointer
        }
        free(entity->properties);
    }
    free(scene->entities);
    
    // Free hierarchy relations
    for (size_t i = 0; i < scene->hierarchy_relations_count; i++) {
        free(scene->hierarchy_relations[i].parent);
        free(scene->hierarchy_relations[i].child);
    }
    free(scene->hierarchy_relations);
    
    // Free systems
    for (size_t i = 0; i < scene->systems_count; i++) {
        free(scene->systems[i].name);
        scene_meta_free(scene->systems[i].meta);
        free_component_value(&scene->systems[i].config);
    }
    free(scene->systems);
    
    // Free constraints
    for (size_t i = 0; i < scene->constraints.joints_count; i++) {
        free(scene->constraints.joints[i].type);
        free(scene->constraints.joints[i].entity_a);
        free(scene->constraints.joints[i].entity_b);
    }
    free(scene->constraints.joints);

    // TODO: Free other sections
    
    free(scene);
}
