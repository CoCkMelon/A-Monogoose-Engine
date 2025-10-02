# Unity-like API Implementation Status

**A Mongoose Engine - cpp/unitylike**  
**Last Updated:** 2025-10-02

---

## Implementation Progress

| Feature | Status | Notes |
|---------|--------|-------|
| **Core** |
| Scene | ✅ Complete | Create/Destroy/Find, Step/StepFixed |
| GameObject | ✅ Complete | Name, active, tag, layer, hierarchy |
| Transform | ✅ Complete | Position, rotation, scale, world/local, helpers |
| MongooseBehaviour | ✅ Complete | All lifecycle callbacks, script hosting |
| Time | ✅ Complete | deltaTime, fixedDeltaTime, timeSinceLevelLoad |
| **Physics2D** |
| Rigidbody2D | ✅ Complete | Velocity, forces, mass, gravity, constraints |
| Collider2D | ✅ Complete | Box/Circle, size, trigger |
| Collision Callbacks | ⚠️ Partial | Declared but not hooked to Box2D listener |
| Physics2D Queries | ❌ Not Implemented | Raycast, OverlapCircle, etc. |
| **Components** |
| SpriteRenderer | ✅ Complete | Texture, UV, color, sorting |
| Material | ✅ Complete | Tint color |
| TilemapRenderer | ✅ Complete | TMX map reference |
| MeshRenderer | ✅ Complete | Vertex data pointers |
| Camera | ✅ Complete | Zoom, viewport, position |
| TextRenderer | ✅ Complete | Heap-managed text |
| AudioSource | ✅ Complete | Play/Stop, volume, spatial audio, occlusion |
| AudioListener | ✅ Complete | Volume, mute, main listener |
| **Prefabs & Scenes** |
| Prefabs (code-first) | ✅ Complete | C API register/instantiate; C++ RegisterPrefab bridge; SDL-mutex thread-safe |
| Prefab management | ⚠️ Partial | No unregister/list/iterate; no parameters/overrides |
| Scene (code-first) | ✅ Complete | Build in-memory scene, instantiate to Flecs |
| Scene serialization | ❌ Not Implemented | Save/load formats |
| SceneManager | ❌ Not Implemented | LoadScene, async, multi-scene |
| **Utilities** |
| Object.Instantiate | ❌ Not Implemented | Clone entities |
| Object.Destroy | ✅ Complete | Immediate destroy |
| Delayed Destroy | ❌ Not Implemented | |
| SceneManager | ❌ Not Implemented | LoadScene, async loading |
| Debug | ❌ Not Implemented | DrawLine, DrawRay |
| **Advanced** |
| Coroutines | ❌ Not Implemented | StartCoroutine, yield |
| OnEnable/OnDisable | ❌ Not Implemented | Component lifecycle |
| Component.enabled | ❌ Not Implemented | Per-component enable |
| Input | ❌ Not Implemented | Project-defined, use asyncinput |

---

## Detailed Status

### ✅ Fully Implemented

#### Prefabs (code-first)
- C API: ame_prefab_register, ame_prefab_instantiate
- C++ facade: unitylike::RegisterPrefab bridges std::function to C registry
- Thread safety: SDL mutex guards register/instantiate

```cpp
bool ok = unitylike::RegisterPrefab("Smoke/TwoNode", [](SceneAsset& asset, SceneAsset::Entity parent, const std::string& name){
    auto root = asset.create(name.empty()?"PrefabRoot":name).transform(0,0,0);
    if (parent.valid()) root.setParent(parent);
    return root;
});
```

#### Scene
- Create/Destroy GameObject
- Find by name (ecs_lookup)
- Step/StepFixed for script lifecycle
- Flecs world wrapper
- Script system registration

```cpp
Scene scene(world);
GameObject player = scene.Create("Player");
scene.Step(0.016f);
```

#### GameObject
- Name management with caching
- Active state via EcsDisabled tag
- Tag/layer system
- Component management (Add/Get/TryGet)
- Script hosting (AddScript<T>)
- Hierarchy (SetParent, GetParent, GetChildren)
- IsValid() check

```cpp
player.tag("Player");
player.layer(8);
player.CompareTag("Player"); // true
child.SetParent(parent, true);
```

#### Transform
- Local position/rotation/scale
- World position/rotation (computed from hierarchy)
- Translate/Rotate helpers
- right()/up() direction vectors
- eulerAngles (2D angle)
- TransformPoint/Direction
- InverseTransformPoint/Direction

```cpp
transform().Translate(glm::vec3(10, 0, 0), true);
transform().Rotate(0.1f);
glm::vec2 dir = transform().right();
```

#### Rigidbody2D
- Velocity (linear + angular)
- Body type (Dynamic/Kinematic/Static)
- Forces: AddForce, AddTorque
- Impulses: AddImpulse, AddAngularImpulse
- Properties: mass, gravityScale, drag, angularDrag
- Constraints: FreezeRotation

```cpp
rb.AddForce(glm::vec2(100, 0));
rb.AddImpulse(glm::vec2(0, 500));
rb.gravityScale(2.0f);
```

#### Collider2D
- Box/Circle type
- Size/radius
- isTrigger flag

```cpp
col.type(Collider2D::Type::Box);
col.boxSize(glm::vec2(32, 48));
```

#### MongooseBehaviour
- Lifecycle: Awake, Start, Update, FixedUpdate, LateUpdate, OnDestroy
- Collision callbacks declared (not hooked yet)
- gameObject() and transform() accessors
- Flecs system integration

```cpp
class PlayerController : public MongooseBehaviour {
    void Start() override { /* ... */ }
    void Update(float dt) override { /* ... */ }
};
player.AddScript<PlayerController>();
```

#### Rendering Components
- **SpriteRenderer:** texture, UV, size, color, sorting, z-depth
- **Material:** RGBA tint
- **TilemapRenderer:** AmeTilemap reference, layer index
- **MeshRenderer:** vertex data pointers
- **Camera:** AmeCamera wrapper, zoom, viewport
- **TextRenderer:** heap-managed text via request buffer

```cpp
auto& sprite = player.AddComponent<SpriteRenderer>();
sprite.texture(myTex);
sprite.size(glm::vec2(32, 32));
sprite.color(glm::vec4(1, 1, 1, 1));
```

#### Audio Components
- **AudioSource:** Play/Stop/Pause, volume, pitch, mute, loop, spatial audio, distance attenuation, occlusion, air absorption
- **AudioListener:** Volume control, mute, main listener system, attached to camera for spatial audio

```cpp
// Audio source with spatial audio
auto& audioSource = source.AddComponent<AudioSource>();
audioSource.InitSawWork(150.0f, 1.0f, 0.3f, 4.0f, 1.0f);
audioSource.spatialAudio(true);
audioSource.minDistance(20.0f);
audioSource.maxDistance(300.0f);
audioSource.Play();

// Audio listener on camera
auto& listener = camera.AddComponent<AudioListener>();
listener.volume(0.8f);
AudioListener::SetMain(&listener);
```

#### Time
- deltaTime()
- fixedDeltaTime()
- timeSinceLevelLoad()

```cpp
float t = Time::timeSinceLevelLoad();
```

---

### ⚠️ Partially Implemented

#### Prefab management
- No unregister/list/enumerate APIs yet
- No namespacing/versioning/metadata helpers
- No prefab parameterization/overrides

#### Collision Callbacks
**Status:** Method declarations exist in MongooseBehaviour, but Box2D contact listener not connected.

```cpp
// Declared in Scene.h
virtual void OnCollisionEnter2D(const Collision2D& collision) {}
virtual void OnTriggerEnter2D(Collider2D* other) {}
```

**What's Needed:**
1. Create `b2ContactListener` subclass
2. Query Flecs for entity IDs from `b2Body` user data
3. Lookup script hosts for entities
4. Dispatch callbacks to all scripts on entity

**Implementation Plan:**
```cpp
class UnitylikeContactListener : public b2ContactListener {
    void BeginContact(b2Contact* contact) override {
        auto* bodyA = contact->GetFixtureA()->GetBody();
        auto* bodyB = contact->GetFixtureB()->GetBody();
        
        // Get entity IDs from user data
        ecs_entity_t entityA = (ecs_entity_t)bodyA->GetUserData().pointer;
        ecs_entity_t entityB = (ecs_entity_t)bodyB->GetUserData().pointer;
        
        // Dispatch to scripts
        DispatchCollision(entityA, entityB, contact);
    }
};
```

#### Rigidbody2D Constraints
**Status:** FreezeRotation works via Box2D, but FreezePositionX/Y need custom implementation.

**What's Needed:**
- Store constraint flags in component
- In physics update system, zero out velocity components that are frozen
- Example: `if (constraints & FreezePositionX) velocity.x = 0;`

---

### ❌ Not Yet Implemented

#### Physics2D Queries
**Missing:**
- `Raycast(origin, direction, distance)` → RaycastHit
- `RaycastAll(...)` → vector<RaycastHit>
- `OverlapCircle(point, radius)` → vector<Collider2D*>
- `OverlapBox(point, size, angle)` → vector<Collider2D*>

**Implementation:**
- Wrap `ame_physics_raycast` from `ame/physics.h`
- Use Box2D's `b2World::QueryAABB` for overlap queries
- Map `b2Body*` back to `ecs_entity_t` via user data

#### Object.Instantiate
**Missing:** Clone GameObject with all components and children.

**Implementation Plan:**
1. Deep copy Flecs entity with `ecs_clone`
2. Copy all registered component data
3. Recursively clone children (walk EcsChildOf relationships)
4. Duplicate script hosts and create new script instances

```cpp
GameObject Object::Instantiate(const GameObject& original) {
    ecs_world_t* w = original.scene()->world();
    ecs_entity_t cloned = ecs_clone(w, 0, original.id(), true);
    // ... copy components, clone children, duplicate scripts
    return GameObject(original.scene(), cloned);
}
```

#### Coroutines
**Missing:** StartCoroutine, yield instructions.

**Options:**
1. **C++20 Coroutines:** Use `co_await` / `co_yield`
2. **State Machine:** Manual coroutine scheduler with frame counters
3. **Fiber Library:** Integrate Boost.Context or similar

**Example API:**
```cpp
IEnumerator FireRoutine() {
    while (true) {
        Fire();
        co_yield WaitForSeconds(0.5f);
    }
}

void Start() override {
    StartCoroutine(FireRoutine());
}
```

#### Camera Helpers
**Missing:**
- `Camera::main()` - static accessor to main camera
- `ScreenToWorldPoint(screenPos)` - unproject screen coords
- `WorldToScreenPoint(worldPos)` - project world coords

**Implementation:**
- Store static `Camera* g_main_camera` in Camera.cpp
- Implement projection math using AmeCamera zoom, viewport, position


#### SceneManager
**Missing:** LoadScene, multi-scene support.

**Plan:**
- Define scene asset format (JSON/binary with Flecs snapshot)
- Implement async loading with callbacks
- Support additive scene loading (multiple Flecs worlds or prefixing)

#### Debug Drawing
**Missing:** DrawLine, DrawRay for visual debugging.

**Plan:**
- Store debug line list with duration timers
- Render in separate debug pass
- Auto-clear lines after duration expires

---

## Architecture Notes

### Script Hosting
**Why scripts aren't ECS components:**
- MongooseBehaviour has virtual functions (non-POD)
- Flecs requires POD components (trivially copyable)
- Solution: Store ScriptHost with `std::vector<MongooseBehaviour*>` outside ECS
- Script systems iterate `g_script_entities` vector to dispatch callbacks

### Transform Hierarchy
**How it works:**
- Uses Flecs `EcsChildOf` relationship
- World transforms computed on-demand via tree traversal
- `ameComputeWorldTransform` helper accumulates parent transforms
- SetParent with `keepWorld=true` re-computes local transform

### Component Registration
**Process:**
- `ensure_components_registered()` called at Scene construction
- Registers all component IDs with Flecs once
- Components stored in global `g_comp` struct
- Templates use `if constexpr` to dispatch to correct component type

### Threading Model
- **Logic thread:** Owns Flecs world, runs Scene.Step/StepFixed
- **Render thread:** Reads components via atomic queries
- **Audio thread:** Reads AudioSource data
- **Rule:** Scripts must not mutate from non-logic threads

---

## Performance Considerations

### Component Access
- `GetComponent<T>()` does ECS lookup each call (hash map + archetype)
- **Optimization:** Cache component refs in Start(), reuse in Update()
- Example: `auto& rb = GetComponent<Rigidbody2D>();` once, not per frame

### Transform Queries
- `worldPosition()` traverses hierarchy (O(depth))
- **Optimization:** Cache world position if accessed multiple times per frame
- Consider flattening hierarchy for static objects

### Script Iteration
- All scripts iterated sequentially (single-threaded)
- **Future:** Use Flecs queries to batch scripts by archetype
- Enable parallel script execution with job system

### Entity Lookup
- `Scene.Find(name)` uses `ecs_lookup` (hash map)
- **Optimization:** Cache GameObject refs, avoid repeated Find() calls

---

## Examples

### Working Examples
- `examples/prefab_smoke` - Verifies C++ RegisterPrefab bridge and world transform
- `examples/unitylike_minimal` - Basic GameObject, Transform, MongooseBehaviour
- `examples/unitylike_box2d_car` - Rigidbody2D, forces, car physics
- `examples/unitylike_platformer_ecs` - Platformer with hierarchy, sprites
- `examples/unitylike_pixel_platformer` - Complete platformer game
- `examples/unitylike_audio_example` - Spatial audio with AudioSource/AudioListener, touch controls

### Recommended Next Example
**Port brackeysjam2025.2 to MongooseBehaviour:**
- Convert car/human C structs to MongooseBehaviour scripts
- Demonstrate tag/layer system for collision filtering
- Show Transform helpers (Translate, right/up vectors)
- Validate full API ergonomics in real game context

---

## Next Steps (Priority Order)

### 1. Collision Callbacks (High Priority)
- Implement Box2D contact listener
- Route collision events to script hosts
- Test with OnCollisionEnter2D, OnTriggerEnter2D

### 2. Physics2D Queries (High Priority)
- Wrap ame_physics_raycast
- Implement OverlapCircle/OverlapBox
- Return GameObject/Collider2D refs

### 3. Validation Example (High Priority)
- Port car controller to MongooseBehaviour
- Validate API feels "Unity-like"
- Identify ergonomic issues

### 4. Object.Instantiate (Medium Priority)
- Deep clone entities
- Duplicate script hosts
- Recursively clone children

### 5. Camera Helpers (Medium Priority)
- Camera::main() static accessor
- ScreenToWorldPoint / WorldToScreenPoint
- Simplify camera queries

### 6. Audio System Enhancements (Low Priority)
- Add pitch shifting support
- Implement audio mixing/effects
- Add doppler effect for moving sources

### 7. Prefab Management (Medium Priority)
- Add unregister/list APIs to prefab registry
- Optional: namespacing/versioning, parameters/overrides
- Add concurrent registration/instantiation stress tests

### 8. Coroutines (Low Priority)
- Evaluate C++20 coroutines vs state machine
- Implement WaitForSeconds, WaitForEndOfFrame
- Only if needed for game patterns

### 9. SceneManager (Low Priority)
- Define scene asset format
- Implement LoadScene, async loading
- Multi-scene support (optional)

---

## Known Issues & Limitations

- Prefab registry: no unregister/list; parameters/overrides not yet supported

1. **2D Only:** Transform uses AmeTransform2D (x, y, angle). 3D requires separate system.
2. **Static Component Types:** AddComponent<T> hardcoded for specific types. No runtime reflection.
3. **Thread-Local Component Refs:** GetComponent returns thread_local to avoid dangling pointers.
4. **No Serialization:** Can't save/load scenes or prefabs yet.
5. **No Editor:** Code-only workflow, no visual tooling.
6. **Single-Threaded Scripts:** All Update() callbacks run sequentially.

---

## Migration from C API

### Before (Direct C/ECS)
```c
ecs_entity_t player = ecs_new_id(world);
AmeTransform2D* tr = ecs_get_mut(world, player, AmeTransform2D);
tr->x = 100.0f;

AmePhysicsBody* body = ecs_get_mut(world, player, AmePhysicsBody);
ame_physics_set_velocity(body->body, 10.0f, 0.0f);
```

### After (Unity-like C++)
```cpp
GameObject player = scene.Create("Player");
player.transform().position(glm::vec3(100, 0, 0));

auto& rb = player.AddComponent<Rigidbody2D>();
rb.velocity(glm::vec2(10, 0));
```

### Script Migration
```cpp
// Before: Manual update loop
void update_player(float dt) {
    // ... player logic
}

// Game loop
while (running) {
    update_player(dt);
}

// After: MongooseBehaviour
class PlayerController : public MongooseBehaviour {
    void Update(float dt) override {
        // ... player logic
    }
};
player.AddScript<PlayerController>();

// Game loop
while (running) {
    scene.Step(dt);
}
```

---

## Testing

### Unit Tests
- ❌ No unit tests yet
- **Plan:** Create tests for Transform, Rigidbody2D, GameObject lifecycle

### Integration Tests
- ✅ Working examples serve as integration tests
- **Plan:** Add automated test scene that exercises all API surfaces

### Performance Tests
- ❌ No benchmarks yet
- **Plan:** Measure component access, script iteration, hierarchy traversal

---

## Documentation

| Document | Status |
|----------|--------|
| API_REFERENCE.md | ✅ Complete |
| IMPLEMENTATION_STATUS.md | ✅ Complete (this doc) |
| GETTING_STARTED.md | ❌ Needed |
| MIGRATION_GUIDE.md | ❌ Needed |
| ARCHITECTURE.md | ❌ Needed |

---

**End of Status Document**