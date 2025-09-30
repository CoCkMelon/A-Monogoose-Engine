# Multithreaded Script Execution Example

This example demonstrates how the Unity-like script system in `cpp/unitylike/` properly executes `MongooseBehaviour` scripts across multiple threads using Flecs ECS.

## What This Shows

- **Thread-Safe Script Execution**: Scripts run in parallel across multiple worker threads
- **Proper Lifecycle Management**: Awake → Start → Update/LateUpdate/FixedUpdate → OnDestroy
- **Multiple Script Types**: Different script classes on the same entity
- **Script Retrieval**: Getting script instances from GameObjects
- **Performance Measurement**: Frame timing with multithreaded execution

## Key Features Demonstrated

### 1. Multithreading
- Uses `ecs_set_threads()` to enable Flecs worker threads
- Scripts execute in parallel across CPU cores
- Thread IDs are logged to show parallel execution

### 2. Script Components
- Each script type gets its own Flecs component
- Proper constructor/destructor hooks for memory management
- Type-safe script retrieval and storage

### 3. Unity-like API
```cpp
GameObject go = scene.Create("MyEntity");
go.AddScript<TestScript>(constructor_args);
TestScript* script = go.GetScript<TestScript>();
```

## Build and Run

```bash
# From the repository root
g++ -std=c++17 -I. -Ithird_party/flecs/include \
    examples/multithreaded_scripts/main.cpp \
    cpp/unitylike/*.cpp \
    -lflecs -pthread -o multithreaded_test

./multithreaded_test
```

## Expected Output

You should see:
- Scripts executing on different thread IDs
- Proper lifecycle order (Awake before Start, etc.)
- All expected method calls counted correctly
- ✅ Test PASSED message

## Architecture Benefits

1. **True Parallelism**: Scripts run simultaneously on multiple cores
2. **ECS Integration**: Scripts are proper Flecs components, not external state
3. **Memory Safety**: Automatic cleanup via component destructors
4. **Type Safety**: Each script type has its own component ID
5. **Performance**: Flecs handles efficient parallel iteration

This replaces the old manual script management system with a proper ECS-based approach that scales across multiple threads.