#include "ame/gl.h"

#include <stdio.h>

PFNGLCREATESHADERPROC ame_glCreateShader;
PFNGLSHADERSOURCEPROC ame_glShaderSource;
PFNGLCOMPILESHADERPROC ame_glCompileShader;
PFNGLGETSHADERIVPROC ame_glGetShaderiv;
PFNGLGETSHADERINFOLOGPROC ame_glGetShaderInfoLog;
PFNGLDELETESHADERPROC ame_glDeleteShader;
PFNGLCREATEPROGRAMPROC ame_glCreateProgram;
PFNGLATTACHSHADERPROC ame_glAttachShader;
PFNGLLINKPROGRAMPROC ame_glLinkProgram;
PFNGLGETPROGRAMIVPROC ame_glGetProgramiv;
PFNGLGETPROGRAMINFOLOGPROC ame_glGetProgramInfoLog;
PFNGLUSEPROGRAMPROC ame_glUseProgram;
PFNGLDELETEPROGRAMPROC ame_glDeleteProgram;
PFNGLGETUNIFORMLOCATIONPROC ame_glGetUniformLocation;
PFNGLUNIFORM1IPROC ame_glUniform1i;
PFNGLUNIFORMMATRIX4FVPROC ame_glUniformMatrix4fv;
PFNGLGENVERTEXARRAYSPROC ame_glGenVertexArrays;
PFNGLBINDVERTEXARRAYPROC ame_glBindVertexArray;
PFNGLDELETEVERTEXARRAYSPROC ame_glDeleteVertexArrays;
PFNGLGENBUFFERSPROC ame_glGenBuffers;
PFNGLBINDBUFFERPROC ame_glBindBuffer;
PFNGLBUFFERDATAPROC ame_glBufferData;
PFNGLBUFFERSUBDATAPROC ame_glBufferSubData;
PFNGLDELETEBUFFERSPROC ame_glDeleteBuffers;
PFNGLENABLEVERTEXATTRIBARRAYPROC ame_glEnableVertexAttribArray;
PFNGLVERTEXATTRIBPOINTERPROC ame_glVertexAttribPointer;
PFNGLGENTEXTURESPROC ame_glGenTextures;
PFNGLDELETETEXTURESPROC ame_glDeleteTextures;
PFNGLBINDTEXTUREPROC ame_glBindTexture;
PFNGLTEXIMAGE2DPROC ame_glTexImage2D;
PFNGLTEXPARAMETERIPROC ame_glTexParameteri;
PFNGLACTIVETEXTUREPROC ame_glActiveTexture;
PFNGLENABLEPROC ame_glEnable;
PFNGLDISABLEPROC ame_glDisable;
PFNGLDEPTHFUNCPROC ame_glDepthFunc;
PFNGLBLENDFUNCPROC ame_glBlendFunc;
PFNGLCLEARPROC ame_glClear;
PFNGLCLEARCOLORPROC ame_glClearColor;
PFNGLVIEWPORTPROC ame_glViewport;
PFNGLDRAWARRAYSPROC ame_glDrawArrays;
PFNGLDRAWELEMENTSPROC ame_glDrawElements;
PFNGLCULLFACEPROC ame_glCullFace;
PFNGLPIXELSTOREIPROC ame_glPixelStorei;

int ame_gl_load(void *(*get_proc)(const char *))
{
    int ok = 1;
#define LOAD(name) do { \
        ame_##name = (void *)get_proc(#name); \
        if (!ame_##name) { fprintf(stderr, "GL missing %s\n", #name); ok = 0; } \
    } while (0)
    LOAD(glCreateShader);
    LOAD(glShaderSource);
    LOAD(glCompileShader);
    LOAD(glGetShaderiv);
    LOAD(glGetShaderInfoLog);
    LOAD(glDeleteShader);
    LOAD(glCreateProgram);
    LOAD(glAttachShader);
    LOAD(glLinkProgram);
    LOAD(glGetProgramiv);
    LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glDeleteProgram);
    LOAD(glGetUniformLocation);
    LOAD(glUniform1i);
    LOAD(glUniformMatrix4fv);
    LOAD(glGenVertexArrays);
    LOAD(glBindVertexArray);
    LOAD(glDeleteVertexArrays);
    LOAD(glGenBuffers);
    LOAD(glBindBuffer);
    LOAD(glBufferData);
    LOAD(glBufferSubData);
    LOAD(glDeleteBuffers);
    LOAD(glEnableVertexAttribArray);
    LOAD(glVertexAttribPointer);
    LOAD(glGenTextures);
    LOAD(glDeleteTextures);
    LOAD(glBindTexture);
    LOAD(glTexImage2D);
    LOAD(glTexParameteri);
    LOAD(glActiveTexture);
    LOAD(glEnable);
    LOAD(glDisable);
    LOAD(glDepthFunc);
    LOAD(glBlendFunc);
    LOAD(glClear);
    LOAD(glClearColor);
    LOAD(glViewport);
    LOAD(glDrawArrays);
    LOAD(glDrawElements);
    LOAD(glCullFace);
    LOAD(glPixelStorei);
#undef LOAD
    return ok;
}
