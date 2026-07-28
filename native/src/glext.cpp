#include "glext.h"

#define X(ret, name, args) PFN_##name name = nullptr;
GL_FUNCS
#undef X

PFNWGLCREATECONTEXTATTRIBSARB wglCreateContextAttribsARB = nullptr;
PFNWGLCHOOSEPIXELFORMATARB wglChoosePixelFormatARB = nullptr;
PFNWGLSWAPINTERVALEXT wglSwapIntervalEXT = nullptr;

static void* getGLProc(const char* name) {
    void* p = (void*)wglGetProcAddress(name);
    if (p == nullptr || p == (void*)1 || p == (void*)2 || p == (void*)3 || p == (void*)-1) {
        static HMODULE mod = LoadLibraryA("opengl32.dll");
        p = (void*)GetProcAddress(mod, name);
    }
    return p;
}

void loadWGLExtensions() {
    wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARB)getGLProc("wglCreateContextAttribsARB");
    wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARB)getGLProc("wglChoosePixelFormatARB");
    wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXT)getGLProc("wglSwapIntervalEXT");
}

bool loadGLFunctions() {
    bool ok = true;
#define X(ret, name, args) name = (PFN_##name)getGLProc(#name); if (!name) ok = false;
    GL_FUNCS
#undef X
    return ok;
}
