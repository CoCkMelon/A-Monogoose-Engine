import Lake
open Lake DSL

/- Formal model of the ame-next engine core (see docs/ and include/ame/).

    C module                    -> Lean namespace
    include/ame/pool.h          -> Ame.Pool      (handles, deferred frees)
    include/ame/events.h        -> Ame.EventRing (bounded drop-oldest)
    examples/memory_game/mem_sim.* -> Ame.MemGame (rules invariants)
    docs/loop.txt fixed step    -> Ame.Basic     (determinism contract)

    Pure Lean core (no mathlib): `lake build` is self-contained. -/

package «ame-model»

@[default_target]
lean_lib «Ame»
