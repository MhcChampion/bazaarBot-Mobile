#include "ui.h"
#include "il2cpp_api.h"
#include "bpp_features.h"

#include <GLES3/gl3.h>
#include <shadowhook.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>

#include <atomic>
#include <mutex>

namespace ui {
namespace {

std::atomic<bool> g_open{false};
std::atomic<bool> g_hooked{false};
std::atomic<int> g_swap_count{0};
std::atomic<bool> g_imgui_ready{false};
std::atomic<int> g_fingers{0};
std::atomic<bool> g_touch_down{false};
std::atomic<float> g_touch_x{0.f}, g_touch_y{0.f};
std::atomic<bool> g_toggle_req{false};
std::atomic<float> g_user_scale{1.0f};
float g_base_scale = 1.0f;
std::mutex g_render_mu;

EGLBoolean (*orig_swap)(EGLDisplay, EGLSurface) = nullptr;
EGLBoolean (*orig_swap_damage)(EGLDisplay, EGLSurface, const EGLint*, EGLint) = nullptr;
void* (*orig_get_proc_address)(const char*) = nullptr;

bool LoadFont(ImGuiIO& io) {
    const char* candidates[] = {
        "/system/fonts/NotoSansCJK-Regular.ttc",
        "/system/fonts/NotoSansSC-Regular.otf",
        "/system/fonts/DroidSansFallback.ttf",
        "/system/fonts/DroidSansFallbackFull.ttf",
        "/system/fonts/NotoSansCJKSC-Regular.otf",
    };
    ImFontConfig cfg;
    cfg.FontNo = 0;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    for (const char* p : candidates) {
        if (access(p, R_OK) != 0) continue;
        ImFont* f = io.Fonts->AddFontFromFileTTF(p, 34.0f, &cfg, nullptr);
        if (f) {
            LOGI("font loaded: %s", p);
            return true;
        }
    }
    LOGW("no CJK font found, falling back to default (Chinese will not render)");
    io.Fonts->AddFontDefault();
    return false;
}

// Diagnostic: a magenta strip drawn with plain GL (no ImGui, no shaders).
// Seeing it proves the EGL hook + GL context work; not seeing it means the
// swap path is wrong, independent of anything ImGui does.
void DrawCanary(int w, int h) {
    static uint64_t first_ms = 0;
    uint64_t now = bpp::NowMs();
    if (first_ms == 0) first_ms = now;
    if (now - first_ms > 45000) return;

    GLint last_scissor[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_SCISSOR_BOX, last_scissor);
    GLboolean scissor_was = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean mask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    glGetBooleanv(GL_COLOR_WRITEMASK, mask);
    GLfloat last_clear[4] = {0, 0, 0, 0};
    glGetFloatv(GL_COLOR_CLEAR_VALUE, last_clear);

    int bw = w / 5;
    int bh = h / 40;
    if (bw < 120) bw = 120;
    if (bh < 24) bh = 24;

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, bw, bh);
    glClearColor(1.0f, 0.0f, 1.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glClearColor(last_clear[0], last_clear[1], last_clear[2], last_clear[3]);
    glScissor(last_scissor[0], last_scissor[1], last_scissor[2], last_scissor[3]);
    if (!scissor_was) glDisable(GL_SCISSOR_TEST);
    glColorMask(mask[0], mask[1], mask[2], mask[3]);

    static bool logged = false;
    if (!logged) {
        logged = true;
        LOGI("gl canary drawn %dx%d (EGL hook + GL context OK)", bw, bh);
    }
}

void RenderFrame(EGLDisplay dpy, EGLSurface surf) {
    std::lock_guard<std::mutex> lk(g_render_mu);
    // Both eglSwapBuffers and the eglGetProcAddress path can fire for the same
    // frame; skip a second pass within 3 ms.
    static uint64_t last_render = 0;
    uint64_t now = bpp::NowMs();
    if (now - last_render < 3) return;
    last_render = now;
    EGLint w = 0, h = 0;
    if (!eglQuerySurface(dpy, surf, EGL_WIDTH, &w)) return;
    if (!eglQuerySurface(dpy, surf, EGL_HEIGHT, &h)) return;
    if (w <= 0 || h <= 0) return;

    // Draw the probe first: it must work even if ImGui init fails.
    DrawCanary(w, h);

    if (!g_imgui_ready.load()) {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.LogFilename = nullptr;
        LoadFont(io);
        io.DisplaySize = ImVec2((float)w, (float)h);
        float scale = (w < h ? (float)w : (float)h) / 1080.0f;
        if (scale < 0.6f) scale = 0.6f;
        if (scale > 2.0f) scale = 2.0f;
        g_base_scale = scale;
        ImGui::GetStyle().FontScaleMain = scale * g_user_scale.load();
        ImGui::GetStyle().ScaleAllSizes(scale);
        ImGui_ImplOpenGL3_Init("#version 300 es");
        g_imgui_ready.store(true);
        LOGI("imgui ready %dx%d scale=%.2f", w, h, scale);
    }

    ImGui::GetStyle().FontScaleMain = g_base_scale * g_user_scale.load();
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2((float)w, (float)h);

    // Feed the latest touch snapshot.
    bool down = g_touch_down.load();
    io.AddMousePosEvent(g_touch_x.load(), g_touch_y.load());
    io.AddMouseButtonEvent(0, down);

    if (g_toggle_req.exchange(false)) g_open.store(!g_open.load());
    if (!g_open.load() && !features::WantsOverlay()) {
        // still render a tiny floating button
    }

    // GL state saved by the backend; ensure we draw into the window framebuffer.
    GLint last_fbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &last_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, w, h);

    ImGui_ImplOpenGL3_NewFrame();
    ImGui::NewFrame();

    features::DrawOverlay(g_open.load());

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glBindFramebuffer(GL_FRAMEBUFFER, (GLuint)last_fbo);
}

EGLBoolean HookSwap(EGLDisplay dpy, EGLSurface surf) {
    g_swap_count.fetch_add(1);
    RenderFrame(dpy, surf);
    return orig_swap ? orig_swap(dpy, surf) : EGL_FALSE;
}

// Unity may resolve the KHR/EXT "with damage" variants through eglGetProcAddress
// and call those instead of eglSwapBuffers. Hook the resolver too, otherwise the
// overlay silently never draws on devices that take that path.
EGLBoolean HookSwapDamage(EGLDisplay dpy, EGLSurface surf, const EGLint* rects,
                          EGLint n_rects) {
    g_swap_count.fetch_add(1);
    RenderFrame(dpy, surf);
    return orig_swap_damage ? orig_swap_damage(dpy, surf, rects, n_rects) : EGL_FALSE;
}

void* HookGetProcAddress(const char* name) {
    void* p = orig_get_proc_address ? orig_get_proc_address(name) : nullptr;
    if (!p || !name) return p;
    if (strcmp(name, "eglSwapBuffers") == 0) {
        if (!orig_swap) orig_swap = (EGLBoolean(*)(EGLDisplay, EGLSurface))p;
        LOGI("eglGetProcAddress(eglSwapBuffers) intercepted");
        return (void*)HookSwap;
    }
    if (strcmp(name, "eglSwapBuffersWithDamageKHR") == 0 ||
        strcmp(name, "eglSwapBuffersWithDamageEXT") == 0) {
        if (!orig_swap_damage)
            orig_swap_damage =
                (EGLBoolean(*)(EGLDisplay, EGLSurface, const EGLint*, EGLint))p;
        LOGI("eglGetProcAddress(%s) intercepted", name);
        return (void*)HookSwapDamage;
    }
    return p;
}

}  // namespace

void InstallEglHook() {
    if (g_hooked.exchange(true)) return;
    // Resolve through dlsym + hook_func_addr: hook_sym_name depends on ShadowHook
    // dlopen monitoring, which is unavailable on some runtimes.
    void* egl = dlopen("libEGL.so", RTLD_NOW);
    void* swap_fn = egl ? dlsym(egl, "eglSwapBuffers") : nullptr;
    void* stub = swap_fn ? shadowhook_hook_func_addr(swap_fn, (void*)HookSwap, (void**)&orig_swap)
                         : nullptr;
    if (!stub || !orig_swap) {
        LOGE("eglSwapBuffers hook failed (fn=%p)", swap_fn);
        g_hooked.store(false);
    } else {
        LOGI("eglSwapBuffers hooked @%p", swap_fn);
    }
    void* gp_fn = egl ? dlsym(egl, "eglGetProcAddress") : nullptr;
    void* stub2 = gp_fn ? shadowhook_hook_func_addr(gp_fn, (void*)HookGetProcAddress,
                                                    (void**)&orig_get_proc_address)
                        : nullptr;
    if (stub2 && orig_get_proc_address) {
        LOGI("eglGetProcAddress hooked");
    } else {
        LOGW("eglGetProcAddress hook failed (damage variants stay unhooked)");
    }
}

bool Hooked() { return g_hooked.load() && orig_swap != nullptr; }

int SwapCount() { return g_swap_count.load(); }

void Tick() {
    // Touch sampling is done on the Unity main thread in features::Tick.
    (void)0;
}

TouchState Snapshot() {
    TouchState t;
    t.down = g_touch_down.load();
    t.x = g_touch_x.load();
    t.y = g_touch_y.load();
    t.fingers = g_fingers.load();
    return t;
}

bool IsOpen() { return g_open.load(); }
void SetOpen(bool open) { g_open.store(open); }

// ---- called by features (main thread) ----
void UpdateTouch(float x, float y, bool down, int fingers) {
    g_touch_x.store(x);
    g_touch_y.store(y);
    g_touch_down.store(down);
    g_fingers.store(fingers);
}

void RequestToggle() { g_toggle_req.store(true); }

void SetUserScale(float scale) {
    if (scale < 0.5f) scale = 0.5f;
    if (scale > 2.0f) scale = 2.0f;
    g_user_scale.store(scale);
}

float UserScale() { return g_user_scale.load(); }

}  // namespace ui
