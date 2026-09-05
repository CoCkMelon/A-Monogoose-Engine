/-
  Tiny imperative AST. Kernels are written here, pretty-printed to C,
  and (for the pure integer fragment) evaluated in Lean so we can
  prove they match Ame.Handle / Ame.Geo.
-/
namespace Ame.Imp

inductive BinOp where
  | add | sub | mul | div | shl | shr | or | and
  | eq | ne | lt | le | gt | ge | land | lor
  deriving DecidableEq, Repr

inductive Expr where
  | const : Int → Expr
  | var : String → Expr
  | bin : BinOp → Expr → Expr → Expr
  | not : Expr → Expr
  | castU64 : Expr → Expr
  | idx : String → Expr → Expr
  | arrow : String → Expr
  | arrowIdx : String → Expr → Expr
  deriving Repr

inductive Stmt where
  | skip
  | seq : Stmt → Stmt → Stmt
  | decl : String → String → Stmt
  | assign : String → Expr → Stmt
  | assignIdx : String → Expr → Expr → Stmt
  | assignArrow : String → Expr → Stmt
  | assignArrowIdx : String → Expr → Expr → Stmt
  | assignDeref : String → Expr → Stmt
  | ite : Expr → Stmt → Stmt → Stmt
  | ret : Expr → Stmt
  | retVoid : Stmt
  | forN : String → Expr → Stmt → Stmt
  deriving Repr

structure Fun where
  name : String
  retTy : String
  params : List (String × String)
  body : Stmt
  doc : String

def seqs : List Stmt → Stmt
  | [] => .skip
  | s :: rest => .seq s (seqs rest)

def pad (n : Nat) : String := String.join (List.replicate n "  ")

def BinOp.toC : BinOp → String
  | .add => "+"
  | .sub => "-"
  | .mul => "*"
  | .div => "/"
  | .shl => "<<"
  | .shr => ">>"
  | .or  => "|"
  | .and => "&"
  | .eq  => "=="
  | .ne  => "!="
  | .lt  => "<"
  | .le  => "<="
  | .gt  => ">"
  | .ge  => ">="
  | .land => "&&"
  | .lor => "||"

partial def Expr.toC : Expr → String
  | .const n =>
      if n < 0 then toString n
      else if n > 2147483647 then toString n ++ "u"
      else toString n
  | .var x => x
  | .bin op a b => "(" ++ a.toC ++ " " ++ op.toC ++ " " ++ b.toC ++ ")"
  | .not a => "(!" ++ a.toC ++ ")"
  | .castU64 a => "((uint64_t)" ++ a.toC ++ ")"
  | .idx name i => name ++ "[" ++ i.toC ++ "]"
  | .arrow f => "s->" ++ f
  | .arrowIdx f i => "s->" ++ f ++ "[" ++ i.toC ++ "]"

partial def Stmt.toC (ind : Nat) : Stmt → String
  | .skip => ""
  | .seq a b => Stmt.toC ind a ++ Stmt.toC ind b
  | .decl ty x => pad ind ++ ty ++ " " ++ x ++ ";\n"
  | .assign x e => pad ind ++ x ++ " = " ++ e.toC ++ ";\n"
  | .assignIdx n i e =>
      pad ind ++ n ++ "[" ++ i.toC ++ "] = " ++ e.toC ++ ";\n"
  | .assignArrow f e => pad ind ++ "s->" ++ f ++ " = " ++ e.toC ++ ";\n"
  | .assignArrowIdx f i e =>
      pad ind ++ "s->" ++ f ++ "[" ++ i.toC ++ "] = " ++ e.toC ++ ";\n"
  | .assignDeref x e => pad ind ++ "*" ++ x ++ " = " ++ e.toC ++ ";\n"
  | .ite c t .skip =>
      pad ind ++ "if (" ++ c.toC ++ ") {\n" ++ Stmt.toC (ind+1) t ++ pad ind ++ "}\n"
  | .ite c .skip e =>
      pad ind ++ "if (" ++ c.toC ++ ") {\n" ++ pad ind ++ "} else {\n" ++
        Stmt.toC (ind+1) e ++ pad ind ++ "}\n"
  | .ite c t e =>
      pad ind ++ "if (" ++ c.toC ++ ") {\n" ++ Stmt.toC (ind+1) t ++ pad ind ++
        "} else {\n" ++ Stmt.toC (ind+1) e ++ pad ind ++ "}\n"
  | .ret e => pad ind ++ "return " ++ e.toC ++ ";\n"
  | .retVoid => pad ind ++ "return;\n"
  | .forN i n body =>
      pad ind ++ "for (int32_t " ++ i ++ " = 0; " ++ i ++ " < " ++ n.toC ++
        "; ++" ++ i ++ ") {\n" ++ Stmt.toC (ind+1) body ++ pad ind ++ "}\n"

def Fun.toC (f : Fun) : String :=
  let args := String.intercalate ", "
    (f.params.map (fun p => p.1 ++ " " ++ p.2))
  let proto := f.retTy ++ " " ++ f.name ++ "(" ++ args ++ ")"
  let doc :=
    if f.doc = "" then ""
    else "/* " ++ f.doc ++ " */\n"
  doc ++ proto ++ " {\n" ++ Stmt.toC 1 f.body ++ "}\n"

def Fun.proto (f : Fun) : String :=
  let args := String.intercalate ", "
    (f.params.map (fun p => p.1 ++ " " ++ p.2))
  f.retTy ++ " " ++ f.name ++ "(" ++ args ++ ");\n"

/- Integer evaluator for the *pure* fragment (no arrow/idx). -/
def evalBin : BinOp → Int → Int → Int
  | .add, a, b => a + b
  | .sub, a, b => a - b
  | .mul, a, b => a * b
  | .div, a, b => if b = 0 then 0 else a / b
  | .shl, a, b => a * (Int.ofNat (2 ^ b.toNat))
  | .shr, a, b => if b.toNat ≥ 64 then 0 else a / (Int.ofNat (2 ^ b.toNat))
  | .or,  a, b => Int.ofNat (a.toNat ||| b.toNat)
  | .and, a, b => Int.ofNat (a.toNat &&& b.toNat)
  | .eq,  a, b => if a = b then 1 else 0
  | .ne,  a, b => if a ≠ b then 1 else 0
  | .lt,  a, b => if a < b then 1 else 0
  | .le,  a, b => if a ≤ b then 1 else 0
  | .gt,  a, b => if a > b then 1 else 0
  | .ge,  a, b => if a ≥ b then 1 else 0
  | .land, a, b => if a = 0 then 0 else if b = 0 then 0 else 1
  | .lor,  a, b => if a = 0 then (if b = 0 then 0 else 1) else 1

abbrev Env := List (String × Int)

def lookup (e : Env) (x : String) : Int :=
  match e.find? (fun p => p.1 == x) with
  | some p => p.2
  | none => 0

def set (e : Env) (x : String) (v : Int) : Env := (x, v) :: e

partial def evalExpr (e : Env) : Expr → Int
  | .const n => n
  | .var x => lookup e x
  | .bin op a b => evalBin op (evalExpr e a) (evalExpr e b)
  | .not a => if evalExpr e a = 0 then 1 else 0
  | .castU64 a => evalExpr e a
  | .idx _ _ => 0
  | .arrow _ => 0
  | .arrowIdx _ _ => 0

partial def evalStmt (e : Env) : Stmt → Env × Option Int
  | .skip => (e, none)
  | .decl _ _ => (e, none)
  | .seq a b =>
      match evalStmt e a with
      | (e', some r) => (e', some r)
      | (e', none) => evalStmt e' b
  | .assign x ex => (set e x (evalExpr e ex), none)
  | .assignIdx _ _ _ => (e, none)
  | .assignArrow _ _ => (e, none)
  | .assignArrowIdx _ _ _ => (e, none)
  | .assignDeref _ _ => (e, none)
  | .ite c t u =>
      if evalExpr e c ≠ 0 then evalStmt e t else evalStmt e u
  | .ret ex => (e, some (evalExpr e ex))
  | .retVoid => (e, some 0)
  | .forN _ _ _ => (e, none)

def evalFun (f : Fun) (args : Env) : Option Int :=
  (evalStmt args f.body).2

end Ame.Imp
