// gl_loader.cpp - Load OpenGL function pointers
// Desktop: Load via SDL_GL_GetProcAddress at runtime
// iOS (GLES): All functions linked statically — loadGLFunctions() is a no-op
//
// NOTE: We must NOT include gl_loader.h here because it #defines GL function
// names to our _fp pointers, which would break the LOAD macro expansion.
// Instead we include SDL directly and redeclare what we need.

#include "engine/core/logger.h"

#ifdef FATEMMO_GLES

namespace fate {
bool loadGLFunctions() {
    // All GL ES functions are linked statically on iOS
    return true;
}
} // namespace fate

#else // Desktop GL — runtime loading via SDL

#ifdef _WIN32
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
#endif

#include <SDL.h>
#include <SDL_opengl.h>

// ============================================================================
// Define all function pointers (initially null)
// ============================================================================
PFNGLCREATESHADERPROC             glCreateShader_fp = nullptr;
PFNGLDELETESHADERPROC             glDeleteShader_fp = nullptr;
PFNGLSHADERSOURCEPROC             glShaderSource_fp = nullptr;
PFNGLCOMPILESHADERPROC            glCompileShader_fp = nullptr;
PFNGLGETSHADERIVPROC              glGetShaderiv_fp = nullptr;
PFNGLGETSHADERINFOLOGPROC         glGetShaderInfoLog_fp = nullptr;
PFNGLCREATEPROGRAMPROC            glCreateProgram_fp = nullptr;
PFNGLDELETEPROGRAMPROC            glDeleteProgram_fp = nullptr;
PFNGLATTACHSHADERPROC             glAttachShader_fp = nullptr;
PFNGLLINKPROGRAMPROC              glLinkProgram_fp = nullptr;
PFNGLUSEPROGRAMPROC               glUseProgram_fp = nullptr;
PFNGLGETPROGRAMIVPROC             glGetProgramiv_fp = nullptr;
PFNGLGETPROGRAMINFOLOGPROC        glGetProgramInfoLog_fp = nullptr;
PFNGLGETUNIFORMLOCATIONPROC       glGetUniformLocation_fp = nullptr;
PFNGLUNIFORM1IPROC               glUniform1i_fp = nullptr;
PFNGLUNIFORM1FPROC               glUniform1f_fp = nullptr;
PFNGLUNIFORM2FPROC               glUniform2f_fp = nullptr;
PFNGLUNIFORM3FPROC               glUniform3f_fp = nullptr;
PFNGLUNIFORM4FPROC               glUniform4f_fp = nullptr;
PFNGLUNIFORMMATRIX4FVPROC        glUniformMatrix4fv_fp = nullptr;
PFNGLGENVERTEXARRAYSPROC          glGenVertexArrays_fp = nullptr;
PFNGLDELETEVERTEXARRAYSPROC       glDeleteVertexArrays_fp = nullptr;
PFNGLBINDVERTEXARRAYPROC          glBindVertexArray_fp = nullptr;
PFNGLGENBUFFERSPROC               glGenBuffers_fp = nullptr;
PFNGLDELETEBUFFERSPROC            glDeleteBuffers_fp = nullptr;
PFNGLBINDBUFFERPROC               glBindBuffer_fp = nullptr;
PFNGLBUFFERDATAPROC               glBufferData_fp = nullptr;
PFNGLBUFFERSUBDATAPROC            glBufferSubData_fp = nullptr;
PFNGLENABLEVERTEXATTRIBARRAYPROC  glEnableVertexAttribArray_fp = nullptr;
PFNGLVERTEXATTRIBPOINTERPROC      glVertexAttribPointer_fp = nullptr;
PFNGLACTIVETEXTUREPROC            glActiveTexture_fp = nullptr;
PFNGLGENERATEMIPMAPPROC           glGenerateMipmap_fp = nullptr;
PFNGLGENFRAMEBUFFERSPROC          glGenFramebuffers_fp = nullptr;
PFNGLDELETEFRAMEBUFFERSPROC       glDeleteFramebuffers_fp = nullptr;
PFNGLBINDFRAMEBUFFERPROC          glBindFramebuffer_fp = nullptr;
PFNGLFRAMEBUFFERTEXTURE2DPROC     glFramebufferTexture2D_fp = nullptr;
PFNGLCHECKFRAMEBUFFERSTATUSPROC   glCheckFramebufferStatus_fp = nullptr;
PFNGLGENRENDERBUFFERSPROC           glGenRenderbuffers_fp = nullptr;
PFNGLDELETERENDERBUFFERSPROC        glDeleteRenderbuffers_fp = nullptr;
PFNGLBINDRENDERBUFFERPROC           glBindRenderbuffer_fp = nullptr;
PFNGLRENDERBUFFERSTORAGEPROC        glRenderbufferStorage_fp = nullptr;
PFNGLFRAMEBUFFERRENDERBUFFERPROC    glFramebufferRenderbuffer_fp = nullptr;
PFNGLTEXIMAGE3DPROC                 glTexImage3D_fp = nullptr;
PFNGLTEXSUBIMAGE3DPROC              glTexSubImage3D_fp = nullptr;

namespace fate {

static void* loadProc(const char* name) {
    void* proc = SDL_GL_GetProcAddress(name);
    if (!proc) {
        LOG_ERROR("GL", "Failed to load GL function: %s", name);
    }
    return proc;
}

bool loadGLFunctions() {
    int failed = 0;

    #define LOAD_GL(func) func##_fp = (decltype(func##_fp))loadProc(#func); \
        if (!func##_fp) failed++

    LOAD_GL(glCreateShader);
    LOAD_GL(glDeleteShader);
    LOAD_GL(glShaderSource);
    LOAD_GL(glCompileShader);
    LOAD_GL(glGetShaderiv);
    LOAD_GL(glGetShaderInfoLog);
    LOAD_GL(glCreateProgram);
    LOAD_GL(glDeleteProgram);
    LOAD_GL(glAttachShader);
    LOAD_GL(glLinkProgram);
    LOAD_GL(glUseProgram);
    LOAD_GL(glGetProgramiv);
    LOAD_GL(glGetProgramInfoLog);
    LOAD_GL(glGetUniformLocation);
    LOAD_GL(glUniform1i);
    LOAD_GL(glUniform1f);
    LOAD_GL(glUniform2f);
    LOAD_GL(glUniform3f);
    LOAD_GL(glUniform4f);
    LOAD_GL(glUniformMatrix4fv);
    LOAD_GL(glGenVertexArrays);
    LOAD_GL(glDeleteVertexArrays);
    LOAD_GL(glBindVertexArray);
    LOAD_GL(glGenBuffers);
    LOAD_GL(glDeleteBuffers);
    LOAD_GL(glBindBuffer);
    LOAD_GL(glBufferData);
    LOAD_GL(glBufferSubData);
    LOAD_GL(glEnableVertexAttribArray);
    LOAD_GL(glVertexAttribPointer);
    LOAD_GL(glActiveTexture);
    LOAD_GL(glGenerateMipmap);
    LOAD_GL(glGenFramebuffers);
    LOAD_GL(glDeleteFramebuffers);
    LOAD_GL(glBindFramebuffer);
    LOAD_GL(glFramebufferTexture2D);
    LOAD_GL(glCheckFramebufferStatus);
    LOAD_GL(glGenRenderbuffers);
    LOAD_GL(glDeleteRenderbuffers);
    LOAD_GL(glBindRenderbuffer);
    LOAD_GL(glRenderbufferStorage);
    LOAD_GL(glFramebufferRenderbuffer);
    LOAD_GL(glTexImage3D);
    LOAD_GL(glTexSubImage3D);

    #undef LOAD_GL

    if (failed > 0) {
        LOG_ERROR("GL", "Failed to load %d GL functions", failed);
        return false;
    }

    LOG_INFO("GL", "All OpenGL 3.3 functions loaded successfully");
    return true;
}

} // namespace fate

#endif // FATEMMO_GLES
