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

// ---- Hierarchy helpers ----
static hierarchy_node_t* hierarchy_create_node(const char* name, hierarchy_node_t* parent) {
    hierarchy_node_t* n = (hierarchy_node_t*)calloc(1, sizeof(hierarchy_node_t));
    if (!n) return NULL;
    n->entity_name = SAFE_STRDUP(name);
    n->parent = parent;
    return n;
}

static void hierarchy_append_child(hierarchy_node_t* parent, hierarchy_node_t* child) {
    if (!parent || !child) return;
    parent->children = (hierarchy_node_t**)realloc(parent->children, (parent->children_count + 1) * sizeof(hierarchy_node_t*));
    parent->children[parent->children_count++] = child;
}

static void hierarchy_free(hierarchy_node_t* node) {
    if (!node) return;
    for (size_t i = 0; i < node->children_count; i++) {
        hierarchy_free(node->children[i]);
    }
    free(node->children);
    free(node->entity_name);
    free(node);
}

static hierarchy_node_t* hierarchy_find(hierarchy_node_t* node, const char* name) {
    if (!node || !name) return NULL;
    if (node->entity_name && strcmp(node->entity_name, name) == 0) return node;
    for (size_t i = 0; i < node->children_count; i++) {
        hierarchy_node_t* f = hierarchy_find(node->children[i], name);
        if (f) return f;
    }
    return NULL;
}

// Forward decl for tree parsing
static bool parse_hierarchy_tree_map(parse_context_t* ctx, yaml_node_t* map_node, hierarchy_node_t** out_root);
static bool parse_hierarchy_children_seq(parse_context_t* ctx, yaml_node_t* seq_node, hierarchy_node_t* parent);

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
        // Tree can be a mapping of root -> [children]
        hierarchy_node_t* maybe_root = NULL;
        if (!parse_hierarchy_tree_map(ctx, tree, &maybe_root)) {
            return false;
        }
        ctx->scene->hierarchy_root = maybe_root;
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
    
    // Free hierarchy tree
    hierarchy_free(scene->hierarchy_root);
    
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

// ---- New public API additions ----
scene_t* scene_load_from_string(const char* yaml_string, scene_error_info_t* error) {
    if (!yaml_string) {
        if (error) { error->code = SCENE_ERR_PARSE_ERROR; strcpy(error->message, "NULL yaml string"); }
        return NULL;
    }

    scene_t* scene = calloc(1, sizeof(scene_t));
    if (!scene) {
        if (error) { error->code = SCENE_ERR_MEMORY; strcpy(error->message, "Out of memory"); }
        return NULL;
    }

    parse_context_t ctx = {0};
    ctx.scene = scene;
    ctx.error = error;

    yaml_parser_initialize(&ctx.parser);
    yaml_parser_set_input_string(&ctx.parser, (const unsigned char*)yaml_string, strlen(yaml_string));

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
        free(scene);
        return NULL;
    }

    ctx.document = &document;
    bool success = parse_document(&ctx);

    yaml_document_delete(&document);
    yaml_parser_delete(&ctx.parser);

    if (!success) {
        scene_free(scene);
        return NULL;
    }

    if (!scene_validate(scene, error)) {
        scene_free(scene);
        return NULL;
    }

    return scene;
}

bool scene_validate_schema(const scene_t* scene, const char* schema_path, scene_error_info_t* error) {
    (void)scene; (void)schema_path; (void)error;
    // Stub: schema validation not implemented in this lightweight loader.
    return true;
}

hierarchy_node_t* scene_find_hierarchy_node(const scene_t* scene, const char* entity_name) {
    if (!scene || !entity_name) return NULL;
    if (scene->hierarchy_root) {
        return hierarchy_find(scene->hierarchy_root, entity_name);
    }
    return NULL;
}

// Helpers to emit component_value_t to YAML recursively
static void emit_yaml_indent(char **out, size_t *len, size_t *cap, int indent) {
    for (int i = 0; i < indent; i++) {
        const char *s = "  ";
        size_t n = 2;
        if (*len + n + 1 > *cap) { size_t nc = *cap? *cap*2:1024; while (nc < *len+n+1) nc*=2; char *nb=(char*)realloc(*out,nc); if(!nb){ free(*out); *out=NULL; return; } *out=nb; *cap=nc; }
        memcpy(*out + *len, s, n); *len += n; (*out)[*len] = '\0';
    }
}

static void emit_yaml_str(char **out, size_t *len, size_t *cap, const char *s) {
    size_t n = strlen(s);
    if (*len + n + 1 > *cap) { size_t nc = *cap? *cap*2:1024; while (nc < *len+n+1) nc*=2; char *nb=(char*)realloc(*out,nc); if(!nb){ free(*out); *out=NULL; return; } *out=nb; *cap=nc; }
    memcpy(*out + *len, s, n); *len += n; (*out)[*len] = '\0';
}

static void emit_yaml_newline(char **out, size_t *len, size_t *cap) { emit_yaml_str(out, len, cap, "\n"); }

static void emit_yaml_value(char **out, size_t *len, size_t *cap, const component_value_t *v, int indent);

static void emit_yaml_object(char **out, size_t *len, size_t *cap, const component_value_t *v, int indent) {
    for (size_t i = 0; i < v->object_val.count; i++) {
        emit_yaml_indent(out, len, cap, indent);
        if (!*out) return;
        // Key
        const char *key = v->object_val.items[i].key ? v->object_val.items[i].key : "";
        emit_yaml_str(out, len, cap, key);
        emit_yaml_str(out, len, cap, ": ");
        // Value
        component_value_t *cv = v->object_val.items[i].value;
        if (cv && (cv->type == COMPONENT_TYPE_OBJECT || cv->type == COMPONENT_TYPE_ARRAY)) {
            // Start complex on new line if empty? For arrays we put newline after '-'
            // For objects, if it has items, we put newline and indent
            emit_yaml_newline(out, len, cap);
            emit_yaml_value(out, len, cap, cv, indent + 1);
        } else {
            emit_yaml_value(out, len, cap, cv, -1);
            emit_yaml_newline(out, len, cap);
        }
        if (!*out) return;
    }
}

static void emit_yaml_array(char **out, size_t *len, size_t *cap, const component_value_t *v, int indent) {
    for (size_t i = 0; i < v->array_val.count; i++) {
        emit_yaml_indent(out, len, cap, indent);
        if (!*out) return;
        emit_yaml_str(out, len, cap, "- ");
        const component_value_t *elem = &v->array_val.values[i];
        if (elem->type == COMPONENT_TYPE_OBJECT) {
            emit_yaml_newline(out, len, cap);
            emit_yaml_value(out, len, cap, elem, indent + 1);
        } else if (elem->type == COMPONENT_TYPE_ARRAY) {
            emit_yaml_newline(out, len, cap);
            emit_yaml_array(out, len, cap, elem, indent + 1);
        } else {
            emit_yaml_value(out, len, cap, elem, -1);
            emit_yaml_newline(out, len, cap);
        }
        if (!*out) return;
    }
}

static void emit_yaml_scalar(char **out, size_t *len, size_t *cap, const component_value_t *v) {
    char buf[128];
    switch (v ? v->type : COMPONENT_TYPE_NULL) {
        case COMPONENT_TYPE_NULL:
            emit_yaml_str(out, len, cap, "null");
            break;
        case COMPONENT_TYPE_BOOL:
            emit_yaml_str(out, len, cap, v->bool_val ? "true" : "false");
            break;
        case COMPONENT_TYPE_INT:
            snprintf(buf, sizeof buf, "%lld", (long long)v->int_val);
            emit_yaml_str(out, len, cap, buf);
            break;
        case COMPONENT_TYPE_FLOAT: {
            // Use %g formatting
            snprintf(buf, sizeof buf, "%g", v->float_val);
            emit_yaml_str(out, len, cap, buf);
            break;
        }
        case COMPONENT_TYPE_STRING: {
            // Quote string
            emit_yaml_str(out, len, cap, "\"");
            emit_yaml_str(out, len, cap, v->string_val ? v->string_val : "");
            emit_yaml_str(out, len, cap, "\"");
            break;
        }
        default:
            emit_yaml_str(out, len, cap, "null");
            break;
    }
}

static void emit_yaml_value(char **out, size_t *len, size_t *cap, const component_value_t *v, int indent) {
    if (!v) { emit_yaml_str(out, len, cap, "null"); return; }
    switch (v->type) {
        case COMPONENT_TYPE_OBJECT:
            emit_yaml_object(out, len, cap, v, indent < 0 ? 0 : indent);
            break;
        case COMPONENT_TYPE_ARRAY:
            emit_yaml_array(out, len, cap, v, indent < 0 ? 0 : indent);
            break;
        default:
            emit_yaml_scalar(out, len, cap, v);
            break;
    }
}

char* scene_to_yaml(const scene_t* scene) {
    if (!scene) return NULL;
    size_t cap = 2048, len = 0;
    char *out = (char*)malloc(cap);
    if (!out) return NULL;
    out[0] = '\0';

    // Metadata
    emit_yaml_str(&out, &len, &cap, "metadata:\n");
    emit_yaml_str(&out, &len, &cap, "  name: "); emit_yaml_str(&out, &len, &cap, scene->metadata.name ? scene->metadata.name : ""); emit_yaml_newline(&out, &len, &cap);
    emit_yaml_str(&out, &len, &cap, "  version: "); emit_yaml_str(&out, &len, &cap, scene->metadata.version ? scene->metadata.version : "0.0.0"); emit_yaml_newline(&out, &len, &cap);
    if (scene->metadata.author) { emit_yaml_str(&out, &len, &cap, "  author: "); emit_yaml_str(&out, &len, &cap, scene->metadata.author); emit_yaml_newline(&out, &len, &cap); }
    if (scene->metadata.description) { emit_yaml_str(&out, &len, &cap, "  description: "); emit_yaml_str(&out, &len, &cap, scene->metadata.description); emit_yaml_newline(&out, &len, &cap); }

    // Entities
    emit_yaml_str(&out, &len, &cap, "entities:\n");
    for (size_t i = 0; i < scene->entities_count; i++) {
        const entity_t *e = &scene->entities[i];
        emit_yaml_str(&out, &len, &cap, "  "); emit_yaml_str(&out, &len, &cap, e->name ? e->name : "Entity"); emit_yaml_str(&out, &len, &cap, ":\n");
        // _meta minimal (skip unless present)
        if (e->meta && (e->meta->description || e->meta->author || e->meta->notes_count || e->meta->deprecated || e->meta->version || e->meta->custom_fields_count)) {
            emit_yaml_str(&out, &len, &cap, "    _meta:\n");
            if (e->meta->description) { emit_yaml_str(&out, &len, &cap, "      description: \""); emit_yaml_str(&out, &len, &cap, e->meta->description); emit_yaml_str(&out, &len, &cap, "\"\n"); }
            if (e->meta->author) { emit_yaml_str(&out, &len, &cap, "      author: \""); emit_yaml_str(&out, &len, &cap, e->meta->author); emit_yaml_str(&out, &len, &cap, "\"\n"); }
            if (e->meta->notes_count) {
                emit_yaml_str(&out, &len, &cap, "      notes:\n");
                for (size_t ni=0; ni<e->meta->notes_count; ni++) {
                    emit_yaml_str(&out, &len, &cap, "        - \""); emit_yaml_str(&out, &len, &cap, e->meta->notes[ni]); emit_yaml_str(&out, &len, &cap, "\"\n");
                }
            }
            if (e->meta->deprecated) { emit_yaml_str(&out, &len, &cap, "      deprecated: true\n"); }
            if (e->meta->version) { char b[64]; snprintf(b,sizeof b,"%d", e->meta->version); emit_yaml_str(&out, &len, &cap, "      version: "); emit_yaml_str(&out, &len, &cap, b); emit_yaml_newline(&out,&len,&cap); }
            for (size_t ci=0; ci<e->meta->custom_fields_count; ci++) {
                emit_yaml_str(&out, &len, &cap, "      "); emit_yaml_str(&out, &len, &cap, e->meta->custom_fields[ci].key);
                emit_yaml_str(&out, &len, &cap, ": \""); emit_yaml_str(&out, &len, &cap, e->meta->custom_fields[ci].value); emit_yaml_str(&out, &len, &cap, "\"\n");
            }
        }
        if (e->prefab && e->prefab[0]) { emit_yaml_str(&out, &len, &cap, "    prefab: "); emit_yaml_str(&out, &len, &cap, e->prefab); emit_yaml_newline(&out, &len, &cap); }
        if (!e->enabled) { emit_yaml_str(&out, &len, &cap, "    enabled: false\n"); }
        if (e->tags_count) {
            emit_yaml_str(&out, &len, &cap, "    tags:\n");
            for (size_t ti=0; ti<e->tags_count; ti++) {
                emit_yaml_str(&out, &len, &cap, "      - "); emit_yaml_str(&out, &len, &cap, e->tags[ti] ? e->tags[ti] : ""); emit_yaml_newline(&out, &len, &cap);
            }
        }
        if (e->components_count) {
            emit_yaml_str(&out, &len, &cap, "    components:\n");
            for (size_t ci=0; ci<e->components_count; ci++) {
                const component_t *c = &e->components[ci];
                emit_yaml_str(&out, &len, &cap, "      "); emit_yaml_str(&out, &len, &cap, c->type_name ? c->type_name : "Component"); emit_yaml_str(&out, &len, &cap, ": ");
                if (c->data.type == COMPONENT_TYPE_OBJECT || c->data.type == COMPONENT_TYPE_ARRAY) {
                    emit_yaml_newline(&out, &len, &cap);
                    emit_yaml_value(&out, &len, &cap, &c->data, 4);
                } else {
                    emit_yaml_value(&out, &len, &cap, &c->data, -1);
                    emit_yaml_newline(&out, &len, &cap);
                }
            }
        }
    }

    // Hierarchy (flat relations)
    if (scene->hierarchy_relations_count) {
        emit_yaml_str(&out, &len, &cap, "hierarchy:\n");
        emit_yaml_str(&out, &len, &cap, "  relations:\n");
        for (size_t i=0;i<scene->hierarchy_relations_count;i++) {
            const parent_child_relation_t *r = &scene->hierarchy_relations[i];
            emit_yaml_str(&out, &len, &cap, "    - parent: "); emit_yaml_str(&out, &len, &cap, r->parent ? r->parent : ""); emit_yaml_newline(&out, &len, &cap);
            emit_yaml_str(&out, &len, &cap, "      child: "); emit_yaml_str(&out, &len, &cap, r->child ? r->child : ""); emit_yaml_newline(&out, &len, &cap);
            if (r->order) { char b[64]; snprintf(b,sizeof b, "%d", r->order); emit_yaml_str(&out, &len, &cap, "      order: "); emit_yaml_str(&out, &len, &cap, b); emit_yaml_newline(&out, &len, &cap); }
        }
    }

    // Relationships -> constraints
    if (scene->constraints.joints_count) {
        emit_yaml_str(&out, &len, &cap, "relationships:\n");
        emit_yaml_str(&out, &len, &cap, "  constraints:\n");
        emit_yaml_str(&out, &len, &cap, "    joints:\n");
        for (size_t i=0;i<scene->constraints.joints_count;i++) {
            const joint_constraint_t *j = &scene->constraints.joints[i];
            emit_yaml_str(&out, &len, &cap, "      - type: "); emit_yaml_str(&out, &len, &cap, j->type ? j->type : ""); emit_yaml_newline(&out, &len, &cap);
            if (j->entity_a) { emit_yaml_str(&out, &len, &cap, "        entity_a: "); emit_yaml_str(&out, &len, &cap, j->entity_a); emit_yaml_newline(&out, &len, &cap); }
            if (j->entity_b) { emit_yaml_str(&out, &len, &cap, "        entity_b: "); emit_yaml_str(&out, &len, &cap, j->entity_b); emit_yaml_newline(&out, &len, &cap); }
        }
    }

    return out;
}

// ---- Hierarchy parsing (tree format) ----
static bool parse_hierarchy_tree_map(parse_context_t* ctx, yaml_node_t* map_node, hierarchy_node_t** out_root) {
    if (!map_node || map_node->type != YAML_MAPPING_NODE) {
        SET_ERROR(ctx, SCENE_ERR_INVALID_TYPE, "hierarchy.tree must be a mapping");
        return false;
    }
    hierarchy_node_t* synthetic_root = NULL;
    size_t roots = 0;

    for (yaml_node_pair_t* pair = map_node->data.mapping.pairs.start; pair < map_node->data.mapping.pairs.top; pair++) {
        yaml_node_t* key = yaml_document_get_node(ctx->document, pair->key);
        yaml_node_t* val = yaml_document_get_node(ctx->document, pair->value);
        const char* key_name = get_scalar_value(ctx->document, key);
        if (!key_name) continue;

        hierarchy_node_t* root_child = hierarchy_create_node(key_name, NULL);
        if (!root_child) { SET_ERROR(ctx, SCENE_ERR_MEMORY, "Out of memory"); return false; }

        if (val && val->type == YAML_SEQUENCE_NODE) {
            if (!parse_hierarchy_children_seq(ctx, val, root_child)) return false;
        }

        if (!synthetic_root && roots == 0) {
            // Single root: return it directly
            *out_root = root_child;
        } else if (roots == 1 && !synthetic_root) {
            // Need to create a synthetic root to hold multiple top-level roots
            synthetic_root = hierarchy_create_node("__ROOT__", NULL);
            if (!synthetic_root) { SET_ERROR(ctx, SCENE_ERR_MEMORY, "Out of memory"); return false; }
            // Move previously set out_root under synthetic_root
            hierarchy_append_child(synthetic_root, *out_root);
            (*out_root)->parent = synthetic_root;
            // Also add the new root_child
            hierarchy_append_child(synthetic_root, root_child);
            root_child->parent = synthetic_root;
            *out_root = synthetic_root;
        } else if (synthetic_root) {
            hierarchy_append_child(synthetic_root, root_child);
            root_child->parent = synthetic_root;
        }
        roots++;
    }
    if (roots == 0) {
        // empty tree is fine
        *out_root = NULL;
    }
    return true;
}

static bool parse_hierarchy_children_seq(parse_context_t* ctx, yaml_node_t* seq_node, hierarchy_node_t* parent) {
    if (!seq_node || seq_node->type != YAML_SEQUENCE_NODE) return true;
    for (yaml_node_item_t* item = seq_node->data.sequence.items.start; item < seq_node->data.sequence.items.top; item++) {
        yaml_node_t* n = yaml_document_get_node(ctx->document, *item);
        if (!n) continue;
        if (n->type == YAML_SCALAR_NODE) {
            const char* child_name = get_scalar_value(ctx->document, n);
            hierarchy_node_t* child = hierarchy_create_node(child_name, parent);
            if (!child) { SET_ERROR(ctx, SCENE_ERR_MEMORY, "Out of memory"); return false; }
            hierarchy_append_child(parent, child);
        } else if (n->type == YAML_MAPPING_NODE) {
            // Expect single entry: name -> sequence(children)
            for (yaml_node_pair_t* p = n->data.mapping.pairs.start; p < n->data.mapping.pairs.top; p++) {
                yaml_node_t* k = yaml_document_get_node(ctx->document, p->key);
                yaml_node_t* v = yaml_document_get_node(ctx->document, p->value);
                const char* name = get_scalar_value(ctx->document, k);
                hierarchy_node_t* child = hierarchy_create_node(name, parent);
                if (!child) { SET_ERROR(ctx, SCENE_ERR_MEMORY, "Out of memory"); return false; }
                hierarchy_append_child(parent, child);
                if (v && v->type == YAML_SEQUENCE_NODE) {
                    if (!parse_hierarchy_children_seq(ctx, v, child)) return false;
                }
            }
        }
    }
    return true;
}
