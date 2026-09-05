import Ame.Geo
import Ame.Handle
import Ame.Imp

namespace Ame.Kernels
open Imp

def c (n : Int) : Expr := .const n
def v (x : String) : Expr := .var x
def eq (a b : Expr) : Expr := .bin .eq a b
def ne (a b : Expr) : Expr := .bin .ne a b
def le (a b : Expr) : Expr := .bin .le a b
def gt (a b : Expr) : Expr := .bin .gt a b
def add (a b : Expr) : Expr := .bin .add a b
def sub (a b : Expr) : Expr := .bin .sub a b
def div (a b : Expr) : Expr := .bin .div a b
def shl (a b : Expr) : Expr := .bin .shl a b
def shr (a b : Expr) : Expr := .bin .shr a b
def or_ (a b : Expr) : Expr := .bin .or a b
def and_ (a b : Expr) : Expr := .bin .and a b
def land (a b : Expr) : Expr := .bin .land a b
def ar (f : String) : Expr := .arrow f
def ari (f : String) (i : Expr) : Expr := .arrowIdx f i

def handleMake : Fun where
  name := "ame_gen_handle_make"
  retTy := "uint64_t"
  params := [("uint32_t", "index"), ("uint32_t", "generation")]
  doc := "Ame.Handle.mk' / pack. Generation 0 is invalid."
  body :=
    .ite (eq (v "generation") (c 0))
      (.ret (c 0))
      (.ret (or_ (shl (.castU64 (v "generation")) (c 32))
                 (.castU64 (v "index"))))

def handleIndex : Fun where
  name := "ame_gen_handle_index"
  retTy := "uint32_t"
  params := [("uint64_t", "h")]
  doc := "Low 32 bits."
  body := .ret (.bin .and (v "h") (c 4294967295))

def handleGeneration : Fun where
  name := "ame_gen_handle_generation"
  retTy := "uint32_t"
  params := [("uint64_t", "h")]
  doc := "High 32 bits. 0 means invalid."
  body := .ret (shr (v "h") (c 32))

def pointInXY : Fun where
  name := "ame_gen_point_in_aabb_xy"
  retTy := "int32_t"
  params := [("int32_t", "minx"), ("int32_t", "miny"),
             ("int32_t", "maxx"), ("int32_t", "maxy"),
             ("int32_t", "x"), ("int32_t", "y")]
  doc := "Ame.Geo.pointInXY (cursor vs card rect)."
  body :=
    .ret (land (land (le (v "minx") (v "x")) (le (v "x") (v "maxx")))
               (land (le (v "miny") (v "y")) (le (v "y") (v "maxy"))))

def aabbOverlap : Fun where
  name := "ame_gen_aabb_overlap"
  retTy := "int32_t"
  params := [("int32_t", "aminx"), ("int32_t", "aminy"), ("int32_t", "aminz"),
             ("int32_t", "amaxx"), ("int32_t", "amaxy"), ("int32_t", "amaxz"),
             ("int32_t", "bminx"), ("int32_t", "bminy"), ("int32_t", "bminz"),
             ("int32_t", "bmaxx"), ("int32_t", "bmaxy"), ("int32_t", "bmaxz")]
  doc := "Ame.Geo.overlap."
  body :=
    .ret (land (land (land (le (v "aminx") (v "bmaxx")) (le (v "bminx") (v "amaxx")))
                     (land (le (v "aminy") (v "bmaxy")) (le (v "bminy") (v "amaxy"))))
               (land (le (v "aminz") (v "bmaxz")) (le (v "bminz") (v "amaxz"))))

def poolSpawn : Fun where
  name := "ame_gen_pool_spawn"
  retTy := "int32_t"
  params := [("uint32_t *", "generation"), ("uint8_t *", "alive"),
             ("int32_t", "cap"), ("uint32_t *", "out_index"),
             ("uint32_t *", "out_gen")]
  doc := "First free slot, bump generation (skip 0). 1 = ok, 0 = full."
  body :=
    seqs [
      .decl "uint32_t" "g",
      .forN "i" (v "cap") (seqs [
        .ite (eq (.idx "alive" (v "i")) (c 0))
          (seqs [
            .assign "g" (add (.idx "generation" (v "i")) (c 1)),
            .ite (eq (v "g") (c 0)) (.assign "g" (c 1)) .skip,
            .assignIdx "generation" (v "i") (v "g"),
            .assignIdx "alive" (v "i") (c 1),
            .assignDeref "out_index" (v "i"),
            .assignDeref "out_gen" (v "g"),
            .ret (c 1)
          ])
          .skip
      ]),
      .ret (c 0)
    ]

def memReset : Fun where
  name := "ame_gen_mem_reset"
  retTy := "void"
  params := [("ame_gen_mem *", "s")]
  doc := "Canonical pairing pair[i] = i/2. Ame.Memory.start."
  body :=
    seqs [
      .forN "i" (c 16) (seqs [
        .assignArrowIdx "pair" (v "i") (div (v "i") (c 2)),
        .assignArrowIdx "face" (v "i") (c 0)
      ]),
      .assignArrow "turn" (c 0),
      .assignArrow "score0" (c 0),
      .assignArrow "score1" (c 0),
      .assignArrow "phase" (c 0),
      .assignArrow "open_a" (c 0),
      .assignArrow "open_b" (c 0),
      .assignArrow "outcome" (c 0),
      .assignArrow "n_matched" (c 0)
    ]

def memClick : Fun where
  name := "ame_gen_mem_click"
  retTy := "void"
  params := [("ame_gen_mem *", "s"), ("int32_t", "i")]
  doc := "Ame.Memory.click. Callback thread."
  body :=
    seqs [
      .ite (ne (ar "outcome") (c 0)) .retVoid .skip,
      .ite (eq (ar "phase") (c 2)) .retVoid .skip,
      .ite (eq (ar "phase") (c 0))
        (.ite (eq (ari "face" (v "i")) (c 0))
          (seqs [
            .assignArrowIdx "face" (v "i") (c 1),
            .assignArrow "phase" (c 1),
            .assignArrow "open_a" (v "i")
          ])
          .skip)
        .skip,
      .ite (eq (ar "phase") (c 1))
        (seqs [
          .ite (eq (v "i") (ar "open_a")) .retVoid .skip,
          .ite (eq (ari "face" (v "i")) (c 0))
            (seqs [
              .assignArrowIdx "face" (v "i") (c 1),
              .assignArrow "phase" (c 2),
              .assignArrow "open_b" (v "i")
            ])
            .skip
        ])
        .skip
    ]

def memResolve : Fun where
  name := "ame_gen_mem_resolve"
  retTy := "void"
  params := [("ame_gen_mem *", "s")]
  doc := "Ame.Memory.resolve. Main thread after HOLD_T."
  body :=
    seqs [
      .decl "int32_t" "a",
      .decl "int32_t" "b",
      .ite (ne (ar "outcome") (c 0)) .retVoid .skip,
      .ite (ne (ar "phase") (c 2)) .retVoid .skip,
      .assign "a" (ar "open_a"),
      .assign "b" (ar "open_b"),
      .ite (eq (ari "pair" (v "a")) (ari "pair" (v "b")))
        (seqs [
          .assignArrowIdx "face" (v "a") (c 2),
          .assignArrowIdx "face" (v "b") (c 2),
          .ite (eq (ar "turn") (c 0))
            (.assignArrow "score0" (add (ar "score0") (c 1)))
            (.assignArrow "score1" (add (ar "score1") (c 1))),
          .assignArrow "n_matched" (add (ar "n_matched") (c 1)),
          .assignArrow "phase" (c 0),
          .ite (eq (ar "n_matched") (c 8))
            (.ite (gt (ar "score0") (ar "score1"))
              (.assignArrow "outcome" (c 1))
              (.ite (gt (ar "score1") (ar "score0"))
                (.assignArrow "outcome" (c 2))
                (.assignArrow "outcome" (c 3))))
            (.assignArrow "turn" (sub (c 1) (ar "turn")))
        ])
        (seqs [
          .assignArrowIdx "face" (v "a") (c 0),
          .assignArrowIdx "face" (v "b") (c 0),
          .assignArrow "phase" (c 0),
          .assignArrow "turn" (sub (c 1) (ar "turn"))
        ])
    ]

def allFuns : List Fun :=
  [handleMake, handleIndex, handleGeneration,
   pointInXY, aabbOverlap, poolSpawn,
   memReset, memClick, memResolve]

def banner : String :=
  "/* GENERATED FROM lean/Ame/Kernels.lean — do not edit.\n" ++
  " * Regenerated by: cd lean && lake exe ame-gen ../generated\n" ++
  " * Reference implementation of proved kernels. Production src/\n" ++
  " * may diverge for performance; this file is the spec in C form.\n" ++
  " */\n"

def structDecl : String :=
  "typedef struct ame_gen_mem {\n" ++
  "  int32_t pair[16];\n" ++
  "  int32_t face[16]; /* 0 down, 1 up, 2 matched */\n" ++
  "  int32_t turn;\n" ++
  "  int32_t score0;\n" ++
  "  int32_t score1;\n" ++
  "  int32_t phase;    /* 0 idle, 1 oneOpen, 2 resolving */\n" ++
  "  int32_t open_a;\n" ++
  "  int32_t open_b;\n" ++
  "  int32_t outcome;  /* 0 playing, 1 p1, 2 p2, 3 tie */\n" ++
  "  int32_t n_matched;\n" ++
  "} ame_gen_mem;\n"

def emitHeader : String :=
  banner ++
  "#ifndef AME_GEN_H\n#define AME_GEN_H\n\n" ++
  "#include <stdint.h>\n\n" ++
  structDecl ++ "\n" ++
  String.join (allFuns.map Fun.proto) ++
  "\n#endif\n"

def emitSource : String :=
  banner ++
  "#include \"ame_gen.h\"\n\n" ++
  String.join (allFuns.map (fun f => Fun.toC f ++ "\n"))

/- Evaluator agrees with Handle.pack / Geo.pointInXY on the documented examples. -/

theorem handle_make_zero :
    evalFun handleMake [("index", 9), ("generation", 0)] = some 0 := by
  native_decide

theorem handle_make_pack37 :
    evalFun handleMake [("index", 3), ("generation", 7)] =
      some (7 * (2 ^ 32 : Int) + 3) := by
  native_decide

theorem handle_make_matches_mk' :
    Handle.mk' 3 7 = ⟨3, 7⟩ ∧ Handle.mk' 9 0 = Handle.invalid := by
  simp [Handle.mk', Handle.invalid]

theorem pointIn_centre_eval :
    evalFun pointInXY
      [("minx", -1), ("miny", -1), ("maxx", 1), ("maxy", 1),
       ("x", 0), ("y", 0)] = some 1 := by
  native_decide

theorem pointIn_far_eval :
    evalFun pointInXY
      [("minx", -1), ("miny", -1), ("maxx", 1), ("maxy", 1),
       ("x", 5), ("y", 0)] = some 0 := by
  native_decide

theorem pointIn_eval_matches_geo_centre :
    Geo.pointInXY Geo.card 0 0 = true :=
  Geo.centre_in_card

theorem pointIn_eval_matches_geo_far :
    Geo.pointInXY Geo.card 5 0 = false :=
  Geo.far_miss_xy

end Ame.Kernels
