#!/usr/bin/env python3
"""Bezier JSON → 3D mesh (OBJ) + C arrays (segs, boxes, markers, tris).

Authoring source is cubic Beziers in XY (gameplay plane). Each curve is
extruded in Z into a solid ribbon. Collide=true curves also emit one-sided
XY segments. Boxes stay AABBs (wall / shelf). Markers place pickups.

Usage:
  tools/level_to_c.py assets/level/biscuit.json --c gen/level_gen.c --h gen/level_gen.h \\
      --obj assets/level/biscuit.obj
"""
from __future__ import annotations

import argparse
import json
import math
import pathlib
import sys

KIND = {
    "fuel": 0,
    "jump": 1,
    "jump_cookie": 1,
    "mine": 2,
    "saw": 3,
    "spawn": 4,
    "goal": 5,
}


def cf(x: float) -> str:
    v = float(x)
    if abs(v) < 1e-10:
        v = 0.0
    s = f"{v:.6f}".rstrip("0").rstrip(".")
    if s in ("", "-0"):
        s = "0"
    if "." not in s and "e" not in s and "E" not in s:
        s += ".0"
    return s + "f"


def lerp(a, b, t):
    return a + (b - a) * t


def bez3(p0, p1, p2, p3, t):
    u = 1.0 - t
    x = u*u*u*p0[0] + 3*u*u*t*p1[0] + 3*u*t*t*p2[0] + t*t*t*p3[0]
    y = u*u*u*p0[1] + 3*u*u*t*p1[1] + 3*u*t*t*p2[1] + t*t*t*p3[1]
    return x, y


def bez3d(p0, p1, p2, p3, t):
    """Derivative of cubic Bezier (tangent)."""
    u = 1.0 - t
    x = 3*u*u*(p1[0]-p0[0]) + 6*u*t*(p2[0]-p1[0]) + 3*t*t*(p3[0]-p2[0])
    y = 3*u*u*(p1[1]-p0[1]) + 6*u*t*(p2[1]-p1[1]) + 3*t*t*(p3[1]-p2[1])
    return x, y


def norm2(x, y):
    l = math.hypot(x, y)
    if l < 1e-10:
        return 0.0, 1.0
    return x / l, y / l


class Mesh:
    def __init__(self):
        self.verts = []  # (x,y,z,nx,ny,nz,u,v)
        self.tris = []   # (i0,i1,i2)

    def add_vert(self, x, y, z, nx, ny, nz, u, v):
        self.verts.append((x, y, z, nx, ny, nz, u, v))
        return len(self.verts) - 1

    def add_tri(self, a, b, c):
        self.tris.append((a, b, c))

    def add_quad(self, p0, p1, p2, p3, n, uv00, uv10, uv11, uv01):
        """p0..p3 CCW as seen along n. uv pairs (u,v)."""
        i0 = self.add_vert(*p0, *n, *uv00)
        i1 = self.add_vert(*p1, *n, *uv10)
        i2 = self.add_vert(*p2, *n, *uv11)
        i3 = self.add_vert(*p3, *n, *uv01)
        self.add_tri(i0, i1, i2)
        self.add_tri(i0, i2, i3)


def atlas_uv(kind: str, u: float, v: float):
    """kind: dirt | grass | bg. Atlas is 256, dirt 0,0 64x64, grass 64,0."""
    u = u - math.floor(u)
    v = max(0.0, min(1.0, v))
    if kind == "grass":
        x0, y0 = 64.0, 0.0
    elif kind == "bg":
        x0, y0 = 0.0, 0.0
    else:
        x0, y0 = 0.0, 0.0
    pad = 1.5
    uu = (x0 + pad + u * (64.0 - 2.0 * pad)) / 256.0
    vv = (y0 + pad + v * (64.0 - 2.0 * pad)) / 256.0
    return uu, vv


def sample_curve(curve: dict):
    cubics = curve["cubics"]
    n = int(curve.get("samples", 12))
    if n < 2:
        n = 2
    pts = []  # (x, y, nx, ny, s_along)
    dist = 0.0
    prev = None
    for cub in cubics:
        p0, p1, p2, p3 = cub["p0"], cub["p1"], cub["p2"], cub["p3"]
        # samples inclusive; skip first of later cubics if it matches last
        for i in range(n + 1):
            t = i / n
            x, y = bez3(p0, p1, p2, p3, t)
            tx, ty = bez3d(p0, p1, p2, p3, t)
            tx, ty = norm2(tx, ty)
            nx, ny = -ty, tx  # left of tangent: (1,0) → (0,1)
            if prev is not None:
                d = math.hypot(x - prev[0], y - prev[1])
                if d < 0.04 and i != n:
                    continue
                dist += d
            pts.append((x, y, nx, ny, dist))
            prev = (x, y)
    return pts


def extrude_ribbon(mesh: Mesh, pts, half_z, thickness, z0, bg: bool):
    if len(pts) < 2:
        return
    hz = float(half_z)
    th = float(thickness)
    # Precompute corners per sample
    tops_l, tops_r, bots_l, bots_r = [], [], [], []
    for x, y, nx, ny, s in pts:
        zl = z0 + hz
        zr = z0 - hz
        bx = x - nx * th
        by = y - ny * th
        tops_l.append((x, y, zl))
        tops_r.append((x, y, zr))
        bots_l.append((bx, by, zl))
        bots_r.append((bx, by, zr))

    length = max(pts[-1][4], 1.0)
    grass_lip = 0.14  # world units of front face painted grass
    lip_v = min(0.22, grass_lip / max(th, 0.2))

    def u_at(i):
        return pts[i][4] / 4.0  # tile every 4 world units

    for i in range(len(pts) - 1):
        u0, u1 = u_at(i), u_at(i + 1)
        x0, y0, nx0, ny0, _ = pts[i]
        x1, y1, nx1, ny1, _ = pts[i + 1]
        ntop = norm2(nx0 + nx1, ny0 + ny1)
        ntop3 = (ntop[0], ntop[1], 0.0)
        # Top ribbon (grass if driveable track, dirt if bg)
        top_kind = "bg" if bg else "grass"
        mesh.add_quad(
            tops_r[i], tops_r[i + 1], tops_l[i + 1], tops_l[i],
            ntop3,
            atlas_uv(top_kind, u0, 0.0),
            atlas_uv(top_kind, u1, 0.0),
            atlas_uv(top_kind, u1, 1.0),
            atlas_uv(top_kind, u0, 1.0),
        )
        # Front +Z (camera-facing)
        kind_body = "bg" if bg else "dirt"
        # split lip / body on front
        def split_front(tl, tr, bl, br, zsign=1.0):
            # tl,tr are top left/right along the curve (here left=i, right=i+1)
            # Interpolate lip points
            def mix(a, b, t):
                return (lerp(a[0], b[0], t), lerp(a[1], b[1], t), lerp(a[2], b[2], t))
            lip_l = mix(tl, bl, lip_v)
            lip_r = mix(tr, br, lip_v)
            n = (0.0, 0.0, zsign)
            if not bg:
                mesh.add_quad(
                    tl, tr, lip_r, lip_l, n,
                    atlas_uv("grass", u0, 0.0),
                    atlas_uv("grass", u1, 0.0),
                    atlas_uv("grass", u1, 1.0),
                    atlas_uv("grass", u0, 1.0),
                )
                mesh.add_quad(
                    lip_l, lip_r, br, bl, n,
                    atlas_uv(kind_body, u0, 0.0),
                    atlas_uv(kind_body, u1, 0.0),
                    atlas_uv(kind_body, u1, 1.0),
                    atlas_uv(kind_body, u0, 1.0),
                )
            else:
                mesh.add_quad(
                    tl, tr, br, bl, n,
                    atlas_uv(kind_body, u0, 0.0),
                    atlas_uv(kind_body, u1, 0.0),
                    atlas_uv(kind_body, u1, 1.0),
                    atlas_uv(kind_body, u0, 1.0),
                )
        split_front(tops_l[i], tops_l[i + 1], bots_l[i], bots_l[i + 1], 1.0)
        # Back -Z (winding flipped so normal -Z)
        mesh.add_quad(
            tops_r[i + 1], tops_r[i], bots_r[i], bots_r[i + 1],
            (0.0, 0.0, -1.0),
            atlas_uv(kind_body, u1, 0.0),
            atlas_uv(kind_body, u0, 0.0),
            atlas_uv(kind_body, u0, 1.0),
            atlas_uv(kind_body, u1, 1.0),
        )
        # Bottom
        nbot = (-ntop[0], -ntop[1], 0.0)
        mesh.add_quad(
            bots_l[i], bots_l[i + 1], bots_r[i + 1], bots_r[i],
            nbot,
            atlas_uv(kind_body, u0, 0.0),
            atlas_uv(kind_body, u1, 0.0),
            atlas_uv(kind_body, u1, 1.0),
            atlas_uv(kind_body, u0, 1.0),
        )

    # End caps
    def cap(i, outward):
        x, y, nx, ny, s = pts[i]
        # outward along ±tangent
        if i == 0:
            tx, ty = pts[1][0] - pts[0][0], pts[1][1] - pts[0][1]
        else:
            tx, ty = pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]
        tx, ty = norm2(tx, ty)
        n = (tx * outward, ty * outward, 0.0)
        u = u_at(i)
        kind = "bg" if bg else "dirt"
        if outward < 0:
            mesh.add_quad(
                tops_l[i], tops_r[i], bots_r[i], bots_l[i], n,
                atlas_uv(kind, u, 0), atlas_uv(kind, u + 0.2, 0),
                atlas_uv(kind, u + 0.2, 1), atlas_uv(kind, u, 1),
            )
        else:
            mesh.add_quad(
                tops_r[i], tops_l[i], bots_l[i], bots_r[i], n,
                atlas_uv(kind, u, 0), atlas_uv(kind, u + 0.2, 0),
                atlas_uv(kind, u + 0.2, 1), atlas_uv(kind, u, 1),
            )

    cap(0, -1.0)
    cap(len(pts) - 1, 1.0)


def add_box_mesh(mesh: Mesh, cx, cy, w, h, hz=0.55, z0=0.0):
    hx, hy = w * 0.5, h * 0.5
    x0, x1 = cx - hx, cx + hx
    y0, y1 = cy - hy, cy + hy
    z0b, z1 = z0 - hz, z0 + hz
    dirt = "dirt"
    grass = "grass"
    # +Z camera face
    mesh.add_quad(
        (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
        (0, 0, 1),
        atlas_uv(dirt, 0, 0), atlas_uv(dirt, w / 4, 0),
        atlas_uv(dirt, w / 4, 1), atlas_uv(dirt, 0, 1),
    )
    # grass lip on +Z
    mesh.add_quad(
        (x0, y1 - 0.12, z1 + 0.01), (x1, y1 - 0.12, z1 + 0.01),
        (x1, y1, z1 + 0.01), (x0, y1, z1 + 0.01),
        (0, 0, 1),
        atlas_uv(grass, 0, 0), atlas_uv(grass, w / 4, 0),
        atlas_uv(grass, w / 4, 1), atlas_uv(grass, 0, 1),
    )
    # -Z
    mesh.add_quad(
        (x1, y0, z0b), (x0, y0, z0b), (x0, y1, z0b), (x1, y1, z0b),
        (0, 0, -1),
        atlas_uv(dirt, 0, 0), atlas_uv(dirt, 1, 0),
        atlas_uv(dirt, 1, 1), atlas_uv(dirt, 0, 1),
    )
    # +Y top
    mesh.add_quad(
        (x0, y1, z1), (x1, y1, z1), (x1, y1, z0b), (x0, y1, z0b),
        (0, 1, 0),
        atlas_uv(grass, 0, 0), atlas_uv(grass, 1, 0),
        atlas_uv(grass, 1, 1), atlas_uv(grass, 0, 1),
    )
    # -Y
    mesh.add_quad(
        (x0, y0, z0b), (x1, y0, z0b), (x1, y0, z1), (x0, y0, z1),
        (0, -1, 0),
        atlas_uv(dirt, 0, 0), atlas_uv(dirt, 1, 0),
        atlas_uv(dirt, 1, 1), atlas_uv(dirt, 0, 1),
    )
    # +X
    mesh.add_quad(
        (x1, y0, z1), (x1, y0, z0b), (x1, y1, z0b), (x1, y1, z1),
        (1, 0, 0),
        atlas_uv(dirt, 0, 0), atlas_uv(dirt, 1, 0),
        atlas_uv(dirt, 1, 1), atlas_uv(dirt, 0, 1),
    )
    # -X
    mesh.add_quad(
        (x0, y0, z0b), (x0, y0, z1), (x0, y1, z1), (x0, y1, z0b),
        (-1, 0, 0),
        atlas_uv(dirt, 0, 0), atlas_uv(dirt, 1, 0),
        atlas_uv(dirt, 1, 1), atlas_uv(dirt, 0, 1),
    )


def segs_from_pts(pts):
    out = []
    for i in range(len(pts) - 1):
        x0, y0, nx0, ny0, _ = pts[i]
        x1, y1, nx1, ny1, _ = pts[i + 1]
        dx, dy = x1 - x0, y1 - y0
        if math.hypot(dx, dy) < 0.03:
            continue
        nx, ny = norm2(nx0 + nx1, ny0 + ny1)
        out.append((x0, y0, x1, y1, nx, ny))
    return out


def write_obj(path: pathlib.Path, mesh: Mesh, segs, boxes, markers, meta: str):
    lines = [
        "# Biscuit Fuel level — generated by tools/level_to_c.py",
        f"# {meta}",
        "o biscuit_level",
        "g mesh",
    ]
    for v in mesh.verts:
        lines.append(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}")
    for v in mesh.verts:
        lines.append(f"vn {v[3]:.6f} {v[4]:.6f} {v[5]:.6f}")
    for v in mesh.verts:
        lines.append(f"vt {v[6]:.6f} {v[7]:.6f}")
    for a, b, c in mesh.tris:
        # 1-indexed, same index for v/vt/vn
        lines.append(f"f {a+1}/{a+1}/{a+1} {b+1}/{b+1}/{b+1} {c+1}/{c+1}/{c+1}")
    lines.append("g collision_segs")
    for i, s in enumerate(segs):
        lines.append(
            f"# seg {i} {s[0]:.4f},{s[1]:.4f} -> {s[2]:.4f},{s[3]:.4f} n={s[4]:.3f},{s[5]:.3f}"
        )
    lines.append("g boxes")
    for b in boxes:
        lines.append(f"# box cx={b[0]} cy={b[1]} w={b[2]} h={b[3]}")
    lines.append("g markers")
    for m in markers:
        lines.append(f"# marker kind={m[0]} x={m[1]} y={m[2]} a={m[3]}")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def write_h(path: pathlib.Path):
    path.write_text(
        """#ifndef LEVEL_GEN_H
#define LEVEL_GEN_H

/* Generated by tools/level_to_c.py — do not edit. */

enum {
    LEVEL_MK_FUEL = 0,
    LEVEL_MK_JUMP = 1,
    LEVEL_MK_MINE = 2,
    LEVEL_MK_SAW = 3,
    LEVEL_MK_SPAWN = 4,
    LEVEL_MK_GOAL = 5
};

typedef struct LevelSeg {
    float x0, y0, x1, y1;
    float nx, ny;
} LevelSeg;

typedef struct LevelBox {
    float cx, cy, w, h;
} LevelBox;

typedef struct LevelVert {
    float x, y, z;
    float nx, ny, nz;
    float u, v;
} LevelVert;

typedef struct LevelTri {
    unsigned short i0, i1, i2;
} LevelTri;

typedef struct LevelMarker {
    int kind;
    float x, y, a;
} LevelMarker;

extern const LevelSeg level_segs[];
extern const int level_n_seg;
extern const LevelBox level_boxes[];
extern const int level_n_box;
extern const LevelVert level_verts[];
extern const int level_n_vert;
extern const LevelTri level_tris[];
extern const int level_n_tri;
extern const LevelMarker level_markers[];
extern const int level_n_marker;

#endif
"""
    )


def write_c(path: pathlib.Path, mesh: Mesh, segs, boxes, markers):
    if mesh.verts and max(max(t) for t in mesh.tris) > 65535:
        raise SystemExit("mesh too large for unsigned short indices")
    lines = [
        "/* Generated by tools/level_to_c.py — do not edit. */",
        '#include "level_gen.h"',
        "",
        "const LevelSeg level_segs[] = {",
    ]
    for s in segs:
        lines.append(
            "    { %s, %s, %s, %s, %s, %s },"
            % tuple(cf(v) for v in s)
        )
    if not segs:
        lines.append("    { 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f },")
        segs = [(0, 0, 1, 0, 0, 1)]
    lines.append("};")
    lines.append(f"const int level_n_seg = {len(segs)};")
    lines.append("")
    lines.append("const LevelBox level_boxes[] = {")
    for b in boxes:
        lines.append("    { %s, %s, %s, %s }," % tuple(cf(v) for v in b))
    if not boxes:
        lines.append("    { 0.0f, 0.0f, 1.0f, 1.0f },")
        boxes = [(0, 0, 1, 1)]
    lines.append("};")
    lines.append(f"const int level_n_box = {len(boxes)};")
    lines.append("")
    lines.append("const LevelVert level_verts[] = {")
    for v in mesh.verts:
        lines.append(
            "    { %s, %s, %s, %s, %s, %s, %s, %s },"
            % tuple(cf(x) for x in v)
        )
    if not mesh.verts:
        lines.append("    { 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f },")
    lines.append("};")
    lines.append(f"const int level_n_vert = {len(mesh.verts) if mesh.verts else 1};")
    lines.append("")
    lines.append("const LevelTri level_tris[] = {")
    for t in mesh.tris:
        lines.append("    { %d, %d, %d }," % t)
    if not mesh.tris:
        lines.append("    { 0, 0, 0 },")
    lines.append("};")
    lines.append(f"const int level_n_tri = {len(mesh.tris) if mesh.tris else 0};")
    lines.append("")
    lines.append("const LevelMarker level_markers[] = {")
    for m in markers:
        lines.append("    { %d, %s, %s, %s }," % (m[0], cf(m[1]), cf(m[2]), cf(m[3])))
    if not markers:
        lines.append("    { 0, 0.0f, 0.0f, 0.0f },")
        markers = [(0, 0, 0, 0)]
    lines.append("};")
    lines.append(f"const int level_n_marker = {len(markers)};")
    lines.append("")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines) + "\n")


def convert(doc: dict):
    mesh = Mesh()
    segs = []
    half_z_default = float(doc.get("half_z", 0.65))
    thick_default = float(doc.get("thickness", 0.70))
    for curve in doc.get("curves", []):
        pts = sample_curve(curve)
        hz = float(curve.get("half_z", half_z_default))
        th = float(curve.get("thickness", thick_default))
        z0 = float(curve.get("z", 0.0))
        bg = curve.get("kind", "track") in ("bg", "pit", "decor")
        collide = bool(curve.get("collide", not bg))
        extrude_ribbon(mesh, pts, hz, th, z0, bg)
        if collide:
            segs.extend(segs_from_pts(pts))
    boxes = []
    for b in doc.get("boxes", []):
        cx, cy = float(b["cx"]), float(b["cy"])
        w, h = float(b["w"]), float(b["h"])
        boxes.append((cx, cy, w, h))
        add_box_mesh(mesh, cx, cy, w, h, hz=float(b.get("half_z", 0.55)))
    markers = []
    for m in doc.get("markers", []):
        k = KIND[m["kind"]]
        markers.append((k, float(m["x"]), float(m["y"]), float(m.get("a", 0.0))))
    return mesh, segs, boxes, markers


def main(argv=None):
    ap = argparse.ArgumentParser(description="Bezier level → OBJ + C")
    ap.add_argument("json", type=pathlib.Path)
    ap.add_argument("--c", type=pathlib.Path, required=True)
    ap.add_argument("--h", type=pathlib.Path, required=True)
    ap.add_argument("--obj", type=pathlib.Path, default=None)
    args = ap.parse_args(argv)
    doc = json.loads(args.json.read_text())
    mesh, segs, boxes, markers = convert(doc)
    write_h(args.h)
    write_c(args.c, mesh, segs, boxes, markers)
    if args.obj:
        write_obj(
            args.obj,
            mesh,
            segs,
            boxes,
            markers,
            meta=f"from {args.json.name} segs={len(segs)} tris={len(mesh.tris)} verts={len(mesh.verts)}",
        )
    print(
        f"wrote {args.c} {args.h}"
        + (f" {args.obj}" if args.obj else "")
        + f" segs={len(segs)} boxes={len(boxes)} markers={len(markers)}"
        + f" verts={len(mesh.verts)} tris={len(mesh.tris)}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
