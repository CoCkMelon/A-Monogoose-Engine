#include "scene_model.hpp"
#include <yaml-cpp/yaml.h>
#include <flecs.h>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>

namespace amg { struct SceneIO; }

using namespace amg;

// Forward decl from scene_yaml.cpp
namespace amg { struct SceneIO { static Scene loadFile(const std::string &path); static std::string toYamlString(const Scene &s); }; }

static std::string read_file(const std::string &p) { std::ifstream ifs(p); std::ostringstream ss; ss << ifs.rdbuf(); return ss.str(); }
static void write_file(const std::string &p, const std::string &data) { std::ofstream ofs(p); ofs << data; }

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " <scene.yaml> [--out OUT.yaml] [--compare] [--dump-world-json]\n";
    return 1;
  }
  std::string input = argv[1];
  std::string out_path; bool compare=false; bool dump_world=false;
  for (int i=2;i<argc;i++) {
    if (std::strcmp(argv[i], "--out")==0 && i+1<argc) { out_path = argv[++i]; }
    else if (std::strcmp(argv[i], "--compare")==0) compare = true;
    else if (std::strcmp(argv[i], "--dump-world-json")==0) dump_world = true;
  }

  Scene scene;
  try { scene = SceneIO::loadFile(input); }
  catch (const std::exception &e) { std::cerr << "Load failed: " << e.what() << "\n"; return 2; }

  ecs_world_t *world = ecs_init();
  if (!world) { std::cerr << "Failed to init Flecs world\n"; return 3; }

  // Minimal population (hierarchy pairs & prefabs)
  extern void populate_world_from_scene(ecs_world_t*, const Scene&);
  populate_world_from_scene(world, scene);

  if (dump_world) {
    ecs_world_to_json_desc_t d{}; char *wj = ecs_world_to_json(world, &d);
    if (wj) { std::cout << wj << std::endl; ecs_os_free(wj); }
  }

  // Roundtrip YAML (normalize with our serializer)
  std::string normalized = SceneIO::toYamlString(scene);
  if (!out_path.empty()) write_file(out_path, normalized);
  else std::cout << normalized << std::endl;

  int rc = 0;
  if (compare) {
    std::string original = read_file(input);
    if (original == normalized) {
      std::cerr << "YAML roundtrip: identical\n";
    } else {
      std::cerr << "YAML roundtrip: differs\n";
      rc = 10;
    }
  }

  ecs_fini(world);
  return rc;
}

