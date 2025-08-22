// tests/test_scene.c
#include "scene_loader.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <assert.h>

static const char* TEST_YAML =
"metadata:\n"
"  name: TestScene\n"
"  version: 1.2.3\n"
"entities:\n"
"  Player:\n"
"    _meta:\n"
"      description: Player entity for tests\n"
"    tags: [Controllable, TestTag]\n"
"    components:\n"
"      Transform: { x: 1, y: 2, z: 3 }\n"
"      Inventory:\n"
"        items: [Sword, Shield, Potion]\n"
"        capacity: 10\n"
"      Nested:\n"
"        obj:\n"
"          inner: { a: true, b: 42, c: 3.14, d: [1, 2, 3] }\n"
"    enabled: true\n";

static char* write_temp_yaml(void) {
    char template[] = "/tmp/test_scene_XXXXXX.yaml";
    int fd = mkstemps(template, 5); // suffix ".yaml" has 5 chars
    if (fd == -1) {
        perror("mkstemps");
        return NULL;
    }
    size_t len = strlen(TEST_YAML);
    if (write(fd, TEST_YAML, len) != (ssize_t)len) {
        perror("write");
        close(fd);
        unlink(template);
        return NULL;
    }
    close(fd);
    return strdup(template);
}

int main(void) {
    char* path = write_temp_yaml();
    if (!path) {
        fprintf(stderr, "Failed to create temp yaml file\n");
        return 1;
    }

    scene_error_info_t err = {0};
    scene_t* scene = scene_load(path, &err);
    unlink(path);
    free(path);

    if (!scene) {
        fprintf(stderr, "scene_load failed: %s\n", err.message);
        return 2;
    }

    // Basic validations
    assert(scene->metadata.name && strcmp(scene->metadata.name, "TestScene") == 0);
    assert(scene->entities_count == 1);
    entity_t* player = scene_find_entity(scene, "Player");
    assert(player != NULL);
    assert(player->tags_count == 2);
    assert(entity_has_tag(player, "Controllable"));

    component_t* tf = entity_find_component(player, "Transform");
    assert(tf && tf->data.type == COMPONENT_TYPE_OBJECT);

    component_t* inv = entity_find_component(player, "Inventory");
    assert(inv && inv->data.type == COMPONENT_TYPE_OBJECT);

    component_t* nested = entity_find_component(player, "Nested");
    assert(nested && nested->data.type == COMPONENT_TYPE_OBJECT);

    // We won't introspect deep values here; purpose is parse and free coverage

    scene_free(scene);
    printf("OK\n");
    return 0;
}

