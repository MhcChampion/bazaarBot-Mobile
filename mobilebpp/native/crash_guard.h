// Crash telemetry: if the process dies while inside one of our hooks, leave a
// short, async-signal-safe note in bpp.log so the failure is attributable.
#pragma once
#include "bpp_common.h"

namespace bpp {

// Installs SIGSEGV/SIGBUS/SIGABRT/SIGILL handlers. Call once, after LogInit().
void InstallCrashHandler();
// Re-open the log fd if the first attempt happened before the directory existed.
void CrashGuardAttachLogFd();

// Hooks bracket managed calls with these so a fault inside a hook is labelled.
void EnterHook(const char* name);
void LeaveHook();

// RAII bracket used at the top of every hook.
struct HookScope {
    explicit HookScope(const char* name) { EnterHook(name); }
    ~HookScope() { LeaveHook(); }
    HookScope(const HookScope&) = delete;
    HookScope& operator=(const HookScope&) = delete;
};

}  // namespace bpp
