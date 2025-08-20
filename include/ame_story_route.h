#ifndef AME_STORY_ROUTE_H
#define AME_STORY_ROUTE_H

#include <stddef.h>
#include <stdbool.h>
#include "ame_dialogue.h"

#ifdef __cplusplus
extern "C" {
#endif

// Edge from one story node to another via a choice label
typedef struct AmeStoryEdge {
    const char *choice;   // user-visible label for this branch (optional)
    const char *to;       // id of target node
} AmeStoryEdge;

// Node in the story graph, referencing a dialogue scene and optional entry label
typedef struct AmeStoryNode {
    const char *id;           // node id (unique within route)
    const char *scene;        // dialogue scene name to play
    const char *entry;        // optional entry line id within the scene (NULL to start from beginning)

    const AmeStoryEdge *edges;
    size_t edge_count;
} AmeStoryNode;

// A complete story route (graph)
typedef struct AmeStoryRoute {
    const char *name;
    const AmeStoryNode *nodes;
    size_t node_count;
} AmeStoryRoute;

// Runtime for traversing a story route
typedef struct AmeStoryRouteRuntime {
    const AmeStoryRoute *route;
    size_t node_index; // index of current node in route->nodes
} AmeStoryRouteRuntime;

// Initialize runtime at given route and optional starting node id (if NULL, uses first node).
// Returns false if route invalid or start node not found.
bool ame_story_route_start(AmeStoryRouteRuntime *rr, const AmeStoryRoute *route, const char *start_node_id);

// Get the current story node (NULL if finished/invalid)
const AmeStoryNode *ame_story_route_current(const AmeStoryRouteRuntime *rr);

// Select an outgoing edge by index and move to the target node. Returns new current node or NULL if invalid.
const AmeStoryNode *ame_story_route_select(AmeStoryRouteRuntime *rr, size_t edge_index);

// Helper: load the current node's dialogue scene from the embedded registry
const AmeDialogueScene *ame_story_route_current_scene(const AmeStoryRouteRuntime *rr);

// Embedded routes registry (implemented by generated code)
const AmeStoryRoute *ame_story_route_load_embedded(const char *name);
const char **ame_story_route_list_embedded(size_t *count);
bool ame_story_route_has_embedded(const char *name);

#ifdef __cplusplus
}
#endif

#endif // AME_STORY_ROUTE_H
