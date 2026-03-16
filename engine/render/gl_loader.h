#pragma once
// ============================================================================
// Minimal OpenGL 3.3 Core Function Loader
// Uses SDL_GL_GetProcAddress - no GLAD/GLEW dependency needed
// SDL_opengl_glext.h provides all PFN* typedefs, we just load the pointers
// ============================================================================

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

// SDL_opengl.h includes GL/gl.h and GL/glext.h (via SDL_opengl_glext.h)
// which defines all PFNGL*PROC typedefs we need. No manual typedefs required.

// ============================================================================
// Global Function Pointers (defined in gl_loader.cpp)
// ============================================================================
extern PFNGLCREATESHADERPROC             glCreateShader_fp;
extern PFNGLDELETESHADERPROC             glDeleteShader_fp;
extern PFNGLSHADERSOURCEPROC             glShaderSource_fp;
extern PFNGLCOMPILESHADERPROC            glCompileShader_fp;
extern PFNGLGETSHADERIVPROC              glGetShaderiv_fp;
extern PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog_fp;
extern PFNGLCREATEPROGRAMPROC            glCreateProgram_fp;
extern PFNGLDELETEPROGRAMPROC            glDeleteProgram_fp;
extern PFNGLATTACHSHADERPROC             glAttachShader_fp;
extern PFNGLLINKPROGRAMPROC              glLinkProgram_fp;
extern PFNGLUSEPROGRAMPROC               glUseProgram_fp;
extern PFNGLGETPROGRAMIVPROC             glGetProgramiv_fp;
extern PFNGLGETPROGRAMINFOLOGPROC        glGetProgramInfoLog_fp;
extern PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation_fp;
extern PFNGLUNIFORM1IPROC               glUniform1i_fp;
extern PFNGLUNIFORM1FPROC               glUniform1f_fp;
extern PFNGLUNIFORM2FPROC               glUniform2f_fp;
extern PFNGLUNIFORM3FPROC               glUniform3f_fp;
extern PFNGLUNIFORM4FPROC               glUniform4f_fp;
extern PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv_fp;
extern PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays_fp;
extern PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays_fp;
extern PFNGLBINDVERTEXARRAYPROC          glBindVertexArray_fp;
extern PFNGLGENBUFFERSPROC               glGenBuffers_fp;
extern PFNGLDELETEBUFFERSPROC            glDeleteBuffers_fp;
extern PFNGLBINDBUFFERPROC               glBindBuffer_fp;
extern PFNGLBUFFERDATAPROC               glBufferData_fp;
extern PFNGLBUFFERSUBDATAPROC            glBufferSubData_fp;
extern PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray_fp;
extern PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer_fp;
extern PFNGLACTIVETEXTUREPROC            glActiveTexture_fp;
extern PFNGLGENERATEMIPMAPPROC           glGenerateMipmap_fp;
extern PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers_fp;
extern PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers_fp;
extern PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer_fp;
extern PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D_fp;
extern PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus_fp;

// ============================================================================
// Convenience macros so code reads like normal GL calls
// ============================================================================
#undef glCreateShader
#undef glDeleteShader
#undef glShaderSource
#undef glCompileShader
#undef glGetShaderiv
#undef glGetShaderInfoLog
#undef glCreateProgram
#undef glDeleteProgram
#undef glAttachShader
#undef glLinkProgram
#undef glUseProgram
#undef glGetProgramiv
#undef glGetProgramInfoLog
#undef glGetUniformLocation
#undef glUniform1i
#undef glUniform1f
#undef glUniform2f
#undef glUniform3f
#undef glUniform4f
#undef glUniformMatrix4fv
#undef glGenVertexArrays
#undef glDeleteVertexArrays
#undef glBindVertexArray
#undef glGenBuffers
#undef glDeleteBuffers
#undef glBindBuffer
#undef glBufferData
#undef glBufferSubData
#undef glEnableVertexAttribArray
#undef glVertexAttribPointer
#undef glActiveTexture
#undef glGenerateMipmap
#undef glGenFramebuffers
#undef glDeleteFramebuffers
#undef glBindFramebuffer
#undef glFramebufferTexture2D
#undef glCheckFramebufferStatus

#define glCreateShader          glCreateShader_fp
#define glDeleteShader          glDeleteShader_fp
#define glShaderSource          glShaderSource_fp
#define glCompileShader         glCompileShader_fp
#define glGetShaderiv           glGetShaderiv_fp
#define glGetShaderInfoLog      glGetShaderInfoLog_fp
#define glCreateProgram         glCreateProgram_fp
#define glDeleteProgram         glDeleteProgram_fp
#define glAttachShader          glAttachShader_fp
#define glLinkProgram           glLinkProgram_fp
#define glUseProgram            glUseProgram_fp
#define glGetProgramiv          glGetProgramiv_fp
#define glGetProgramInfoLog     glGetProgramInfoLog_fp
#define glGetUniformLocation    glGetUniformLocation_fp
#define glUniform1i             glUniform1i_fp
#define glUniform1f             glUniform1f_fp
#define glUniform2f             glUniform2f_fp
#define glUniform3f             glUniform3f_fp
#define glUniform4f             glUniform4f_fp
#define glUniformMatrix4fv      glUniformMatrix4fv_fp
#define glGenVertexArrays       glGenVertexArrays_fp
#define glDeleteVertexArrays    glDeleteVertexArrays_fp
#define glBindVertexArray       glBindVertexArray_fp
#define glGenBuffers            glGenBuffers_fp
#define glDeleteBuffers         glDeleteBuffers_fp
#define glBindBuffer            glBindBuffer_fp
#define glBufferData            glBufferData_fp
#define glBufferSubData         glBufferSubData_fp
#define glEnableVertexAttribArray glEnableVertexAttribArray_fp
#define glVertexAttribPointer   glVertexAttribPointer_fp
#define glActiveTexture         glActiveTexture_fp
#define glGenerateMipmap        glGenerateMipmap_fp
#define glGenFramebuffers       glGenFramebuffers_fp
#define glDeleteFramebuffers    glDeleteFramebuffers_fp
#define glBindFramebuffer       glBindFramebuffer_fp
#define glFramebufferTexture2D  glFramebufferTexture2D_fp
#define glCheckFramebufferStatus glCheckFramebufferStatus_fp

// ============================================================================
// Loader function (call after creating SDL GL context)
// ============================================================================
namespace fate {
    bool loadGLFunctions();
}
