# Unity-like API gaps in cpp/unitylike/Scene.cpp and cpp/unitylike/Time.cpp

This document lists everything missing or not fully implemented (relative to Unity’s API behavior) that is evident from the current implementations in:
- cpp/unitylike/Scene.cpp
- cpp/unitylike/Time.cpp

Items are ordered by implementation priority within each section.

## Scene and GameObject (cpp/unitylike/Scene.cpp)

1) GameObject active state
- Missing: Actual enabled/disabled state. activeSelf() is hardcoded true; SetActive(bool) is a TODO.
- Impact: Cannot disable objects or mirror Unity’s activeSelf/activeInHierarchy semantics.
- Suggested next: Add an Active tag/component and wire SetActive/activeSelf; consider activeInHierarchy later with parenting.

2) Transform scale
- Missing: Scale storage and application. localScale() returns (1,1,1) and setter is a no-op.
- Impact: No way to size objects; breaks parity with Transform.localScale and lossyScale.
- Suggested next: Extend AmeTransform2D (or add a separate component) to persist scale.

3) Transform rotation fidelity (2D only, crude conversion)
- Not fully implemented: Rotation is 2D-only via AmeTransform2D.angle, and setting from a quaternion uses a crude Z-extraction formula.
- Impact: Potential inaccuracies and no support for full 3D rotation API (Euler angles, forward/up vectors, Rotate, LookAt, etc.).
- Suggested next: Use glm helpers for yaw/angle around Z in 2D; document 2D-only. Full 3D can be deferred if engine is 2D.

4) Rigidbody2D isKinematic
- Missing: Query and control of kinematic state. isKinematic() returns false; setter is a no-op.
- Impact: Cannot switch between kinematic and dynamic behaviors consistent with Unity.
- Suggested next: Expose body type in the C physics API and store/reflect it via AmePhysicsBody.

5) GameObject/Transform hierarchy
- Missing: Parent/child relationships (Transform.parent, SetParent, hierarchy traversal).
- Impact: No activeInHierarchy semantics, no local vs world transform handling, no scene graph operations.
- Suggested next: Introduce a relationship component (e.g., Parent/Child) and compute world/local transforms.

6) Transform position/rotation completeness
- Missing: Local vs world separation (localPosition, localRotation), Translate/Rotate helpers, eulerAngles, right/up/forward accessors.
- Impact: Limited ergonomics compared to Unity’s Transform API.
- Suggested next: Provide minimal helpers once hierarchy/local transforms exist.

7) Rigidbody2D breadth of API
- Missing: AddForce, gravityScale, mass, constraints, drag/linearDamping, angularVelocity, collision/trigger events, etc.
- Impact: Narrow control over physics behavior.
- Suggested next: Incrementally surface what is available from ame/physics.h.

8) GameObject/component API surface
- Missing: Unity-style AddComponent/GetComponent, tag/layer, CompareTag, etc. (if intended in this facade).
- Impact: Harder to mirror Unity scripting patterns.
- Suggested next: Decide scope; add thin wrappers around Flecs as needed.

## Time (cpp/unitylike/Time.cpp)

1) Time.fixedDeltaTime control
- Not fully implemented: There is an internal unitylike_set_fixed_dt, but no public setter akin to UnityEngine.Time.fixedDeltaTime.
- Impact: Cannot configure fixed step from the facade.
- Suggested next: Expose a setter or initialize from engine configuration.

2) Additional Time properties
- Missing: time (alias for timeSinceLevelLoad), unscaledDeltaTime, unscaledTime, timeScale, realtimeSinceStartup, frameCount.
- Impact: Scripts may rely on these for pause/slow-motion or diagnostics.
- Suggested next: Add frameCount tick, consider timeScale/unscaled tracking.

## Notes
- The current scope, as documented in Scene.cpp, is an MVP: Scene, GameObject name/creation, minimal Transform, minimal Rigidbody2D velocity, and minimal script hosting. The items above enumerate gaps relative to Unity, but not all may be in scope for this project.

