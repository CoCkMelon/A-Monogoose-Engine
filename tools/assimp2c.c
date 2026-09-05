/* assimp2c — build-time model baker (levels.txt: "model geometry via
 * Assimp (build-time tool) - do not hand-write a runtime .obj parser").
 *
 * Reads any Assimp-supported model file, triangulates + generates
 * smooth normals and UVs, emits a C source file with one mesh
 * (positions/normals/uvs + triangle indices) as plain arrays.
 *
 * Usage: assimp2c <model> <symbol> <out.c>
 * The emitted file defines:
 *   const int <symbol>_vert_count;
 *   const float <symbol>_verts[][8];   // pos xyz, nrm xyz, uv
 *   const int <symbol>_idx_count;
 *   const unsigned int <symbol>_idx[];
 * Build the tool when Assimp exists; commit BAKED outputs so the
 * engine/tests never need Assimp installed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <assimp/cimport.h>
#include <assimp/cfileio.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: %s <model> <symbol> <out.c>\n", argv[0]);
        return 2;
    }
    const char *model = argv[1], *sym = argv[2], *out_path = argv[3];

    const struct aiScene *scene = aiImportFile(
        model, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices
                   | aiProcess_GenSmoothNormals | aiProcess_GenUVCoords
                   | aiProcess_FlipUVs);
    if (!scene) {
        fprintf(stderr, "assimp2c: %s: %s\n", model, aiGetErrorString());
        return 1;
    }
    if (scene->mNumMeshes < 1 || !scene->mMeshes[0]->mVertices) {
        fprintf(stderr, "assimp2c: no geometry in %s\n", model);
        aiReleaseImport(scene);
        return 1;
    }
    /* single mesh per bake (scenes with several meshes: bake per mesh
     * by exporting per-node, or extend when a real game needs it) */
    const struct aiMesh *m = scene->mMeshes[0];

    FILE *f = fopen(out_path, "w");
    if (!f) {
        fprintf(stderr, "assimp2c: cannot write %s\n", out_path);
        aiReleaseImport(scene);
        return 1;
    }
    fprintf(f, "/* baked by tools/assimp2c.c from %s - do not edit */\n",
            model);
    fprintf(f, "#include <stddef.h>\n");
    fprintf(f, "const int %s_vert_count = %u;\n", sym,
            (unsigned)m->mNumVertices);
    fprintf(f, "const float %s_verts[][8] = {\n", sym);
    for (unsigned i = 0; i < m->mNumVertices; i++) {
        const struct aiVector3D p = m->mVertices[i];
        const struct aiVector3D n =
            m->mNormals ? m->mNormals[i]
                        : (struct aiVector3D){ 0, 1, 0 };
        float u = 0, v = 0;
        if (m->mTextureCoords[0]) {
            u = m->mTextureCoords[0][i].x;
            v = m->mTextureCoords[0][i].y;
        }
        fprintf(f, "    { %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff, %.6ff },\n",
                p.x, p.y, p.z, n.x, n.y, n.z, u, v);
    }
    fprintf(f, "};\n");
    unsigned idx_total = 0;
    for (unsigned i = 0; i < m->mNumFaces; i++)
        idx_total += m->mFaces[i].mNumIndices; /* triangles: 3 each */
    fprintf(f, "const int %s_idx_count = %u;\n", sym, idx_total);
    fprintf(f, "const unsigned int %s_idx[] = {\n", sym);
    for (unsigned i = 0; i < m->mNumFaces; i++) {
        if (m->mFaces[i].mNumIndices != 3)
            continue; /* triangulated, but never emit garbage */
        fprintf(f, "    %u, %u, %u,\n",
                (unsigned)m->mFaces[i].mIndices[0],
                (unsigned)m->mFaces[i].mIndices[1],
                (unsigned)m->mFaces[i].mIndices[2]);
    }
    fprintf(f, "};\n");
    fclose(f);
    printf("assimp2c: %s -> %s (%u verts, %u indices)\n", model, out_path,
           (unsigned)m->mNumVertices, idx_total);
    aiReleaseImport(scene);
    return 0;
}
