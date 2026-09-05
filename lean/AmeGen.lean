import Ame.Kernels

def main (args : List String) : IO UInt32 := do
  let out : System.FilePath :=
    match args with
    | p :: _ => p
    | [] => "../generated"
  IO.FS.createDirAll out
  IO.FS.writeFile (out / "ame_gen.h") Ame.Kernels.emitHeader
  IO.FS.writeFile (out / "ame_gen.c") Ame.Kernels.emitSource
  IO.println s!"wrote {out}/ame_gen.h and {out}/ame_gen.c"
  pure 0
