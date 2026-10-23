#include "bpp_common.h"
#include "il2cpp_api.h"
#include "sqlite_shim.h"
#include "ui.h"
#include "bpp_features.h"
#include "crash_guard.h"

#include <shadowhook.h>
#include <atomic>

static void* g_orig_tick = nullptr;
static std::atomic<bool> g_inited{false};
static std::atomic<bool> g_hook_done{false};

static void TickHook(void* self, void* method_info) {
    bpp::HookScope scope("EventSystem.Update");
    if (!g_inited.exchange(true)) {
        LOGI("first tick on unity main thread");
        features::InstallHooks();
    }
    features::Tick();
    if (g_orig_tick) {
        ((void (*)(void*, void*))g_orig_tick)(self, method_info);
    }
}

static void* MethodPointer(void* method_info) {
    if (!method_info) return nullptr;
    // Unity 2021.2+ MethodInfo: { methodPointer; virtualMethodPointer; ... }
    // Verify against the owning module instead of trusting the layout blindly.
    Dl_info info{};
    void* cand = *(void**)method_info;
    if (cand && dladdr(cand, &info) && info.dli_fname && strstr(info.dli_fname, "libil2cpp"))
        return cand;
    void* cand2 = *(void**)((char*)method_info + 8);
    if (cand2 && dladdr(cand2, &info) && info.dli_fname && strstr(info.dli_fname, "libil2cpp")) {
        LOGW("methodPointer found at MethodInfo+8 (layout variant)");
        return cand2;
    }
    LOGE("cannot locate methodPointer in MethodInfo %p (cand=%p/%p)", method_info, cand, cand2);
    return nullptr;
}

static bool HookOne(const char* image, const char* ns, const char* klass, const char* method,
                    int argc) {
    void* k = il2::Klass(image, ns, klass);
    if (!k) return false;
    void* m = il2::Method(k, method, argc);
    if (!m) {
        LOGW("method missing %s::%s/%d", klass, method, argc);
        return false;
    }
    void* fn = MethodPointer(m);
    if (!fn) {
        LOGE("null method pointer for %s::%s", klass, method);
        return false;
    }
    Dl_info info{};
    dladdr(fn, &info);
    void* stub = shadowhook_hook_func_addr(fn, (void*)TickHook, &g_orig_tick);
    if (!stub || !g_orig_tick) {
        int err = shadowhook_get_errno();
        LOGE("hook failed %s::%s stub=%p errno=%d msg=%s (%s+%s)", klass, method, stub, err,
             shadowhook_to_errmsg(err), info.dli_fname ? info.dli_fname : "?",
             info.dli_sname ? info.dli_sname : "?");
        return false;
    }
    LOGI("tick hook installed: %s::%s @%p (%s)", klass, method, fn,
         info.dli_fname ? info.dli_fname : "?");
    return true;
}

static void InstallTickHook() {
    if (g_hook_done.exchange(true)) return;
    int shrc = shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    LOGI("shadowhook_init rc=%d errno=%d msg=%s", shrc, shadowhook_get_errno(),
         shadowhook_to_errmsg(shadowhook_get_errno()));
    // Per-frame main-thread entry points, most reliable first.
    if (HookOne("UnityEngine.UI.dll", "UnityEngine.EventSystems", "EventSystem", "Update", 0))
        return;
    // Note: StandaloneInputModule::Process is deliberately NOT hooked. Input comes
    // from polling InputSystem; a second hook inside EventSystem.Update crashed the
    // game on tap (hook_depth=2) after login.
    if (false && HookOne("UnityEngine.UI.dll", "UnityEngine.EventSystems", "StandaloneInputModule",
                "Process", 0))
        return;
    // Camera.Render is intentionally not hooked: the game calls it from inside
    // EventSystem.Update, which made it a nested (depth=2) hook and crashed on
    // scene changes. Rendering happens in the eglSwapBuffers hook instead.
    if (HookOne("UnityEngine.CoreModule.dll", "UnityEngine", "Application", "Update", 0)) return;
    LOGE("no per-frame hook candidate found");
    g_hook_done.store(false);
}

static void* BootThread(void*) {
    bpp::SleepMs(300);  // let the linker finish mapping libil2cpp.so
    LOGI("bpp boot thread started (pid=%d)", getpid());
    if (!il2::Load(60000)) {
        LOGE("il2cpp load failed");
        return nullptr;
    }
    // Never call into the runtime before il2cpp_init returns: on a fresh process
    // il2cpp_domain_get() faults when IL2CPP state is still null (observed on device).
    const uint64_t boot_t0 = bpp::NowMs();
    const bool init_hooked = il2::InstallInitHook();
    LOGI("init hook: %s", init_hooked ? "installed" : "unavailable (using grace period)");
    while (bpp::NowMs() - boot_t0 < 30000) {
        const uint64_t elapsed = bpp::NowMs() - boot_t0;
        if (il2::RuntimeInited() && elapsed > 1500) break;
        if (elapsed > 45000) break;  // hook may have missed an early il2cpp_init
        if (elapsed % 5000 < 100) LOGI("gate wait %llu ms (inited=%d)", (unsigned long long)elapsed, il2::RuntimeInited() ? 1 : 0);
        bpp::SleepMs(100);
    }
    LOGI("runtime gate passed after %llu ms (inited=%d)",
         (unsigned long long)(bpp::NowMs() - boot_t0), il2::RuntimeInited() ? 1 : 0);
    const uint64_t deadline = bpp::NowMs() + 120000;
    while (!il2::DomainReady() && bpp::NowMs() < deadline) bpp::SleepMs(200);
    if (!il2::DomainReady()) {
        LOGE("il2cpp domain never became ready");
        return nullptr;
    }
    LOGI("il2cpp domain ready");
    il2::AttachCurrentThread();

    // Wait until the game assemblies are actually populated.
    for (int i = 0; i < 600; ++i) {
        if (il2::Image("TheBazaarRuntime.dll") && il2::Image("BazaarGameShared.dll")) break;
        bpp::SleepMs(200);
    }
    il2::DumpImages();

    features::Boot();
    InstallTickHook();
    ui::InstallEglHook();
    LOGI("bpp boot done");
    return nullptr;
}

__attribute__((constructor)) static void BppConstructor() {
    bpp::LogInit();
    LOGI("libbpp constructor, pid=%d", getpid());
    if (bpp::SafeModeRequested()) {
        LOGW("safe mode marker found: mod disabled (remove the file to re-enable)");
        return;
    }
    bpp::InstallCrashHandler();
    // Opt-in: register a memory VFS before the game opens its database.
    {
        std::string marker = bpp::MediaDir() + "/bpp_sqlite_shim";
        if (!marker.empty() && bpp::FileExists(marker)) InstallSqliteShim();
    }
    pthread_t t;
    if (pthread_create(&t, nullptr, BootThread, nullptr) == 0) {
        pthread_detach(t);
    } else {
        LOGE("failed to start boot thread");
    }
}
