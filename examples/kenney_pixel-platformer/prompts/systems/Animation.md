Title: System – Animation

Purpose
Choose player sprite frame based on grounded state and horizontal speed. Advance animation time.

Query
- in: CAnimation (read/write)
- in: CPhysicsBody (read)
- in: CGrounded (read)

Algorithm
- Read vx, vy from physics body
- an.time += dt
- if not grounded -> an.frame = 3 (jump)
- else if abs(vx) > 1 -> walk cycle at ~10 FPS: an.frame = 1 + ((int)(an.time * 10) % 2)
- else -> an.frame = 0 (idle)
- Mirror an.frame into atomic player_frame for renderer

