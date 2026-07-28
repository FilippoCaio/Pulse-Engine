// ─── Manual OpenGL 3.3 loader: no GLEW/GLAD, functions loaded via wgl ───
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <GL/gl.h>

// ── missing typedefs ──
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

// ── missing constants ──
#define GL_ARRAY_BUFFER                   0x8892
#define GL_ELEMENT_ARRAY_BUFFER           0x8893
#define GL_STATIC_DRAW                    0x88E4
#define GL_DYNAMIC_DRAW                   0x88E8
#define GL_VERTEX_SHADER                  0x8B31
#define GL_FRAGMENT_SHADER                0x8B30
#define GL_COMPILE_STATUS                 0x8B81
#define GL_LINK_STATUS                    0x8B82
#define GL_INFO_LOG_LENGTH                0x8B84
#define GL_TEXTURE0                       0x84C0
#define GL_TEXTURE1                       0x84C1
#define GL_FRAMEBUFFER                    0x8D40
#define GL_DEPTH_ATTACHMENT               0x8D00
#define GL_FRAMEBUFFER_COMPLETE           0x8CD5
#define GL_DEPTH_COMPONENT24              0x81A6
#define GL_TEXTURE_COMPARE_MODE           0x884C
#define GL_TEXTURE_COMPARE_FUNC           0x884D
#define GL_COMPARE_REF_TO_TEXTURE         0x884E
#define GL_CLAMP_TO_EDGE                  0x812F
#define GL_MULTISAMPLE                    0x809D
#define GL_R8                             0x8229
#define GL_RED_CHANNEL                    0x1903  /* GL_RED already defined as 0x1903 in gl.h 1.1 */
#define GL_FRAMEBUFFER_SRGB               0x8DB9

// ── wgl extension constants ──
#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x0001
#define WGL_DRAW_TO_WINDOW_ARB            0x2001
#define WGL_ACCELERATION_ARB              0x2003
#define WGL_FULL_ACCELERATION_ARB         0x2027
#define WGL_SUPPORT_OPENGL_ARB            0x2010
#define WGL_DOUBLE_BUFFER_ARB             0x2011
#define WGL_PIXEL_TYPE_ARB                0x2013
#define WGL_TYPE_RGBA_ARB                 0x202B
#define WGL_COLOR_BITS_ARB                0x2014
#define WGL_DEPTH_BITS_ARB                0x2022
#define WGL_STENCIL_BITS_ARB              0x2023
#define WGL_SAMPLE_BUFFERS_ARB            0x2041
#define WGL_SAMPLES_ARB                   0x2042

// ── function pointer declarations ──
#define GL_FUNCS \
    X(GLuint, glCreateShader, (GLenum type)) \
    X(void, glShaderSource, (GLuint shader, GLsizei count, const GLchar* const* string, const GLint* length)) \
    X(void, glCompileShader, (GLuint shader)) \
    X(void, glGetShaderiv, (GLuint shader, GLenum pname, GLint* params)) \
    X(void, glGetShaderInfoLog, (GLuint shader, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(GLuint, glCreateProgram, (void)) \
    X(void, glAttachShader, (GLuint program, GLuint shader)) \
    X(void, glLinkProgram, (GLuint program)) \
    X(void, glGetProgramiv, (GLuint program, GLenum pname, GLint* params)) \
    X(void, glGetProgramInfoLog, (GLuint program, GLsizei bufSize, GLsizei* length, GLchar* infoLog)) \
    X(void, glUseProgram, (GLuint program)) \
    X(void, glDeleteShader, (GLuint shader)) \
    X(GLint, glGetUniformLocation, (GLuint program, const GLchar* name)) \
    X(void, glUniform1i, (GLint location, GLint v0)) \
    X(void, glUniform1f, (GLint location, GLfloat v0)) \
    X(void, glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
    X(void, glUniform3f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2)) \
    X(void, glUniform4f, (GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3)) \
    X(void, glUniform1fv, (GLint location, GLsizei count, const GLfloat* value)) \
    X(void, glUniform3fv, (GLint location, GLsizei count, const GLfloat* value)) \
    X(void, glUniformMatrix3fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat* value)) \
    X(void, glGenBuffers, (GLsizei n, GLuint* buffers)) \
    X(void, glBindBuffer, (GLenum target, GLuint buffer)) \
    X(void, glBufferData, (GLenum target, GLsizeiptr size, const void* data, GLenum usage)) \
    X(void, glBufferSubData, (GLenum target, GLintptr offset, GLsizeiptr size, const void* data)) \
    X(void, glGenVertexArrays, (GLsizei n, GLuint* arrays)) \
    X(void, glBindVertexArray, (GLuint array)) \
    X(void, glEnableVertexAttribArray, (GLuint index)) \
    X(void, glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void* pointer)) \
    X(void, glActiveTexture, (GLenum texture)) \
    X(void, glGenFramebuffers, (GLsizei n, GLuint* framebuffers)) \
    X(void, glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
    X(void, glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
    X(GLenum, glCheckFramebufferStatus, (GLenum target))

#define X(ret, name, args) typedef ret(WINAPI* PFN_##name) args; extern PFN_##name name;
GL_FUNCS
#undef X

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARB)(HDC, HGLRC, const int*);
typedef BOOL(WINAPI* PFNWGLCHOOSEPIXELFORMATARB)(HDC, const int*, const FLOAT*, UINT, int*, UINT*);
typedef BOOL(WINAPI* PFNWGLSWAPINTERVALEXT)(int);
extern PFNWGLCREATECONTEXTATTRIBSARB wglCreateContextAttribsARB;
extern PFNWGLCHOOSEPIXELFORMATARB wglChoosePixelFormatARB;
extern PFNWGLSWAPINTERVALEXT wglSwapIntervalEXT;

// loads wgl extension pointers (needs a current dummy context)
void loadWGLExtensions();
// loads all GL 2.0+ functions (needs the final context current)
bool loadGLFunctions();
