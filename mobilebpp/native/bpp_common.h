// BPP Mobile - common helpers
#pragma once
#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <pthread.h>
#include <unistd.h>
#include <dlfcn.h>

#define BPP_TAG "BPP"
namespace bpp {
void LogInit();
void LogWrite(int prio, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
const char* LogPath();
std::string ReadPackageName();
bool SafeModeRequested();
std::string FilesDir();          // /sdcard/Android/data/<pkg>/files
std::string MediaDir();          // /sdcard/Android/media/<pkg> (MTP/file-manager visible)
bool FileExists(const std::string& path);
bool VulkanLoaded();             // true when libvulkan.so is already mapped
bool LogOpenRetry();             // retry opening the log file (dir may appear late)
}  // namespace bpp

#define LOGI(...) bpp::LogWrite(ANDROID_LOG_INFO, __VA_ARGS__)
#define LOGW(...) bpp::LogWrite(ANDROID_LOG_WARN, __VA_ARGS__)
#define LOGE(...) bpp::LogWrite(ANDROID_LOG_ERROR, __VA_ARGS__)
#define LOGD(...) bpp::LogWrite(ANDROID_LOG_DEBUG, __VA_ARGS__)

namespace bpp {

inline uint64_t NowMs() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

inline void SleepMs(int ms) { usleep((useconds_t)ms * 1000); }

// Minimal UTF-8 <-> UTF-16 conversion (no ICU dependency).
std::string Utf16ToUtf8(const uint16_t* s, int len);
std::u16string Utf8ToUtf16(const std::string& s);

}  // namespace bpp
