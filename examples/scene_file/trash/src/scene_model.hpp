#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <variant>
#include <optional>
#include <cstdint>
#include <unordered_map>

namespace amg {

struct MetaKV {
  std::string key;
  std::string value;
};

struct SceneMeta {
  std::string description;
  std::vector<std::string> notes;
  std::string author;
  std::string modified;
  std::string todo;
  bool deprecated{false};
  int version{0};
  std::vector<MetaKV> custom;
};

struct ComponentValue;
using Array = std::vector<ComponentValue>;
struct ObjectItem { std::string key; ComponentValue* value; };
struct Object { std::vector<ObjectItem> items; };

struct ComponentValue {
  // Simple tagged union using std::variant for safety
  using Variant = std::variant<std::monostate, bool, std::int64_t, double, std::string, Array, Object>;
  Variant v;

  static ComponentValue null() { return ComponentValue{std::monostate{}}; }
  static ComponentValue boolean(bool b) { return ComponentValue{b}; }
  static ComponentValue integer(std::int64_t i) { return ComponentValue{i}; }
  static ComponentValue number(double d) { return ComponentValue{d}; }
  static ComponentValue string(std::string s) { return ComponentValue{std::move(s)}; }
  static ComponentValue array(Array a) { return ComponentValue{std::move(a)}; }
  static ComponentValue object(Object o) { return ComponentValue{std::move(o)}; }
};

struct ComponentInst {
  std::string type_name;
  ComponentValue value;
};

struct Entity {
  std::string name;
  std::optional<SceneMeta> meta;
  std::string prefab; // optional
  std::vector<std::string> tags;
  std::vector<ComponentInst> components;
  bool enabled{true};
};

struct ParentChildRelation { std::string parent; std::string child; int order{0}; };

struct JointConstraint { std::string type; std::string entity_a; std::string entity_b; };

struct Constraints { std::vector<JointConstraint> joints; };

struct SceneHeader { std::string name; std::string version; std::string author; std::string description; };

struct Scene {
  SceneHeader metadata;
  std::vector<Entity> entities;
  std::vector<ParentChildRelation> hierarchy_relations;
  Constraints constraints;
};

} // namespace amg

