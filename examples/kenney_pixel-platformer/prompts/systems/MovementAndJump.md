Title: System – Movement and Jump

Purpose
Apply horizontal movement based on input and implement buffered/coyote jump logic. Emit a one-tick jump_trigger when a jump is applied.

Query
- in: CPhysicsBody (read/write velocity)
- in: CInput (read/write buffer fields)
- in: CGrounded (read)

Constants
- move_speed = 50.0
- coyote_time = 0.15
- jump_buffer_time = 0.18
- jump_impulse_vy = -100.0

Algorithm (per entity)
- Read current velocity (vx, vy)
- Set vx = move_speed * in.move_dir
- Update coyote_timer: if grounded -> set to coyote_time; else decrement by dt, clamp >=0
- Edge detect jump: jump_edge = in.jump_down && !in.prev_jump_down
  - If jump_edge: in.jump_buffer = jump_buffer_time else decrement and clamp >=0
- in.jump_trigger = false
- If in.jump_buffer > 0 and (grounded || coyote_timer > 0):
  - vy = jump_impulse_vy
  - in.jump_buffer = 0
  - in.coyote_timer = 0
  - in.jump_trigger = true
- in.prev_jump_down = in.jump_down
- Write velocity (vx, vy)

Notes
- jump_trigger is consumed by AudioUpdate to play SFX.

