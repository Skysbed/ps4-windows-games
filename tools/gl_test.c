/*
 * OpenGL smoke test for PCPS4: opens a window, creates a legacy context and
 * then an OpenGL 3.2 core context (Minecraft's minimum), logs what it got to
 * Z:\data\stkuser\gltest.log, and draws a triangle for ~5 seconds.
 */
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>
#include <math.h>

#define WGL_CONTEXT_MAJOR_VERSION_ARB     0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB     0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB      0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB  0x0001
#define GL_SHADING_LANGUAGE_VERSION       0x8B8C
typedef HGLRC (WINAPI *PFNCREATECTXATTRIBS)(HDC, HGLRC, const int *);

static FILE *log_file;
static void say(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (log_file) { vfprintf(log_file, fmt, ap); fputc('\n', log_file); fflush(log_file); }
    va_end(ap);
}

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    WNDCLASSA wc = {0};
    PIXELFORMATDESCRIPTOR pfd = {0};
    HWND win;
    HDC dc;
    HGLRC legacy, core = NULL;
    PFNCREATECTXATTRIBS create_attribs;
    int pf;

    log_file = fopen("Z:\\data\\stkuser\\gltest.log", "w");
    say("gltest start");

    wc.style = CS_OWNDC;
    wc.lpfnWndProc = wndproc;
    wc.hInstance = inst;
    wc.lpszClassName = "pcps4gl";
    RegisterClassA(&wc);
    win = CreateWindowExA(0, "pcps4gl", "PCPS4 OpenGL test", WS_POPUP | WS_VISIBLE, 0, 0, 1920, 1080, NULL, NULL, inst, NULL);
    dc = GetDC(win);
    say("window %p dc %p", (void *)win, (void *)dc);

    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pf = ChoosePixelFormat(dc, &pfd);
    say("ChoosePixelFormat -> %d, SetPixelFormat -> %d", pf, SetPixelFormat(dc, pf, &pfd));

    legacy = wglCreateContext(dc);
    say("wglCreateContext -> %p, MakeCurrent -> %d", (void *)legacy, legacy ? wglMakeCurrent(dc, legacy) : 0);
    if (!legacy) { say("FAIL: no OpenGL context"); goto done; }
    say("GL_VENDOR   %s", glGetString(GL_VENDOR));
    say("GL_RENDERER %s", glGetString(GL_RENDERER));
    say("GL_VERSION  %s", glGetString(GL_VERSION));

    create_attribs = (PFNCREATECTXATTRIBS)wglGetProcAddress("wglCreateContextAttribsARB");
    if (create_attribs)
    {
        static const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 2,
                                       WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
        core = create_attribs(dc, NULL, attribs);
        say("3.2 core context -> %p", (void *)core);
        if (core && wglMakeCurrent(dc, core))
        {
            say("core GL_VERSION %s", glGetString(GL_VERSION));
            say("core GLSL       %s", glGetString(GL_SHADING_LANGUAGE_VERSION));
            say("PASS: OpenGL 3.2 core available");
            wglMakeCurrent(dc, legacy);
        }
        else say("FAIL: no 3.2 core context");
    }
    else say("FAIL: wglCreateContextAttribsARB missing");

    for (int frame = 0; frame < 300; frame++)
    {
        MSG msg;
        float a = frame * 0.03f;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg);
        glViewport(0, 0, 1920, 1080);
        glClearColor(0.05f, 0.12f + 0.1f * sinf(a), 0.25f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glLoadIdentity();
        glRotatef(frame * 1.2f, 0, 0, 1);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0.2f, 0.2f); glVertex2f(0, 0.7f);
        glColor3f(0.2f, 1, 0.3f); glVertex2f(-0.6f, -0.5f);
        glColor3f(0.2f, 0.4f, 1); glVertex2f(0.6f, -0.5f);
        glEnd();
        if (frame < 3 || frame % 60 == 0) say("frame %d drawn, glGetError %u, swapping", frame, glGetError());
        {
            BOOL ok = SwapBuffers(dc);
            if (frame < 3 || frame % 60 == 0) say("frame %d SwapBuffers -> %d (GetLastError %lu)", frame, ok, GetLastError());
        }
    }
    say("done: 300 frames");

done:
    if (log_file) fclose(log_file);
    ExitProcess(0);
    return 0;
}
