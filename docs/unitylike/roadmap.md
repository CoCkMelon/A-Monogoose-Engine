# Unity‑like Facade Roadmap

MVP (Phase A)
- C++ façade target that links to the existing C core.
- Implement: World, GameObject, Transform2D, MongooseBehaviour, Input, Time.
- Physics2D: minimal Rigidbody2D and Collider2D wrappers (velocity set/get, basic box collider creation).
- Optional: SpriteRenderer component binding an OpenGL texture id.
- Example: Reproduce pixel platformer movement using the façade.

Phase B
- Script registry and hot‑attachment API: go.AddScript<PlayerController>() constructs via registry.
- Camera component and Camera.main; simple Follow behaviour.
- Input mouse helpers and buttons; keycode enum aligning with asyncinput/SDL.
- Destruction queueing: defer entity destruction to end of frame.

Phase C
- Animator (frame‑based, driven by existing textures) with a small state machine.
- Simple timers/scheduler to emulate coroutines (InvokeAfter, WaitForSeconds style helpers).
- Basic UI debug overlay (optional).

Deferred (later phases)
- 3D façade and camera; full physics variations; editor; networking; serialization pipeline.

Non‑goals (for MVP)
- Avoid mixing render thread writes from scripts.
- No complex scene importers or editors.
- Keep API small and consistent; prefer data‑driven growth from real examples.
