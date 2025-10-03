# Physics Test Example

This is a simple test program that verifies the Unity-like physics API implementation in `cpp/unitylike/`.

## What it Tests

- ✅ **Physics world creation**
- ✅ **Multiple physics bodies** (5 GameObjects)
  - Player (dynamic, box collider)
  - Wall (static, box collider)
  - Platform (static, box collider)
  - Trigger zone (static, circle trigger)
  - Enemy (dynamic, circle collider)
- ✅ **Automatic body creation**
  - Bodies created automatically after Scene.Step()
  - No manual `ame_physics_create_body()` needed
- ✅ **Rigidbody2D component**
  - Adding to GameObject
  - Setting and getting velocity
  - Body types (Dynamic, Static)
  - Forces (AddForce)
- ✅ **Collider2D component**
  - Box colliders
  - Circle colliders
  - Trigger support
  - Dynamic size configuration
- ✅ **Physics2D raycast tests**
  - Single raycast (hits & misses)
  - RaycastAll (multiple hits)
  - Trigger behavior (triggers do hit raycasts)
  - Distance and normal calculation
- ✅ **Physics2D overlap queries**
  - OverlapPoint (hit detection)
  - OverlapCircle (finds multiple objects)
  - OverlapBox (area query)

## Building

```bash
cd build
cmake ..
make physics_test
```

## Running

```bash
./examples/physics_test/physics_test
```

## Expected Output

```
[UnityLike] AudioSync system registered
✓ Physics world created

=== Creating Physics Bodies ===
✓ Player created at (0, 0) with 1x2 box collider
✓ Wall created at (10, 0) with 2x10 box collider
✓ Platform created at (5, 8) with 6x1 box collider
✓ Trigger created at (-5, 0) with radius 2 circle (trigger)
✓ Enemy created at (3, 3) with radius 0.5 circle collider

=== Stepping Scene (creates physics bodies) ===
✓ Scene stepped - bodies created automatically

=== Testing Rigidbody2D API ===
✓ Set/Get velocity: (5, 2)
✓ AddForce called successfully

=== Testing Raycasts ===
  Right raycast: HIT at distance 9 (Wall)
  Left raycast: HIT at distance 3 (Trigger)
  Up raycast: HIT at distance 7.5 (Platform)
  Diagonal raycast: MISS

=== Testing RaycastAll ===
  RaycastAll found 3 hits
    Hit 1: Trigger at distance 3
    Hit 2: Player at distance 9.5
    Hit 3: Wall at distance 19

=== Testing Overlap Queries ===
  OverlapPoint at (0,0): HIT
  OverlapPoint at (20,20): MISS
  OverlapCircle(r=10) at origin: 5 objects
    - Trigger
    - Player
    - Wall
    - Enemy
    - Platform
  OverlapCircle(r=5) at (50,50): 0 objects
  OverlapBox(12x4) at (5,0): 2 objects
    - Player
    - Wall

✅ All physics tests passed!
```

## Implementation Files

The physics implementation tested here includes:

- `cpp/unitylike/Physics2D.cpp` - Raycasts and spatial queries
- `cpp/unitylike/PhysicsSync.cpp` - Transform/physics synchronization
- `cpp/unitylike/PhysicsCallbacks.cpp` - Collision detection callbacks
- `cpp/unitylike/Rigidbody2D.cpp` - Rigidbody component
- `cpp/unitylike/Collider2D.cpp` - Collider component

See `cpp/unitylike/PHYSICS_IMPLEMENTATION.md` for complete API documentation.
