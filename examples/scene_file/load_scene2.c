#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <yaml.h>
#include <jansson.h> // For JSON schema validation if needed

// Structure definitions
typedef struct {
    char* name;
    char* version;
    char* author;
    char* description;
} SceneMetadata;

typedef struct {
    char* path;
    char* namespace;
} SceneInclude;

typedef struct {
    char* name;
    json_t* schema;
} ComponentSchema;

typedef struct {
    char* name;
    json_t* meta;
    char* prefab;
    char** tags;
    size_t tag_count;
    json_t* components;
    json_t* properties;
    int enabled;
} Entity;

typedef struct {
    char* parent;
    char* child;
    int order;
} ParentChildRelation;

typedef struct {
    char* from;
    char* to;
    double weight;
    int bidirectional;
    char* type;
    json_t* meta;
} RelationshipEdge;

typedef struct {
    char* name;
    char* extends;
    int abstract;
    json_t* components;
    char** tags;
    size_t tag_count;
    json_t* properties;
    json_t* meta;
} Prefab;

typedef struct {
    char* entity;
    double wait_time;
    double speed;
    json_t* meta;
} Waypoint;

typedef struct {
    char* name;
    int enabled;
    int priority;
    json_t* when;
    json_t* then;
    json_t* else_;
    json_t* meta;
} Rule;

typedef struct {
    SceneMetadata metadata;
    SceneInclude* includes;
    size_t include_count;
    ComponentSchema* schemas;
    size_t schema_count;
    Entity* entities;
    size_t entity_count;
    json_t* hierarchy_tree;
    ParentChildRelation* hierarchy_relations;
    size_t hierarchy_relation_count;
    RelationshipEdge* relationship_edges;
    size_t relationship_edge_count;
    Prefab* prefabs;
    size_t prefab_count;
    json_t* sequences;
    json_t* constraints;
    Rule* rules;
    size_t rule_count;
    json_t* systems;
} Scene;

// Function prototypes
Scene* scene_load(const char* filename);
void scene_free(Scene* scene);
int scene_validate(const Scene* scene, const char* schema_path);
void print_scene_info(const Scene* scene);

// Helper functions
char* yaml_value_to_string(yaml_event_t* event);
json_t* yaml_value_to_json(yaml_event_t* event);
int parse_metadata(yaml_parser_t* parser, SceneMetadata* metadata);
int parse_includes(yaml_parser_t* parser, SceneInclude** includes, size_t* count);
int parse_entities(yaml_parser_t* parser, Entity** entities, size_t* count);

int main(int argc, char* argv[]) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <scene_file.yaml>\n", argv[0]);
        return 1;
    }

    Scene* scene = scene_load(argv[1]);
    if (!scene) {
        fprintf(stderr, "Failed to load scene\n");
        return 1;
    }

    // Validate against schema (if provided)
    if (scene_validate(scene, "scene_schema.json")) {
        fprintf(stderr, "Scene validation failed\n");
        scene_free(scene);
        return 1;
    }

    print_scene_info(scene);
    scene_free(scene);

    return 0;
}

Scene* scene_load(const char* filename) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    yaml_parser_t parser;
    if (!yaml_parser_initialize(&parser)) {
        fclose(file);
        fprintf(stderr, "Failed to initialize YAML parser\n");
        return NULL;
    }

    yaml_parser_set_input_file(&parser, file);

    Scene* scene = calloc(1, sizeof(Scene));
    if (!scene) {
        fclose(file);
        yaml_parser_delete(&parser);
        return NULL;
    }

    yaml_event_t event;
    int done = 0;
    int in_section = 0;
    char* current_section = NULL;

    while (!done) {
        if (!yaml_parser_parse(&parser, &event)) {
            fprintf(stderr, "Parser error %d\n", parser.error);
            break;
        }

        switch (event.type) {
            case YAML_STREAM_START_EVENT:
                break;
            case YAML_DOCUMENT_START_EVENT:
                break;
            case YAML_MAPPING_START_EVENT:
                if (!current_section) {
                    // Top-level mapping
                } else if (strcmp(current_section, "metadata") == 0) {
                    parse_metadata(&parser, &scene->metadata);
                }
                break;
            case YAML_SCALAR_EVENT:
                {
                    char* value = yaml_value_to_string(&event);
                    if (!current_section) {
                        // This is a section name
                        if (strcmp(value, "metadata") == 0) {
                            current_section = "metadata";
                        } else if (strcmp(value, "includes") == 0) {
                            current_section = "includes";
                            parse_includes(&parser, &scene->includes, &scene->include_count);
                        } else if (strcmp(value, "entities") == 0) {
                            current_section = "entities";
                            parse_entities(&parser, &scene->entities, &scene->entity_count);
                        }
                        // Handle other sections similarly...
                    }
                    free(value);
                }
                break;
            case YAML_SEQUENCE_START_EVENT:
                break;
            case YAML_MAPPING_END_EVENT:
                if (current_section) {
                    current_section = NULL;
                }
                break;
            case YAML_STREAM_END_EVENT:
                done = 1;
                break;
            default:
                break;
        }

        yaml_event_delete(&event);
    }

    fclose(file);
    yaml_parser_delete(&parser);
    return scene;
}

void scene_free(Scene* scene) {
    if (!scene) return;

    free(scene->metadata.name);
    free(scene->metadata.version);
    free(scene->metadata.author);
    free(scene->metadata.description);

    for (size_t i = 0; i < scene->include_count; i++) {
        free(scene->includes[i].path);
        free(scene->includes[i].namespace);
    }
    free(scene->includes);

    for (size_t i = 0; i < scene->schema_count; i++) {
        free(scene->schemas[i].name);
        json_decref(scene->schemas[i].schema);
    }
    free(scene->schemas);

    for (size_t i = 0; i < scene->entity_count; i++) {
        Entity* e = &scene->entities[i];
        free(e->name);
        json_decref(e->meta);
        free(e->prefab);

        for (size_t j = 0; j < e->tag_count; j++) {
            free(e->tags[j]);
        }
        free(e->tags);

        json_decref(e->components);
        json_decref(e->properties);
    }
    free(scene->entities);

    // Free other allocated resources similarly...

    free(scene);
}

int scene_validate(const Scene* scene, const char* schema_path) {
    // Load JSON schema
    json_error_t error;
    json_t* schema_json = json_load_file(schema_path, 0, &error);
    if (!schema_json) {
        fprintf(stderr, "Schema error: %s\n", error.text);
        return 1;
    }

    // Convert scene to JSON for validation
    json_t* scene_json = json_object();

    // Add metadata
    json_t* metadata = json_object();
    json_object_set_new(metadata, "name", json_string(scene->metadata.name));
    json_object_set_new(metadata, "version", json_string(scene->metadata.version));
    if (scene->metadata.author) {
        json_object_set_new(metadata, "author", json_string(scene->metadata.author));
    }
    if (scene->metadata.description) {
        json_object_set_new(metadata, "description", json_string(scene->metadata.description));
    }
    json_object_set_new(scene_json, "metadata", metadata);

    // Add includes
    json_t* includes = json_array();
    for (size_t i = 0; i < scene->include_count; i++) {
        json_t* include = json_object();
        json_object_set_new(include, "path", json_string(scene->includes[i].path));
        if (scene->includes[i].namespace) {
            json_object_set_new(include, "namespace", json_string(scene->includes[i].namespace));
        }
        json_array_append_new(includes, include);
    }
    json_object_set_new(scene_json, "includes", includes);

    // Add entities
    json_t* entities = json_object();
    for (size_t i = 0; i < scene->entity_count; i++) {
        const Entity* e = &scene->entities[i];
        json_t* entity = json_object();

        if (e->meta) {
            json_object_set_new(entity, "_meta", json_deep_copy(e->meta));
        }

        if (e->prefab) {
            json_object_set_new(entity, "prefab", json_string(e->prefab));
        }

        if (e->tag_count > 0) {
            json_t* tags = json_array();
            for (size_t j = 0; j < e->tag_count; j++) {
                json_array_append_new(tags, json_string(e->tags[j]));
            }
            json_object_set_new(entity, "tags", tags);
        }

        if (e->components) {
            json_object_set_new(entity, "components", json_deep_copy(e->components));
        }

        if (e->properties) {
            json_object_set_new(entity, "properties", json_deep_copy(e->properties));
        }

        json_object_set_new(entity, "enabled", json_boolean(e->enabled));
        json_object_set_new(entities, e->name, entity);
    }
    json_object_set_new(scene_json, "entities", entities);

    // TODO: Add other sections to scene_json

    // Validate against schema (pseudo-code - would need a JSON schema validator)
    /*
    if (!json_validate(schema_json, scene_json)) {
        fprintf(stderr, "Validation failed\n");
        json_decref(schema_json);
        json_decref(scene_json);
        return 1;
    }
    */

    json_decref(schema_json);
    json_decref(scene_json);
    return 0;
}

void print_scene_info(const Scene* scene) {
    printf("Scene: %s (v%s)\n", scene->metadata.name, scene->metadata.version);
    if (scene->metadata.description) {
        printf("Description: %s\n", scene->metadata.description);
    }

    printf("\nIncludes (%zu):\n", scene->include_count);
    for (size_t i = 0; i < scene->include_count; i++) {
        printf("  - %s", scene->includes[i].path);
        if (scene->includes[i].namespace) {
            printf(" (namespace: %s)", scene->includes[i].namespace);
        }
        printf("\n");
    }

    printf("\nEntities (%zu):\n", scene->entity_count);
    for (size_t i = 0; i < scene->entity_count; i++) {
        const Entity* e = &scene->entities[i];
        printf("  - %s", e->name);
        if (e->prefab) {
            printf(" (prefab: %s)", e->prefab);
        }
        printf("\n");

        if (e->tag_count > 0) {
            printf("    Tags: ");
            for (size_t j = 0; j < e->tag_count; j++) {
                printf("%s%s", j > 0 ? ", " : "", e->tags[j]);
            }
            printf("\n");
        }
    }
}

// Helper function implementations
char* yaml_value_to_string(yaml_event_t* event) {
    if (event->type != YAML_SCALAR_EVENT) return NULL;

    size_t len = event->data.scalar.length;
    char* str = malloc(len + 1);
    if (!str) return NULL;

    memcpy(str, event->data.scalar.value, len);
    str[len] = '\0';
    return str;
}

json_t* yaml_value_to_json(yaml_event_t* event) {
    if (event->type != YAML_SCALAR_EVENT) return NULL;

    // Try to parse as number
    char* endptr;
    char* value = yaml_value_to_string(event);
    if (!value) return NULL;

    double num = strtod(value, &endptr);
    if (*endptr == '\0') {
        free(value);
        return json_real(num);
    }

    // Try to parse as boolean
    if (strcmp(value, "true") == 0) {
        free(value);
        return json_true();
    }
    if (strcmp(value, "false") == 0) {
        free(value);
        return json_false();
    }
    if (strcmp(value, "null") == 0) {
        free(value);
        return json_null();
    }

    // Default to string
    json_t* result = json_string(value);
    free(value);
    return result;
}

int parse_metadata(yaml_parser_t* parser, SceneMetadata* metadata) {
    yaml_event_t event;
    char* key = NULL;
    int done = 0;

    while (!done) {
        if (!yaml_parser_parse(parser, &event)) {
            return 0;
        }

        switch (event.type) {
            case YAML_SCALAR_EVENT:
                if (!key) {
                    key = yaml_value_to_string(&event);
                } else {
                    char* value = yaml_value_to_string(&event);
                    if (strcmp(key, "name") == 0) {
                        metadata->name = value;
                    } else if (strcmp(key, "version") == 0) {
                        metadata->version = value;
                    } else if (strcmp(key, "author") == 0) {
                        metadata->author = value;
                    } else if (strcmp(key, "description") == 0) {
                        metadata->description = value;
                    } else {
                        free(value);
                    }
                    free(key);
                    key = NULL;
                }
                break;
            case YAML_MAPPING_END_EVENT:
                done = 1;
                break;
            default:
                break;
        }

        yaml_event_delete(&event);
    }

    return 1;
}

// Similar implementations for parse_includes, parse_entities, etc.
