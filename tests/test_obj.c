/* test_obj — the runtime OBJ reader (levels.txt dev path).
 *
 * Proves the dual-path contract: parsing OBJ text yields byte-identical
 * ame_mesh_vert/index data to the assimp2c bake output (baked_cube.h),
 * plus the marker support (o/g groups), negative indices, CRLF, n-gons,
 * flat-normal fallback and the failure paths.
 */
#include <ame/mesh.h>
#include <ame/obj.h>

#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "assets/baked_cube.h"
#include "utest.h"

#define TMP_OBJ "ame_obj_test_tmp.obj"

static int write_tmp(const char *text)
{
    FILE *f = fopen(TMP_OBJ, "wb");
    if (!f)
        return -1;
    fputs(text, f);
    fclose(f);
    return 0;
}

static const char *CUBE_OBJ =
    "# cube matching tests/assets/baked_cube.h (assimp2c output)\n"
    "v -0.500000 -0.500000 -0.500000\n"
    "v -0.500000 0.500000 -0.500000\n"
    "v 0.500000 0.500000 -0.500000\n"
    "v 0.500000 -0.500000 -0.500000\n"
    "v -0.500000 -0.500000 0.500000\n"
    "v 0.500000 -0.500000 0.500000\n"
    "v 0.500000 0.500000 0.500000\n"
    "v -0.500000 0.500000 0.500000\n"
    "vt 0.000000 0.000000\n"
    "vn -0.577350 -0.577350 -0.577350\n"
    "vn -0.577350 0.577350 -0.577350\n"
    "vn 0.577350 0.577350 -0.577350\n"
    "vn 0.577350 -0.577350 -0.577350\n"
    "vn -0.577350 -0.577350 0.577350\n"
    "vn 0.577350 -0.577350 0.577350\n"
    "vn 0.577350 0.577350 0.577350\n"
    "vn -0.577350 0.577350 0.577350\n"
    "f 1/1/1 2/1/2 3/1/3\n"
    "f 1/1/1 3/1/3 4/1/4\n"
    "f 5/1/5 6/1/6 7/1/7\n"
    "f 5/1/5 7/1/7 8/1/8\n"
    "f 1/1/1 4/1/4 6/1/6\n"
    "f 1/1/1 6/1/6 5/1/5\n"
    "f 4/1/4 3/1/3 7/1/7\n"
    "f 4/1/4 7/1/7 6/1/6\n"
    "f 3/1/3 2/1/2 8/1/8\n"
    "f 3/1/3 8/1/8 7/1/7\n"
    "f 2/1/2 1/1/1 5/1/5\n"
    "f 2/1/2 5/1/5 8/1/8\n";

int main(void)
{
    printf("=== test_obj (runtime OBJ reader) ===\n");

    UT_CASE("bake-path parity: same type, same triangles as assimp2c");
    {
        /* the baked C array (float[8]/vert) must literally BE an
         * ame_mesh_vert array: same stride, same field offsets */
        const ame_mesh_vert *baked = (const ame_mesh_vert *)baked_cube_verts;
        UT_ASSERT(sizeof(ame_mesh_vert) == 8 * sizeof(float));
        UT_ASSERT(offsetof(ame_mesh_vert, pos) == 0);
        UT_ASSERT(offsetof(ame_mesh_vert, nrm) == 3 * sizeof(float));
        UT_ASSERT(offsetof(ame_mesh_vert, uv) == 6 * sizeof(float));
        UT_ASSERT(baked[0].pos[0] == -0.5f && baked[7].nrm[1] == 0.577350f);

        ame_mesh m;
        UT_ASSERT(ame_obj_parse(CUBE_OBJ, &m) == 0);
        UT_ASSERTF(m.idx_count == baked_cube_idx_count,
                   "idx_count %d != baked %d", m.idx_count,
                   baked_cube_idx_count);
        /* same triangle stream: corner-for-corner identical vertex data
         * (the OBJ path expands shared verts per corner; the triangles
         * it produces are the baked triangles, byte for byte) */
        for (int i = 0; i < m.idx_count; i++) {
            UT_ASSERTF(memcmp(&m.verts[m.idx[i]], &baked[baked_cube_idx[i]],
                              sizeof(ame_mesh_vert))
                           == 0,
                       "triangle %d corner %d differs from baked data",
                       i / 3, i % 3);
        }
        UT_ASSERT(m.group_count == 0);
        ame_mesh_free(&m);
        UT_ASSERT(m.verts == NULL && m.vert_count == 0);
        UT_OK();
    }

    UT_CASE("quad fan-triangulates; vt/vn map per corner");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                                "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                                "vn 0 0 1\n"
                                "f 1/1/1 2/2/1 3/3/1 4/4/1\n",
                                &m)
                  == 0);
        UT_ASSERTF(m.vert_count == 6, "quad -> 6 corner verts, got %d",
                   m.vert_count);
        UT_ASSERTF(m.idx_count == 6, "quad -> 2 tris, got %d idx", m.idx_count);
        /* one vertex per corner: indices run 0..5; the FAN is in the
         * vertex order (tri2 = corner0, corner2, corner3) */
        for (int i = 0; i < 6; i++)
            UT_ASSERTF(m.idx[i] == (unsigned)i, "idx[%d]=%u", i, m.idx[i]);
        /* corner 2 of the quad: pos (1,1,0), uv (1,1), nrm +z */
        UT_ASSERT(m.verts[2].pos[0] == 1.0f && m.verts[2].pos[1] == 1.0f);
        UT_ASSERT(m.verts[2].uv[0] == 1.0f && m.verts[2].uv[1] == 1.0f);
        UT_ASSERT(m.verts[2].nrm[2] == 1.0f);
        /* tri 2 re-emits corner0 then corner2 then corner3 */
        UT_ASSERT(m.verts[3].pos[0] == 0.0f && m.verts[3].pos[1] == 0.0f);
        UT_ASSERT(m.verts[4].pos[0] == 1.0f && m.verts[4].pos[1] == 1.0f);
        UT_ASSERT(m.verts[5].pos[0] == 0.0f && m.verts[5].pos[1] == 1.0f);
        /* corner with no vt in the file gets uv 0,0 */
        ame_mesh_free(&m);
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", &m)
                  == 0);
        UT_ASSERT(m.verts[0].uv[0] == 0.0f && m.verts[0].uv[1] == 0.0f);
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("negative (relative) indices resolve");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 1 1 0\n"
                                "f -3 -2 -1\n",
                                &m)
                  == 0);
        UT_ASSERT(m.vert_count == 3 && m.idx_count == 3);
        UT_ASSERT(m.verts[0].pos[0] == 0.0f);
        UT_ASSERT(m.verts[1].pos[0] == 1.0f);
        UT_ASSERT(m.verts[2].pos[1] == 1.0f);
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("o/g names become groups with index ranges");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse("o K_col_wall\n"
                                "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                                "f 1 2 3\n"
                                "g E_slime_01\n"
                                "v 5 0 0\nv 6 0 0\nv 5 1 0\n"
                                "f 4 5 6\n"
                                "o P_player\n"
                                "# an empty object contributes no group\n",
                                &m)
                  == 0);
        UT_ASSERTF(m.group_count == 2, "group_count %d != 2", m.group_count);
        UT_ASSERT(strcmp(m.groups[0].name, "K_col_wall") == 0);
        UT_ASSERT(m.groups[0].first_idx == 0 && m.groups[0].idx_count == 3);
        UT_ASSERT(strcmp(m.groups[1].name, "E_slime_01") == 0);
        UT_ASSERT(m.groups[1].first_idx == 3 && m.groups[1].idx_count == 3);
        UT_ASSERT(ame_mesh_group_find(&m, "E_slime_01") == &m.groups[1]);
        UT_ASSERT(ame_mesh_group_find(&m, "nope") == NULL);
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("CRLF line endings parse");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse("o C_cookie_03\r\n"
                                "v 0 0 0\r\nv 1 0 0\r\nv 0 1 0\r\n"
                                "f 1 2 3\r\n",
                                &m)
                  == 0);
        UT_ASSERT(m.idx_count == 3);
        UT_ASSERT(m.group_count == 1);
        UT_ASSERT(strcmp(m.groups[0].name, "C_cookie_03") == 0);
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("missing vn -> flat unit face normal");
    {
        ame_mesh m;
        /* CCW in XY: normal must come out +Z, unit length */
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n", &m)
                  == 0);
        for (int i = 0; i < m.vert_count; i++) {
            UT_ASSERT_NEAR(m.verts[i].nrm[0], 0.0f, 1e-6f);
            UT_ASSERT_NEAR(m.verts[i].nrm[1], 0.0f, 1e-6f);
            UT_ASSERT_NEAR(m.verts[i].nrm[2], 1.0f, 1e-6f);
        }
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("n-gon fan-triangulates (pentagon -> 3 tris)");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 1.5 1 0\nv 0.5 1.5 0\n"
                                "v -0.5 1 0\n"
                                "f 1 2 3 4 5\n",
                                &m)
                  == 0);
        UT_ASSERTF(m.idx_count == 9, "pentagon -> 9 idx, got %d", m.idx_count);
        UT_ASSERT(m.vert_count == 9);
        ame_mesh_free(&m);
        UT_OK();
    }

    UT_CASE("load_file round-trip");
    {
        UT_ASSERT(write_tmp(CUBE_OBJ) == 0);
        ame_mesh m;
        UT_ASSERT(ame_obj_load_file(TMP_OBJ, &m) == 0);
        /* one vertex per corner: verts == indices == baked triangle idx */
        UT_ASSERT(m.vert_count == baked_cube_idx_count);
        UT_ASSERT(m.idx_count == baked_cube_idx_count);
        ame_mesh_free(&m);
        remove(TMP_OBJ);
        UT_OK();
    }

    UT_CASE("failure paths set ame_last_error");
    {
        ame_mesh m;
        UT_ASSERT(ame_obj_parse(NULL, &m) == -1);
        UT_ASSERT(ame_last_error_code() == AME_ERR_INVALID_ARGUMENT);
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 0 1 0\n# no faces\n", &m)
                  == -1);
        UT_ASSERT(ame_last_error_code() == AME_ERR_MESH_FORMAT);
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nf 1 2 9\n", &m) == -1);
        UT_ASSERT(ame_last_error_code() == AME_ERR_MESH_FORMAT);
        UT_ASSERT(ame_obj_parse("v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2\n", &m)
                  == -1);
        UT_ASSERT(ame_last_error_code() == AME_ERR_MESH_FORMAT);
        UT_ASSERT(ame_obj_load_file("does/not/exist.obj", &m) == -1);
        UT_ASSERT(ame_last_error_code() == AME_ERR_FILE_OPEN);
        /* a failed parse leaves the mesh zeroed, not half-built */
        UT_ASSERT(m.verts == NULL && m.idx == NULL && m.owned == 0);
        UT_OK();
    }

    return ut_done("test_obj");
}
