#include "scene_model.hpp"
#include <flecs.h>
#include <string>
#include <vector>
#include <unordered_map>

namespace amg {

static const char* find_parent_of(const std::vector<ParentChildRelation> &rels, const std::string &child) {
  for (auto &r : rels) if (r.child == child) return r.parent.c_str();
  return nullptr;
}

void populate_world_from_scene(ecs_world_t *world, const Scene &s) {
  // Pre-create entities by name
  for (auto &e : s.entities) {
    if (e.name.empty()) continue;
    ecs_entity_desc_t ed{}; ed.name = e.name.c_str(); (void)ecs_entity_init(world, &ed);
  }
  // Tags referenced commonly
  const char* tags[] = { "Controllable", "Persistent", "EntityGroup" };
  for (auto *t : tags) { ecs_entity_desc_t ed{}; ed.name = t; (void)ecs_entity_init(world, &ed); }
  // Prefabs
  for (auto &e : s.entities) {
    if (!e.prefab.empty()) { ecs_entity_desc_t ed{}; ed.name = e.prefab.c_str(); ecs_entity_t p = ecs_entity_init(world, &ed); ecs_add_id(world, p, EcsPrefab); }
  }
  // Apply simple pairs and names via JSON per-entity (let Flecs resolve components/tags/pairs)
  for (auto &e : s.entities) {
    ecs_entity_t id = ecs_lookup(world, e.name.c_str());
    if (!id) { ecs_entity_desc_t ed{}; ed.name = e.name.c_str(); id = ecs_entity_init(world, &ed); }
    // Build minimal JSON for Remote API
    std::string json; json.reserve(512);
    json += "{";
    // parent pair (ChildOf)
    if (auto p = find_parent_of(s.hierarchy_relations, e.name)) {
      json += "\"pairs\":{";
      json += "\"ChildOf\":"; json += "\""; json += p; json += "\"";
      if (!e.prefab.empty()) { json += ",\"IsA\":\""; json += e.prefab; json += "\""; }
      json += "}";
    } else if (!e.prefab.empty()) {
      json += "\"pairs\":{"; json += "\"IsA\":\""; json += e.prefab; json += "\"}";
    }
    // name and tags/components are optional here; components without meta won't be applied
    json += "}";
    (void)ecs_entity_from_json(world, id, json.c_str(), nullptr);
  }
}

} // namespace amg

