#ifndef GL_LOADER_H
#define GL_LOADER_H

#include <SDL3/SDL.h>
#include <stdbool.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/gl.h>
#endif

// Minimal set of OpenGL 4.5 function pointers we need
// We keep prototypes compatible with GL core profile

typedef void (APIENTRYP PFNGLGENVERTEXARRAYSPROC)(GLsizei n, GLuint* arrays);
typedef void (APIENTRYP PFNGLBINDVERTEXARRAYPROC)(GLuint array);
typedef void (APIENTRYP PFNGLDELETEVERTEXARRAYSPROC)(GLsizei n, const GLuint* arrays);

typedef void (APIENTRYP PFNGLGENBUFFERSPROC)(GLsizei n, GLuint* buffers);
typedef void (APIENTRYP PFNGLBINDBUFFERPROC)(GLenum target, GLuint buffer);
typedef void (APIENTRYP PFNGLBUFFERDATAPROC)(GLenum target, ptrdiff_t size, const void* data, GLenum usage);
typedef void (APIENTRYP PFNGLBUFFERSUBDATAPROC)(GLenum target, ptrdiff_t offset, ptrdiff_t size, const void* data);
typedef void (APIENTRYP PFNGLDELETEBUFFERSPROC)(GLsizei n, const GLuint* buffers);

typedef GLuint (APIENTRYP PFNGLCREATESHADERPROC)(GLenum type);
typedef void (APIENTRYP PFNGLSHADERSOURCEPROC)(GLuint shader, GLsizei count, const char* const* string, const GLint* length);
typedef void (APIENTRYP PFNGLCOMPILESHADERPROC)(GLuint shader);
typedef void (APIENTRYP PFNGLGETSHADERIVPROC)(GLuint shader, GLenum pname, GLint* param);
typedef void (APIENTRYP PFNGLGETSHADERINFOLOGPROC)(GLuint shader, GLsizei bufSize, GLsizei* length, char* infoLog);

typedef GLuint (APIENTRYP PFNGLCREATEPROGRAMPROC)(void);
typedef void (APIENTRYP PFNGLATTACHSHADERPROC)(GLuint program, GLuint shader);
typedef void (APIENTRYP PFNGLLINKPROGRAMPROC)(GLuint program);
typedef void (APIENTRYP PFNGLGETPROGRAMIVPROC)(GLuint program, GLenum pname, GLint* param);
typedef void (APIENTRYP PFNGLGETPROGRAMINFOLOGPROC)(GLuint program, GLsizei bufSize, GLsizei* length, char* infoLog);
typedef void (APIENTRYP PFNGLUSEPROGRAMPROC)(GLuint program);

typedef GLint (APIENTRYP PFNGLGETUNIFORMLOCATIONPROC)(GLuint program, const char* name);
typedef void (APIENTRYP PFNGLUNIFORMMATRIX4FVPROC)(GLint location, GLsizei count, GLboolean transpose, const float* value);
typedef void (APIENTRYP PFNGLUNIFORM4FPROC)(GLint location, float v0, float v1, float v2, float v3);

typedef void (APIENTRYP PFNGLVERTEXATTRIBPOINTERPROC)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer);
typedef void (APIENTRYP PFNGLENABLEVERTEXATTRIBARRAYPROC)(GLuint index);

typedef void (APIENTRYP PFNGLBINDVERTEXBUFFERPROC)(GLuint bindingindex, GLuint buffer, ptrdiff_t offset, GLsizei stride);

typedef void (APIENTRYP PFNGLGETINTEGERVPROC)(GLenum pname, GLint* data);

typedef void (APIENTRYP PFNGLPOLYGONMODEPROC)(GLenum face, GLenum mode);

typedef void* (APIENTRYP PFNGLGETPROCADDRESS)(const char*);

extern PFNGLGENVERTEXARRAYSPROC glGenVertexArrays_;
extern PFNGLBINDVERTEXARRAYPROC glBindVertexArray_;
extern PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays_;

extern PFNGLGENBUFFERSPROC glGenBuffers_;
extern PFNGLBINDBUFFERPROC glBindBuffer_;
extern PFNGLBUFFERDATAPROC glBufferData_;
extern PFNGLBUFFERSUBDATAPROC glBufferSubData_;
extern PFNGLDELETEBUFFERSPROC glDeleteBuffers_;

extern PFNGLCREATESHADERPROC glCreateShader_;
extern PFNGLSHADERSOURCEPROC glShaderSource_;
extern PFNGLCOMPILESHADERPROC glCompileShader_;
extern PFNGLGETSHADERIVPROC glGetShaderiv_;
extern PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog_;

extern PFNGLCREATEPROGRAMPROC glCreateProgram_;
extern PFNGLATTACHSHADERPROC glAttachShader_;
extern PFNGLLINKPROGRAMPROC glLinkProgram_;
extern PFNGLGETPROGRAMIVPROC glGetProgramiv_;
extern PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_;
extern PFNGLUSEPROGRAMPROC glUseProgram_;

extern PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation_;
extern PFNGLUNIFORMMATRIX4FVPROC glUniformMatrix4fv_;
extern PFNGLUNIFORM4FPROC glUniform4f_;

extern PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer_;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray_;

extern PFNGLBINDVERTEXBUFFERPROC glBindVertexBuffer_;

extern PFNGLGETINTEGERVPROC glGetIntegerv_;
extern PFNGLPOLYGONMODEPROC glPolygonMode_;

bool gl_load_all(SDL_FunctionPointer (*get_proc)(const char*));

#endif // GL_LOADER_H
