#include "ame_story_route.h"
#include <string.h>

static const AmeStoryNode *find_node(const AmeStoryRoute *r, const char *id) {
    if (!r || !id) return NULL;
    for (size_t i=0;i<r->node_count;i++) {
        if (r->nodes[i].id && strcmp(r->nodes[i].id, id) == 0) return &r->nodes[i];
    }
    return NULL;
}

bool ame_story_route_start(AmeStoryRouteRuntime *rr, const AmeStoryRoute *route, const char *start_node_id) {
    if (!rr || !route || route->node_count == 0) return false;
    rr->route = route;
    rr->node_index = 0;
    if (start_node_id) {
        for (size_t i=0;i<route->node_count;i++) {
            if (route->nodes[i].id && strcmp(route->nodes[i].id, start_node_id) == 0) {
                rr->node_index = i;
                break;
            }
        }
    }
    return true;
}

const AmeStoryNode *ame_story_route_current(const AmeStoryRouteRuntime *rr) {
    if (!rr || !rr->route || rr->node_index >= rr->route->node_count) return NULL;
    return &rr->route->nodes[rr->node_index];
}

const AmeStoryNode *ame_story_route_select(AmeStoryRouteRuntime *rr, size_t edge_index) {
    const AmeStoryNode *cur = ame_story_route_current(rr);
    if (!cur || edge_index >= cur->edge_count) return NULL;
    const char *to = cur->edges[edge_index].to;
    if (!to) return NULL;
    // find node
    for (size_t i=0;i<rr->route->node_count;i++) {
        if (rr->route->nodes[i].id && strcmp(rr->route->nodes[i].id, to) == 0) {
            rr->node_index = i;
            return &rr->route->nodes[i];
        }
    }
    return NULL;
}

const AmeDialogueScene *ame_story_route_current_scene(const AmeStoryRouteRuntime *rr) {
    const AmeStoryNode *cur = ame_story_route_current(rr);
    if (!cur || !cur->scene) return NULL;
    return ame_dialogue_load_embedded(cur->scene);
}
