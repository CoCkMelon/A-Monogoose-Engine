#include "gl_loader.h"
#include <string.h>
#include <stdio.h>

#define LOAD(name) do { \
    name##_ = (void*)SDL_GL_GetProcAddress(#name); \
    if (!(name##_)) { /* some drivers omit underscoreless names; try variations if needed */ } \
} while (0)

PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_ = NULL;
PFNGLBINDVERTEXARRAYPROC glBindVertexArray_ = NULL;
PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays_ = NULL;

PFNGLGENBUFFERSPROC glGenBuffers_ = NULL;
PFNGLBINDBUFFERPROC glBindBuffer_ = NULL;
PFNGLBUFFERDATAPROC glBufferData_ = NULL;
PFNGLBUFFERSUBDATAPROC glBufferSubData_ = NULL;
PFNGLDELETEBUFFERSPROC glDeleteBuffers_ = NULL;

PFNGLCREATESHADERPROC glCreateShader_ = NULL;
PFNGLSHADERSOURCEPROC glShaderSource_ = NULL;
PFNGLCOMPILESHADERPROC glCompileShader_ = NULL;
PFNGLGETSHADERIVPROC glGetShaderiv_ = NULL;
PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_ = NULL;

PFNGLCREATEPROGRAMPROC glCreateProgram_ = NULL;
PFNGLATTACHSHADERPROC glAttachShader_ = NULL;
PFNGLLINKPROGRAMPROC glLinkProgram_ = NULL;
PFNGLGETPROGRAMIVPROC glGetProgramiv_ = NULL;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = NULL;
PFNGLUSEPROGRAMPROC glUseProgram_ = NULL;

PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_ = NULL;
PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_ = NULL;
PFNGLUNIFORM1FPROC glUniform1f_ = NULL;
PFNGLUNIFORM2FPROC glUniform2f_ = NULL;
PFNGLUNIFORM3FPROC glUniform3f_ = NULL;
PFNGLUNIFORM4FPROC glUniform4f_ = NULL;
PFNGLUNIFORM1IPROC glUniform1i_ = NULL;

PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_ = NULL;
PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_ = NULL;
PFNGLDISABLEVERTEXATTRIBARRAYPROC glDisableVertexAttribArray_ = NULL;

PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer_ = NULL;

PFNGLGETINTEGERVPROC glGetIntegerv_ = NULL;
PFNGLPOLYGONMODEPROC glPolygonMode_ = NULL;

PFNGLGENTEXTURESPROC glGenTextures_ = NULL;
PFNGLDELETETEXTURESPROC glDeleteTextures_ = NULL;
PFNGLBINDTEXTUREPROC glBindTexture_ = NULL;
PFNGLTEXPARAMETERIPROC glTexParameteri_ = NULL;
PFNGLTEXIMAGE2DPROC glTexImage2D_ = NULL;
PFNGLACTIVETEXTUREPROC glActiveTexture_ = NULL;

bool gl_load_all(SDL_FunctionPointer (*get_proc)(const char*)) {
    (void)get_proc; // We will use SDL_GL_GetProcAddress directly
    LOAD(glGenVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glDeleteVertexArrays);

    LOAD(glGenBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glBufferSubData);
    LOAD(glDeleteBuffers);

    LOAD(glCreateShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);

    LOAD(glCreateProgram);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);

    LOAD(glGetUniformLocation);
LOAD(glUniformMatrix4fv);
LOAD(glUniform1f);
LOAD(glUniform2f);
LOAD(glUniform3f);
LOAD(glUniform4f);
LOAD(glUniform1i);

    LOAD(glVertexAttribPointer);
    LOAD(glEnableVertexAttribArray);
    LOAD(glDisableVertexAttribArray);

    LOAD(glBindVertexBuffer);

    LOAD(glGetIntegerv);
    LOAD(glPolygonMode);

    // Textures
    LOAD(glGenTextures);
    LOAD(glDeleteTextures);
    LOAD(glBindTexture);
    LOAD(glTexParameteri);
    LOAD(glTexImage2D);
    LOAD(glActiveTexture);

    // Minimal validation: ensure a few critical pointers are loaded
    return glGenVertexArrays_ && glCreateShader_ && glCreateProgram_ && glBufferData_ && glVertexAttribPointer_;
}
