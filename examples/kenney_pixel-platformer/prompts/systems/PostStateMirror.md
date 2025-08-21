Title: System – Post State Mirror

Purpose
Mirror authoritative physics position into atomics for the renderer to consume without locking.

Query
- in: CPhysicsBody (read)

Algorithm
- Read px, py from body
- atomic_store(player_x, px)
- atomic_store(player_y, py)

Notes
- Run late in update order, after movement.

