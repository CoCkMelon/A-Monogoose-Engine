Title: System – Input Gather

Purpose
Copy atomic keyboard state into CInput for entities that have it. This provides stable per-tick values and handles derived move_dir.

Query
- in: CInput (write)

Algorithm
- Read atomics left_down, right_down, jump_down.
- Derive move_dir = (right?1:0) - (left?1:0).
- For each matched entity:
  - in.move_dir = move_dir
  - in.jump_down = jump_down

Notes
- Do not update prev_jump_down here; that is owned by MovementAndJump.
- Keep this system early in the update order.

