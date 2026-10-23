// Minimal UTF-8/UTF-16 conversion + logging + shared globals.
#include "bpp_common.h"
#include <sys/stat.h>
#include <cstdarg>
#include <ctime>

namespace bpp {

namespace {
std::mutex g_log_mu;
FILE* g_log_file = nullptr;
std::atomic<bool> g_log_open{false};
char g_log_path[512] = {0};

void Mkdirs(const std::string& path) {
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        cur.push_back(path[i]);
        if (path[i] == '/' && cur.size() > 1) mkdir(cur.c_str(), 0770);
    }
    mkdir(path.c_str(), 0770);
}
}  // namespace

std::string ReadPackageName() {
    const char* fallback = "com.tempogames.thebazaar.bazaarbotmobile";
    FILE* f = fopen("/proc/self/cmdline", "r");
    if (!f) return fallback;
    char buf[256] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    if (n == 0) return fallback;
    std::string s(buf, strnlen(buf, n));
    // Service processes append ":service"; storage belongs to the package itself.
    size_t suffix = s.find(':');
    if (suffix != std::string::npos) s.resize(suffix);
    return s.empty() ? fallback : s;
}

const char* LogPath() { return g_log_path; }

// The Android/data/<pkg>/files directory may not exist yet when the library is
// constructed (it is created by the game later), so the first open can fail.
bool LogOpenRetry() {
    if (g_log_open.load()) return true;
    LogInit();
    return g_log_open.load();
}

std::string FilesDir() {
    std::string pkg = ReadPackageName();
    if (pkg.empty()) return {};
    return "/sdcard/Android/data/" + pkg + "/files";
}

// Android/media/<pkg> is writable by the app and visible to file managers and MTP,
// unlike Android/data which Android 11+ hides from everything except adb.
std::string MediaDir() {
    std::string pkg = ReadPackageName();
    if (pkg.empty()) return {};
    std::string dir = "/sdcard/Android/media/" + pkg;
    Mkdirs(dir);
    return dir;
}

bool FileExists(const std::string& path) { return !path.empty() && access(path.c_str(), F_OK) == 0; }

bool VulkanLoaded() {
    // RTLD_NOLOAD only reports whether the library is already mapped.
    void* h = dlopen("libvulkan.so", RTLD_NOLOAD);
    if (h) dlclose(h);
    return h != nullptr;
}

// A marker file lets the user disable the mod without reinstalling the APK:
//   adb shell touch /sdcard/Android/data/<package>/files/bpp_safe_mode
bool SafeModeRequested() {
    std::string pkg = ReadPackageName();
    if (pkg.empty()) return false;
    char path[512];
    snprintf(path, sizeof(path), "/sdcard/Android/media/%s/bpp_safe_mode", pkg.c_str());
    if (access(path, F_OK) == 0) return true;
    snprintf(path, sizeof(path), "/sdcard/Android/data/%s/files/bpp_safe_mode", pkg.c_str());
    return access(path, F_OK) == 0;
}

void LogInit() {
    std::lock_guard<std::mutex> lk(g_log_mu);
    if (g_log_file) return;
    std::string pkg = ReadPackageName();
    if (pkg.empty()) return;
    std::string path;
    // Prefer the MTP-visible media dir so the log can be copied off the phone
    // without adb; fall back to the app external dir.
    std::string media = "/sdcard/Android/media/" + pkg;
    Mkdirs(media);
    path = media + "/bpp.log";
    g_log_file = fopen(path.c_str(), "a");
    if (!g_log_file) {
        std::string dir = "/sdcard/Android/data/" + pkg + "/files";
        Mkdirs(dir);
        path = dir + "/bpp.log";
        g_log_file = fopen(path.c_str(), "a");
        if (!g_log_file) {
            Mkdirs(dir);
            g_log_file = fopen(path.c_str(), "a");
        }
    }
    snprintf(g_log_path, sizeof(g_log_path), "%s", path.c_str());
    if (g_log_file) {
        g_log_open.store(true);
        fprintf(g_log_file, "\n===== bpp start pid=%d pkg=%s =====\n", getpid(), pkg.c_str());
        fflush(g_log_file);
    }
}

void LogWrite(int prio, const char* fmt, ...) {
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    __android_log_print(prio, BPP_TAG, "%s", msg);
    std::lock_guard<std::mutex> lk(g_log_mu);
    if (!g_log_file) return;
    timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv{};
    time_t sec = ts.tv_sec;
    localtime_r(&sec, &tmv);
    fprintf(g_log_file, "%02d:%02d:%02d.%03d %c %s\n", tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
            (int)(ts.tv_nsec / 1000000),
            prio == ANDROID_LOG_ERROR ? 'E' : prio == ANDROID_LOG_WARN ? 'W' : 'I', msg);
    fflush(g_log_file);
}

std::string Utf16ToUtf8(const uint16_t* s, int len) {
    std::string out;
    if (!s || len <= 0) return out;
    out.reserve((size_t)len * 3);
    for (int i = 0; i < len; ++i) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < len && s[i + 1] >= 0xDC00 && s[i + 1] <= 0xDFFF) {
            uint32_t lo = s[i + 1];
            c = 0x10000u + ((c - 0xD800u) << 10) + (lo - 0xDC00u);
            ++i;
        } else if (c >= 0xD800 && c <= 0xDFFF) {
            c = 0xFFFD;  // lone surrogate
        }
        if (c < 0x80) {
            out.push_back((char)c);
        } else if (c < 0x800) {
            out.push_back((char)(0xC0 | (c >> 6)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else if (c < 0x10000) {
            out.push_back((char)(0xE0 | (c >> 12)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        } else {
            out.push_back((char)(0xF0 | (c >> 18)));
            out.push_back((char)(0x80 | ((c >> 12) & 0x3F)));
            out.push_back((char)(0x80 | ((c >> 6) & 0x3F)));
            out.push_back((char)(0x80 | (c & 0x3F)));
        }
    }
    return out;
}

std::u16string Utf8ToUtf16(const std::string& s) {
    std::u16string out;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t c = (unsigned char)s[i];
        int extra = 0;
        if (c < 0x80) {
            extra = 0;
        } else if ((c & 0xE0) == 0xC0) {
            c &= 0x1F; extra = 1;
        } else if ((c & 0xF0) == 0xE0) {
            c &= 0x0F; extra = 2;
        } else if ((c & 0xF8) == 0xF0) {
            c &= 0x07; extra = 3;
        } else {
            c = 0xFFFD; extra = 0;
        }
        ++i;
        for (int k = 0; k < extra && i < s.size(); ++k, ++i)
            c = (c << 6) | ((unsigned char)s[i] & 0x3F);
        if (c >= 0x10000) {
            c -= 0x10000;
            out.push_back((char16_t)(0xD800 + (c >> 10)));
            out.push_back((char16_t)(0xDC00 + (c & 0x3FF)));
        } else {
            out.push_back((char16_t)c);
        }
    }
    return out;
}

}  // namespace bpp
