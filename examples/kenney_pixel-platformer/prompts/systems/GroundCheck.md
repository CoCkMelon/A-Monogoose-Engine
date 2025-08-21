Title: System – Ground Check

Purpose
Set CGrounded.value by raycasting under the actor at three points (left/center/right) using AmePhysicsWorld.

Query
- in: CPhysicsBody (read)
- in: CGrounded (write)
- opt in: CSize (read, optional) – to determine half width

Algorithm
- For each entity:
  - Get world position from physics body.
  - halfw = (size.w*0.5) or 8.0 default
  - Define three X offsets: -halfw+2, 0, +halfw-2
  - Raycast from y0 = py + halfw - 1 to y1 = py + halfw + 12 (downwards)
  - If any ray hits, grounded = true
  - Write CGrounded.value

Notes
- Uses ame_physics_raycast; physics world provided via resource.

