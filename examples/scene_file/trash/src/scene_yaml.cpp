#include "scene_model.hpp"
#include <yaml-cpp/yaml.h>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace amg {

static ComponentValue parseValue(const YAML::Node &n);

static SceneMeta parseMeta(const YAML::Node &n) {
  SceneMeta m;
  if (!n || !n.IsMap()) return m;
  if (auto x = n["description"]) m.description = x.as<std::string>("");
  if (auto x = n["author"]) m.author = x.as<std::string>("");
  if (auto x = n["modified"]) m.modified = x.as<std::string>("");
  if (auto x = n["todo"]) m.todo = x.as<std::string>("");
  if (auto x = n["deprecated"]) m.deprecated = x.as<bool>(false);
  if (auto x = n["version"]) m.version = x.as<int>(0);
  if (auto x = n["notes"]) {
    if (x.IsScalar()) m.notes.push_back(x.as<std::string>());
    else if (x.IsSequence()) for (auto it : x) m.notes.push_back(it.as<std::string>(""));
  }
  for (auto it : n) {
    auto key = it.first.as<std::string>("");
    if (key == "description" || key == "author" || key == "modified" || key == "todo" || key == "deprecated" || key == "version" || key == "notes") continue;
    if (it.second.IsScalar()) m.custom.push_back({key, it.second.as<std::string>("")});
  }
  return m;
}

static ComponentValue parseValue(const YAML::Node &n) {
  if (!n || n.IsNull()) return ComponentValue::null();
  if (n.IsScalar()) {
    // Try bool
    auto s = n.as<std::string>();
    if (s == "true") return ComponentValue::boolean(true);
    if (s == "false") return ComponentValue::boolean(false);
    // Try int
    char *end=nullptr; long long v = std::strtoll(s.c_str(), &end, 10);
    if (end && *end == '\0') return ComponentValue::integer(v);
    // Try float
    char *end2=nullptr; double d = std::strtod(s.c_str(), &end2);
    if (end2 && *end2 == '\0') return ComponentValue::number(d);
    return ComponentValue::string(std::move(s));
  }
  if (n.IsSequence()) {
    Array arr; arr.reserve(n.size());
    for (auto it : n) arr.push_back(parseValue(it));
    return ComponentValue::array(std::move(arr));
  }
  if (n.IsMap()) {
    Object obj; obj.items.reserve(n.size());
    for (auto it : n) {
      auto key = it.first.as<std::string>("");
      auto v = new ComponentValue(parseValue(it.second));
      obj.items.push_back({std::move(key), v});
    }
    return ComponentValue::object(std::move(obj));
  }
  return ComponentValue::null();
}

static YAML::Node emitValue(const ComponentValue &v) {
  YAML::Node n;
  std::visit([&](auto&& arg){
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, std::monostate>) {
      n = YAML::Node();
    } else if constexpr (std::is_same_v<T, bool>) {
      n = YAML::Node(arg);
    } else if constexpr (std::is_same_v<T, std::int64_t>) {
      n = YAML::Node(static_cast<long long>(arg));
    } else if constexpr (std::is_same_v<T, double>) {
      n = YAML::Node(arg);
    } else if constexpr (std::is_same_v<T, std::string>) {
      n = YAML::Node(arg);
    } else if constexpr (std::is_same_v<T, Array>) {
      YAML::Node seq(YAML::NodeType::Sequence);
      for (auto &el : arg) seq.push_back(emitValue(el));
      n = std::move(seq);
    } else if constexpr (std::is_same_v<T, Object>) {
      YAML::Node map(YAML::NodeType::Map);
      for (auto &it : arg.items) {
        map[it.key] = emitValue(*it.value);
      }
      n = std::move(map);
    }
  }, v.v);
  return n;
}

static void cleanupObject(Object &o) {
  for (auto &it : o.items) { delete it.value; it.value = nullptr; }
}

static void cleanupValue(ComponentValue &v) {
  std::visit([&](auto &arg){
    using T = std::decay_t<decltype(arg)>;
    if constexpr (std::is_same_v<T, Object>) {
      cleanupObject(arg);
    } else if constexpr (std::is_same_v<T, Array>) {
      for (auto &el : arg) cleanupValue(el);
    }
  }, v.v);
}

struct SceneIO {
  static Scene loadFile(const std::string &path) {
    YAML::Node root = YAML::LoadFile(path);
    return loadNode(root);
  }

  static Scene loadNode(const YAML::Node &root) {
    Scene s;
    // metadata
    auto md = root["metadata"];
    if (!md || !md.IsMap()) throw std::runtime_error("metadata missing or invalid");
    s.metadata.name = md["name"].as<std::string>("");
    s.metadata.version = md["version"].as<std::string>("0.0.0");
    if (auto x = md["author"]) s.metadata.author = x.as<std::string>("");
    if (auto x = md["description"]) s.metadata.description = x.as<std::string>("");

    // entities
    auto ents = root["entities"];
    if (!ents || !ents.IsMap()) throw std::runtime_error("entities missing or invalid");
    for (auto it : ents) {
      Entity e;
      e.name = it.first.as<std::string>("");
      auto body = it.second;
      if (!body.IsMap()) throw std::runtime_error("entity body must be map");
      if (auto mn = body["_meta"]) e.meta = parseMeta(mn);
      if (auto pf = body["prefab"]) e.prefab = pf.as<std::string>("");
      if (auto en = body["enabled"]) e.enabled = en.as<bool>(true);
      if (auto tags = body["tags"]) if (tags.IsSequence()) for (auto t : tags) e.tags.push_back(t.as<std::string>(""));
      if (auto comps = body["components"]) if (comps.IsMap()) {
        for (auto cit : comps) {
          ComponentInst ci;
          ci.type_name = cit.first.as<std::string>("");
          ci.value = parseValue(cit.second);
          e.components.push_back(std::move(ci));
        }
      }
      s.entities.push_back(std::move(e));
    }

    // hierarchy (optional, flat relations)
    if (auto h = root["hierarchy"]) {
      if (auto rels = h["relations"]) if (rels.IsSequence()) {
        for (auto rn : rels) {
          ParentChildRelation r;
          if (auto p = rn["parent"]) r.parent = p.as<std::string>("");
          if (auto c = rn["child"]) r.child = c.as<std::string>("");
          if (auto o = rn["order"]) r.order = o.as<int>(0);
          s.hierarchy_relations.push_back(std::move(r));
        }
      }
    }

    // relationships -> constraints -> joints
    if (auto rels = root["relationships"]) {
      if (auto cons = rels["constraints"]) {
        if (auto joints = cons["joints"]) if (joints.IsSequence()) {
          for (auto j : joints) {
            JointConstraint jc;
            if (auto t = j["type"]) jc.type = t.as<std::string>("");
            if (auto a = j["entity_a"]) jc.entity_a = a.as<std::string>("");
            if (auto b = j["entity_b"]) jc.entity_b = b.as<std::string>("");
            s.constraints.joints.push_back(std::move(jc));
          }
        }
      }
    }

    return s;
  }

  static YAML::Node toNode(const Scene &s) {
    YAML::Node root(YAML::NodeType::Map);
    // metadata
    YAML::Node md(YAML::NodeType::Map);
    md["name"] = s.metadata.name;
    md["version"] = s.metadata.version;
    if (!s.metadata.author.empty()) md["author"] = s.metadata.author;
    if (!s.metadata.description.empty()) md["description"] = s.metadata.description;
    root["metadata"] = md;

    // entities
    YAML::Node ents(YAML::NodeType::Map);
    for (auto &e : s.entities) {
      YAML::Node en(YAML::NodeType::Map);
      if (e.meta) {
        YAML::Node mn(YAML::NodeType::Map);
        if (!e.meta->description.empty()) mn["description"] = e.meta->description;
        if (!e.meta->author.empty()) mn["author"] = e.meta->author;
        if (!e.meta->modified.empty()) mn["modified"] = e.meta->modified;
        if (!e.meta->todo.empty()) mn["todo"] = e.meta->todo;
        if (e.meta->deprecated) mn["deprecated"] = true;
        if (e.meta->version) mn["version"] = e.meta->version;
        if (!e.meta->notes.empty()) {
          YAML::Node ns(YAML::NodeType::Sequence);
          for (auto &n : e.meta->notes) ns.push_back(n);
          mn["notes"] = ns;
        }
        for (auto &kv : e.meta->custom) mn[kv.key] = kv.value;
        en["_meta"] = mn;
      }
      if (!e.prefab.empty()) en["prefab"] = e.prefab;
      if (!e.enabled) en["enabled"] = false;
      if (!e.tags.empty()) {
        YAML::Node ts(YAML::NodeType::Sequence);
        for (auto &t : e.tags) ts.push_back(t);
        en["tags"] = ts;
      }
      if (!e.components.empty()) {
        YAML::Node comps(YAML::NodeType::Map);
        for (auto &c : e.components) comps[c.type_name] = emitValue(c.value);
        en["components"] = comps;
      }
      ents[e.name] = en;
    }
    root["entities"] = ents;

    if (!s.hierarchy_relations.empty()) {
      YAML::Node h(YAML::NodeType::Map);
      YAML::Node rels(YAML::NodeType::Sequence);
      for (auto &r : s.hierarchy_relations) {
        YAML::Node rn(YAML::NodeType::Map);
        rn["parent"] = r.parent; rn["child"] = r.child; if (r.order) rn["order"] = r.order;
        rels.push_back(rn);
      }
      h["relations"] = rels; root["hierarchy"] = h;
    }

    if (!s.constraints.joints.empty()) {
      YAML::Node rels(YAML::NodeType::Map);
      YAML::Node cons(YAML::NodeType::Map);
      YAML::Node joints(YAML::NodeType::Sequence);
      for (auto &j : s.constraints.joints) {
        YAML::Node jn(YAML::NodeType::Map);
        jn["type"] = j.type; if (!j.entity_a.empty()) jn["entity_a"] = j.entity_a; if (!j.entity_b.empty()) jn["entity_b"] = j.entity_b;
        joints.push_back(jn);
      }
      cons["joints"] = joints; rels["constraints"] = cons; root["relationships"] = rels;
    }

    return root;
  }

  static std::string toYamlString(const Scene &s) {
    YAML::Emitter out;
    out.SetSeqFormat(YAML::Block);
    out << toNode(s);
    return std::string(out.c_str());
  }
};

} // namespace amg

