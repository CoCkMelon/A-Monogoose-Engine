Title: Subsystem – Physics World and Tilemap Collisions

Purpose
Create and manage the AmePhysicsWorld with a fixed timestep simulation, build static collisions from the tilemap, and create the dynamic player body.

Initialization
- Physics world: physics = ame_physics_world_create(gravity_x=0, gravity_y=100, fixed_dt)
- Tile collisions:
  - Choose a collision tile layer (heuristic: first non-empty layer named with "Tiles"; fallback to first non-empty)
  - Call ame_physics_create_tilemap_collision(physics, layer.data, layer.width, layer.height, tile_size_pixels)
- Player body:
  - ame_physics_create_body(physics, start_x, start_y, w=16, h=16, AME_BODY_DYNAMIC, is_sensor=false, userdata=NULL)
  - Attach to Player entity via CPhysicsBody

Fixed Timestep Loop (Logic Thread)
- Run separate thread that owns ecs_progress and physics stepping
- Accumulate real time; clamp per-frame to avoid spiral of death (e.g., <= 0.05s)
- While acc >= fixed_dt and steps < 5:
  - ecs_progress(world, fixed_dt)
  - ame_physics_world_step(physics)
  - acc -= fixed_dt
- Small sleep (~0.2ms) to reduce CPU

Ground Check Helper (optional)
- 3 raycasts downward at left/center/right offsets from the body to compute grounded state
- Use ame_physics_raycast(physics, x0, y0, x1, y1)

Shutdown
- Destroy player body: ame_physics_destroy_body(physics, body)
- Destroy physics world: ame_physics_world_destroy(physics)

