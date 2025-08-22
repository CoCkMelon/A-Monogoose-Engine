# Unity-like API: Completely Missing Systems and Methods

This document lists major Unity API areas and representative methods/classes that are not present at all in the current MVP façade (based on cpp/unitylike/Scene.cpp and cpp/unitylike/InputTime.cpp). These are beyond the partial implementations noted in UNITY_API_GAPS.md and focus on entirely absent features.

Note: This is scoped to the façade layer; some engine capabilities may exist at lower levels but are not surfaced with Unity-like APIs.

## Rendering and Visuals

- Sprites and 2D Rendering
  - Classes: Sprite, SpriteRenderer
  - Methods/Props: SpriteRenderer.sprite, color, flipX/flipY, draw order/sortingLayer, sortingOrder, material override
  - Atlas/packing support, sprite slicing, pivot, pixelsPerUnit
- Materials and Shaders
  - Classes: Material, Shader
  - Methods: Shader.Find, Material.SetFloat/SetVector/SetTexture, MaterialPropertyBlocks, keywords
- Mesh Rendering (3D)
  - Classes: Mesh, MeshRenderer, MeshFilter, SkinnedMeshRenderer
  - Methods: RecalculateNormals/Tangents, submeshes, bounds, dynamic batching controls
- Lighting
  - Classes: Light, ReflectionProbe
  - Methods/Props: Light.type, color, intensity, range, shadows
- Cameras
  - Class: Camera
  - Methods/Props: main, worldToScreenPoint, screenToWorldPoint, viewport conversions, cullingMask, clearFlags, orthographic size/FOV, projection matrices
- Post-processing/Render Pipelines
  - SRP hooks, render features, command buffers, blit utilities

## Prefabs and Asset Management

- Prefabs
  - Instantiate from prefab assets, prefab variants, overrides, Apply/Revert
- Asset Loading
  - Resources API: Resources.Load/LoadAll/UnloadUnusedAssets
  - Addressables (or equivalent): async loading, references
  - AssetDatabase (editor-only) concepts omitted or replaced

## Physics (2D and 3D)

- Colliders and Queries
  - 2D: BoxCollider2D, CircleCollider2D, PolygonCollider2D, CompositeCollider2D
  - 3D: BoxCollider, SphereCollider, CapsuleCollider, MeshCollider
  - Queries: Physics2D.Raycast/RaycastAll/OverlapCircle/BoxCast; Physics.Raycast suite
- Rigidbody breadth
  - 2D: bodyType (Dynamic/Kinematic/Static), gravityScale, constraints, drag, angularDrag, AddForce/Impulse/Torque, Sleep/Wake
  - 3D: Rigidbody equivalent set
- Collision/Trigger events
  - OnCollisionEnter/Stay/Exit, OnTriggerEnter/Stay/Exit (2D/3D)
- Layers and collision matrix

## Scene Management

- SceneManager
  - LoadScene (single/additive), LoadSceneAsync, UnloadSceneAsync, activeScene
  - Scene lifecycle events
- Multi-scene additive composition, lighting scenes

## GameObject and Component System

- Component lifecycle breadth
  - OnEnable/OnDisable, OnDestroy already minimal; missing: OnApplicationFocus/Pause/Quit, OnBecameVisible/Invisible, OnValidate, Reset
- Component APIs
  - AddComponent/GetComponent/GetComponentsInChildren/InParent, TryGetComponent
  - Enable/disable components (Behaviour.enabled), MonoBehaviour.enabled
- Tags and Layers
  - tag, CompareTag, layer
- SendMessage/BroadcastMessage
- DontDestroyOnLoad

## Transform and Hierarchy (extended)

- Parenting APIs
  - Transform.parent, SetParent, GetChild, childCount, hierarchy traversal
- Local vs world transforms fully
  - localPosition, localRotation, localScale, lossyScale, InverseTransformPoint/Direction, TransformPoint/Direction
- Helpers
  - Translate, Rotate, LookAt, TransformDirection/Vector/Point

## Animation

- Animator and Animation systems
  - Animator: Controllers, parameters (bool/int/float/trigger), Play/CrossFade, layers, avatar masks
  - Animation: legacy clips, curve sampling
  - Events on animation clips

## Audio

- AudioSource, AudioClip, AudioListener, AudioMixer
  - Play/Stop/Pause, spatial blend, 2D/3D settings, doppler, reverb zones

## UI

- UGUI (Canvas, RectTransform, Image, Text, Button, EventSystem)
  - Raycasters, input modules
- Text Rendering (TextMeshPro or equivalent)

## Timing and Coroutines

- Time
  - time, unscaledTime, unscaledDeltaTime, timeScale, realtimeSinceStartup, frameCount, fixedUnscaledDeltaTime
- Coroutines
  - StartCoroutine/StopCoroutine, yield instructions (WaitForSeconds, WaitForEndOfFrame, WaitForFixedUpdate, CustomYieldInstruction)

## Scripting and Events

- Execution order controls (DefaultExecutionOrder)
- Attributes (RequireComponent, DisallowMultipleComponent, ExecuteInEditMode equivalents)
- Reflection-based serialization hooks (OnBeforeSerialize/OnAfterDeserialize)

## Navigation and Pathfinding

- NavMesh, NavMeshAgent, NavMeshObstacle

## Particles and Effects

- ParticleSystem and submodules
- LineRenderer, TrailRenderer
- Gizmos and Debug drawing (OnDrawGizmos, Debug.DrawLine/Ray)

## Resources and Persistence

- PlayerPrefs
- Persistent data path and file IO helpers
- Serialization of scenes/prefabs/components with versioning

## Math and Utilities

- Mathf parity (beyond std/glm): Perlin noise, SmoothDamp, Approximately, Repeat/PingPong, DeltaAngle
- Random API parity (Range, insideUnitSphere/insideUnitCircle, stateful Random)

## Networking/Multiplayer (optional scope)

- UNet equivalents or abstraction for transports

## Editor-only Concepts (if any are to be mirrored in tooling)

- Gizmo/UI handles, custom inspectors, property drawers
- Build pipeline hooks

---

Prioritization suggestion
- Core gameplay-enabling: Prefab instantiate, SpriteRenderer/Material basics, Collider+Rigidbody2D breadth (forces, bodyType), SceneManager.LoadScene, basic Camera.
- Developer ergonomics: AddComponent/GetComponent, hierarchy, local transforms, Time extras, Input axes/buttons.
- Nice-to-have later: UI, Audio, Animation, Particles, NavMesh, advanced rendering, Addressables.

