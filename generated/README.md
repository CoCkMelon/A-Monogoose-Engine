# Generated C (from Lean)

Do not edit `ame_gen.h` / `ame_gen.c`. They are pretty-printed from
`lean/Ame/Kernels.lean` (an Imp AST that is also evaluated in Lean).

```
export PATH="$HOME/.elan/bin:$PATH"
cd lean && lake exe ame-gen ../generated
```

This is the **reference** for handle packing, AABB XY pick, pool spawn,
and Memory click/resolve. Hand-written `src/` may optimize; if a kernel
disagrees with these files, the Lean model wins until you change the
model and regenerate.
