#include "crash_guard.h"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <ucontext.h>
#include <unistd.h>

namespace bpp {
namespace {

int g_crash_fd = -1;
struct OldAction {
    int sig;
    struct sigaction sa;
};
OldAction g_old[8];
int g_old_count = 0;

const struct sigaction* FindOld(int sig) {
    for (int i = 0; i < g_old_count; ++i)
        if (g_old[i].sig == sig) return &g_old[i].sa;
    return nullptr;
}
const int kMaxHookName = 64;
char g_hook_name[kMaxHookName] = {0};
volatile sig_atomic_t g_hook_depth = 0;

void WriteAll(const char* s, size_t n) {
    if (g_crash_fd < 0) return;
    ssize_t off = 0;
    while (off < (ssize_t)n) {
        ssize_t w = write(g_crash_fd, s + off, (size_t)(n - off));
        if (w <= 0) break;
        off += w;
    }
}

void Handler(int sig, siginfo_t* info, void* ucv) {
    char buf[320];
    unsigned long pc = 0;
    if (ucv) {
#if defined(__aarch64__)
        ucontext_t* uc = (ucontext_t*)ucv;
        pc = (unsigned long)uc->uc_mcontext.pc;
#elif defined(__arm__)
        ucontext_t* uc = (ucontext_t*)ucv;
        pc = (unsigned long)uc->uc_mcontext.arm_pc;
#endif
    }
    int depth = (int)g_hook_depth;
    const char* where = depth > 0 ? g_hook_name : "(not in bpp hook)";
    int n = snprintf(buf, sizeof(buf),
                     "\n*** FATAL signal=%d addr=%p pc=0x%lx hook_depth=%d in=%s ***\n", sig,
                     info ? info->si_addr : nullptr, pc, depth, where);
    if (n > 0) WriteAll(buf, (size_t)n);
    // Chain to whatever handler was installed before us (Unity ships one), then
    // re-raise. We only add attribution, we never swallow the crash.
    const struct sigaction* old = FindOld(sig);
    if (old) {
        ::sigaction(sig, old, nullptr);
    } else {
        ::signal(sig, SIG_DFL);
    }
    ::raise(sig);
}

}  // namespace

void CrashGuardAttachLogFd() {
    if (g_crash_fd >= 0) return;
    std::string path = LogPath();
    if (path.empty()) return;
    g_crash_fd = open(path.c_str(), O_WRONLY | O_APPEND | O_CREAT, 0600);
}

void InstallCrashHandler() {
    CrashGuardAttachLogFd();
    if (g_crash_fd < 0) {
        LOGW("crash handler has no log fd; attribution disabled");
        return;
    }
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_sigaction = Handler;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    sigemptyset(&sa.sa_mask);
    for (int sig : {SIGSEGV, SIGBUS, SIGABRT, SIGILL, SIGFPE}) {
        if (g_old_count >= 8) break;
        struct sigaction old;
        std::memset(&old, 0, sizeof(old));
        if (sigaction(sig, &sa, &old) != 0) {
            LOGW("sigaction(%d) failed", sig);
            continue;
        }
        g_old[g_old_count].sig = sig;
        g_old[g_old_count].sa = old;
        ++g_old_count;
    }
    LOGI("crash handler installed (fd=%d)", g_crash_fd);
}

void EnterHook(const char* name) {
    if (name && g_hook_depth == 0) {
        std::strncpy(g_hook_name, name, kMaxHookName - 1);
        g_hook_name[kMaxHookName - 1] = 0;
    }
    ++g_hook_depth;
}

void LeaveHook() { --g_hook_depth; }

}  // namespace bpp
