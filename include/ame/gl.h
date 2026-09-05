#ifndef AME_GL_H
#define AME_GL_H

#define GL_GLEXT_PROTOTYPES 0
#include <GL/glcorearb.h>

int ame_gl_load(void *(*get_proc)(const char *));

extern PFNGLCREATESHADERPROC ame_glCreateShader;
extern PFNGLSHADERSOURCEPROC ame_glShaderSource;
extern PFNGLCOMPILESHADERPROC ame_glCompileShader;
extern PFNGLGETSHADERIVPROC ame_glGetShaderiv;
extern PFNGLGETSHADERINFOLOGPROC ame_glGetShaderInfoLog;
extern PFNGLDELETESHADERPROC ame_glDeleteShader;
extern PFNGLCREATEPROGRAMPROC ame_glCreateProgram;
extern PFNGLATTACHSHADERPROC ame_glAttachShader;
extern PFNGLLINKPROGRAMPROC ame_glLinkProgram;
extern PFNGLGETPROGRAMIVPROC ame_glGetProgramiv;
extern PFNGLGETPROGRAMINFOLOGPROC ame_glGetProgramInfoLog;
extern PFNGLUSEPROGRAMPROC ame_glUseProgram;
extern PFNGLDELETEPROGRAMPROC ame_glDeleteProgram;
extern PFNGLGETUNIFORMLOCATIONPROC ame_glGetUniformLocation;
extern PFNGLUNIFORM1IPROC ame_glUniform1i;
extern PFNGLUNIFORMMATRIX4FVPROC ame_glUniformMatrix4fv;
extern PFNGLGENVERTEXARRAYSPROC ame_glGenVertexArrays;
extern PFNGLBINDVERTEXARRAYPROC ame_glBindVertexArray;
extern PFNGLDELETEVERTEXARRAYSPROC ame_glDeleteVertexArrays;
extern PFNGLGENBUFFERSPROC ame_glGenBuffers;
extern PFNGLBINDBUFFERPROC ame_glBindBuffer;
extern PFNGLBUFFERDATAPROC ame_glBufferData;
extern PFNGLBUFFERSUBDATAPROC ame_glBufferSubData;
extern PFNGLDELETEBUFFERSPROC ame_glDeleteBuffers;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC ame_glEnableVertexAttribArray;
extern PFNGLVERTEXATTRIBPOINTERPROC ame_glVertexAttribPointer;
extern PFNGLGENTEXTURESPROC ame_glGenTextures;
extern PFNGLDELETETEXTURESPROC ame_glDeleteTextures;
extern PFNGLBINDTEXTUREPROC ame_glBindTexture;
extern PFNGLTEXIMAGE2DPROC ame_glTexImage2D;
extern PFNGLTEXPARAMETERIPROC ame_glTexParameteri;
extern PFNGLACTIVETEXTUREPROC ame_glActiveTexture;
extern PFNGLENABLEPROC ame_glEnable;
extern PFNGLDISABLEPROC ame_glDisable;
extern PFNGLDEPTHFUNCPROC ame_glDepthFunc;
extern PFNGLBLENDFUNCPROC ame_glBlendFunc;
extern PFNGLCLEARPROC ame_glClear;
extern PFNGLCLEARCOLORPROC ame_glClearColor;
extern PFNGLVIEWPORTPROC ame_glViewport;
extern PFNGLDRAWARRAYSPROC ame_glDrawArrays;
extern PFNGLDRAWELEMENTSPROC ame_glDrawElements;
extern PFNGLCULLFACEPROC ame_glCullFace;
extern PFNGLPIXELSTOREIPROC ame_glPixelStorei;

#define glCreateShader            ame_glCreateShader
#define glShaderSource            ame_glShaderSource
#define glCompileShader           ame_glCompileShader
#define glGetShaderiv             ame_glGetShaderiv
#define glGetShaderInfoLog        ame_glGetShaderInfoLog
#define glDeleteShader            ame_glDeleteShader
#define glCreateProgram           ame_glCreateProgram
#define glAttachShader            ame_glAttachShader
#define glLinkProgram             ame_glLinkProgram
#define glGetProgramiv            ame_glGetProgramiv
#define glGetProgramInfoLog       ame_glGetProgramInfoLog
#define glUseProgram              ame_glUseProgram
#define glDeleteProgram           ame_glDeleteProgram
#define glGetUniformLocation      ame_glGetUniformLocation
#define glUniform1i               ame_glUniform1i
#define glUniformMatrix4fv        ame_glUniformMatrix4fv
#define glGenVertexArrays         ame_glGenVertexArrays
#define glBindVertexArray         ame_glBindVertexArray
#define glDeleteVertexArrays      ame_glDeleteVertexArrays
#define glGenBuffers              ame_glGenBuffers
#define glBindBuffer              ame_glBindBuffer
#define glBufferData              ame_glBufferData
#define glBufferSubData           ame_glBufferSubData
#define glDeleteBuffers           ame_glDeleteBuffers
#define glEnableVertexAttribArray ame_glEnableVertexAttribArray
#define glVertexAttribPointer     ame_glVertexAttribPointer
#define glGenTextures             ame_glGenTextures
#define glDeleteTextures          ame_glDeleteTextures
#define glBindTexture             ame_glBindTexture
#define glTexImage2D              ame_glTexImage2D
#define glTexParameteri           ame_glTexParameteri
#define glActiveTexture           ame_glActiveTexture
#define glEnable                  ame_glEnable
#define glDisable                 ame_glDisable
#define glDepthFunc               ame_glDepthFunc
#define glBlendFunc               ame_glBlendFunc
#define glClear                   ame_glClear
#define glClearColor              ame_glClearColor
#define glViewport                ame_glViewport
#define glDrawArrays              ame_glDrawArrays
#define glDrawElements            ame_glDrawElements
#define glCullFace                ame_glCullFace
#define glPixelStorei             ame_glPixelStorei

#endif
