// Memory-backed SQLite VFS shim.
//
// Why: on some ARM-translation runtimes (MuMu/houdini) the game's bundled SQLite
// can open ":memory:" but fails with SQLITE_CANTOPEN for *every* file path, even
// though plain open()/fstat()/fcntl() work in the same process. Registering our
// own VFS as the default makes the game's database load into RAM using our own
// (working) file I/O, which is enough to get past the loading screen for testing.
//
// Enabled only when the marker file "bpp_sqlite_shim" exists next to bpp.log.
// Nothing is written back to disk; the database lives for the process lifetime.

#include "sqlite_shim.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <cstdlib>

#include "bpp_common.h"

namespace {

typedef long long i64;

struct sqlite3_file {
    const struct sqlite3_io_methods* pMethods;
};
struct sqlite3_vfs;

struct sqlite3_io_methods {
    int iVersion;
    int (*xClose)(sqlite3_file*);
    int (*xRead)(sqlite3_file*, void*, int iAmt, i64 iOfst);
    int (*xWrite)(sqlite3_file*, const void*, int iAmt, i64 iOfst);
    int (*xTruncate)(sqlite3_file*, i64 size);
    int (*xSync)(sqlite3_file*, int flags);
    int (*xFileSize)(sqlite3_file*, i64* pSize);
    int (*xLock)(sqlite3_file*, int);
    int (*xUnlock)(sqlite3_file*, int);
    int (*xCheckReservedLock)(sqlite3_file*, int* pResOut);
    int (*xFileControl)(sqlite3_file*, int op, void* pArg);
    int (*xSectorSize)(sqlite3_file*);
    int (*xDeviceCharacteristics)(sqlite3_file*);
    int (*xShmMap)(sqlite3_file*, int iPg, int pgsz, int, void volatile**);
    int (*xShmLock)(sqlite3_file*, int offset, int n, int flags);
    void (*xShmBarrier)(sqlite3_file*);
    int (*xShmUnmap)(sqlite3_file*, int deleteFlag);
    int (*xFetch)(sqlite3_file*, i64 iOfst, int iAmt, void** pp);
    int (*xUnfetch)(sqlite3_file*, i64 iOfst, void* p);
};

struct sqlite3_vfs {
    int iVersion;
    int szOsFile;
    int mxPathname;
    sqlite3_vfs* pNext;
    const char* zName;
    void* pAppData;
    int (*xOpen)(sqlite3_vfs*, const char* zName, sqlite3_file*, int flags, int* pOutFlags);
    int (*xDelete)(sqlite3_vfs*, const char* zName, int syncDir);
    int (*xAccess)(sqlite3_vfs*, const char* zName, int flags, int* pResOut);
    int (*xFullPathname)(sqlite3_vfs*, const char* zName, int nOut, char* zOut);
    void* (*xDlOpen)(sqlite3_vfs*, const char* zFilename);
    void (*xDlError)(sqlite3_vfs*, int nByte, char* zErrMsg);
    void (*(*xDlSym)(sqlite3_vfs*, void*, const char* zSymbol))(void);
    void (*xDlClose)(sqlite3_vfs*, void*);
    int (*xRandomness)(sqlite3_vfs*, int nByte, char* zOut);
    int (*xSleep)(sqlite3_vfs*, int microseconds);
    int (*xCurrentTime)(sqlite3_vfs*, double*);
    int (*xGetLastError)(sqlite3_vfs*, int, char*);
};

constexpr int kOk = 0;
constexpr int kIoErr = 10;
constexpr int kNotFound = 12;
constexpr int kCanOpen = 14;
constexpr int kIoErrShortRead = kIoErr | (2 << 8);

constexpr int kOpenReadonly = 0x00000001;
constexpr int kOpenReadWrite = 0x00000002;
constexpr int kOpenCreate = 0x00000004;
constexpr int kOpenMainDb = 0x00000100;
constexpr int kOpenDeleteOnClose = 0x00000008;

constexpr int kAccessExists = 0;
constexpr int kAccessReadWrite = 1;

struct MemFile {
    sqlite3_file base;
    int fd;
    i64 size;
};

int MemClose(sqlite3_file* f) {
    MemFile* m = (MemFile*)f;
    if (m->fd >= 0) close(m->fd);
    m->fd = -1;
    m->size = 0;
    return kOk;
}

int MemRead(sqlite3_file* f, void* buf, int amt, i64 ofst) {
    MemFile* m = (MemFile*)f;
    if (m->fd < 0 || ofst < 0) return kIoErr;
    if (ofst >= m->size) {
        memset(buf, 0, (size_t)amt);
        return kIoErrShortRead;
    }
    ssize_t n = pread(m->fd, buf, (size_t)amt, (off_t)ofst);
    if (n < 0) return kIoErr;
    if (n < amt) {
        memset((char*)buf + n, 0, (size_t)(amt - n));
        return kIoErrShortRead;
    }
    return kOk;
}

int MemWrite(sqlite3_file* f, const void* buf, int amt, i64 ofst) {
    MemFile* m = (MemFile*)f;
    if (m->fd < 0 || ofst < 0) return kIoErr;
    ssize_t n = pwrite(m->fd, buf, (size_t)amt, (off_t)ofst);
    if (n != amt) return kIoErr;
    if (ofst + amt > m->size) m->size = ofst + amt;
    return kOk;
}

int MemTruncate(sqlite3_file* f, i64 size) {
    MemFile* m = (MemFile*)f;
    if (m->fd < 0 || size < 0) return kIoErr;
    if (ftruncate(m->fd, (off_t)size) != 0) return kIoErr;
    m->size = size;
    return kOk;
}

int MemSync(sqlite3_file*, int) { return kOk; }
int MemFileSize(sqlite3_file* f, i64* pSize) {
    *pSize = ((MemFile*)f)->size;
    return kOk;
}
int MemLock(sqlite3_file*, int) { return kOk; }
int MemUnlock(sqlite3_file*, int) { return kOk; }
int MemCheckReserved(sqlite3_file*, int* out) {
    *out = 0;
    return kOk;
}
int MemFileControl(sqlite3_file*, int, void*) { return kNotFound; }
int MemSectorSize(sqlite3_file*) { return 4096; }
int MemDeviceCharacteristics(sqlite3_file*) { return 0; }
int MemShmMap(sqlite3_file*, int, int, int, void volatile**) { return kOk; }
int MemShmLock(sqlite3_file*, int, int, int) { return kOk; }
void MemShmBarrier(sqlite3_file*) {}
int MemShmUnmap(sqlite3_file*, int) { return kOk; }
int MemFetch(sqlite3_file*, i64, int, void** pp) {
    *pp = nullptr;
    return kOk;
}
int MemUnfetch(sqlite3_file*, i64, void*) { return kOk; }

sqlite3_io_methods g_io = {
    3,          MemClose,   MemRead,   MemWrite,  MemTruncate, MemSync,
    MemFileSize, MemLock,   MemUnlock, MemCheckReserved, MemFileControl,
    MemSectorSize, MemDeviceCharacteristics, MemShmMap, MemShmLock,
    MemShmBarrier, MemShmUnmap, MemFetch, MemUnfetch,
};

int VfsOpen(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags, int* outFlags) {
    MemFile* m = (MemFile*)file;
    memset(m, 0, sizeof(*m));
    m->base.pMethods = &g_io;
    if (outFlags) *outFlags = flags & (kOpenReadonly | kOpenReadWrite);

    m->fd = -1;
    if (name && name[0]) {
        // Never buffer the file: open it once and serve pread/pwrite. Buffering a
        // 42 MB database per handle exhausted memory and crashed the game.
        m->fd = open(name, O_RDWR);
        if (m->fd < 0) m->fd = open(name, O_RDONLY);
        if (m->fd < 0) {
            if (!(flags & kOpenCreate)) return kCanOpen;
            m->fd = open(name, O_RDWR | O_CREAT, 0600);
            if (m->fd < 0) return kCanOpen;
        }
        struct stat st;
        if (fstat(m->fd, &st) == 0) m->size = st.st_size;
        if (flags & kOpenMainDb) {
            LOGI("sqlite shim: opened %s (%lld bytes, fd=%d)", name, (long long)m->size, m->fd);
        }
    }
    return kOk;
}

int VfsDelete(sqlite3_vfs*, const char* name, int) {
    unlink(name);
    return kOk;
}

int VfsAccess(sqlite3_vfs*, const char* name, int flags, int* out) {
    int mode = (flags == kAccessReadWrite) ? R_OK | W_OK : F_OK;
    *out = (access(name, mode) == 0) ? 1 : 0;
    return kOk;
}

int VfsFullPathname(sqlite3_vfs*, const char* name, int nOut, char* out) {
    if (nOut <= 0) return kCanOpen;
    snprintf(out, (size_t)nOut, "%s", name ? name : "");
    return kOk;
}

void* VfsDlOpen(sqlite3_vfs*, const char*) { return nullptr; }
void VfsDlError(sqlite3_vfs*, int n, char* msg) {
    if (n > 0) snprintf(msg, (size_t)n, "dlopen disabled");
}
void (*VfsDlSym(sqlite3_vfs*, void*, const char*))(void) { return nullptr; }
void VfsDlClose(sqlite3_vfs*, void*) {}

int VfsRandomness(sqlite3_vfs*, int n, char* out) {
    for (int i = 0; i < n; ++i) out[i] = (char)(rand() & 0xff);
    return n;
}

int VfsSleep(sqlite3_vfs*, int us) {
    usleep((useconds_t)us);
    return us;
}

int VfsCurrentTime(sqlite3_vfs*, double* out) {
    *out = (double)time(nullptr) / 86400.0 + 2440587.5;
    return kOk;
}

int VfsGetLastError(sqlite3_vfs*, int, char*) { return 0; }

sqlite3_vfs g_vfs = {
    1,
    (int)sizeof(MemFile),
    512,
    nullptr,
    "bpp-mem",
    nullptr,
    VfsOpen,
    VfsDelete,
    VfsAccess,
    VfsFullPathname,
    VfsDlOpen,
    VfsDlError,
    VfsDlSym,
    VfsDlClose,
    VfsRandomness,
    VfsSleep,
    VfsCurrentTime,
    VfsGetLastError,
};

}  // namespace

bool InstallSqliteShim() {
    void* h = dlopen("libgilzoide-sqlite-net.so", RTLD_NOW);
    if (!h) {
        LOGW("sqlite shim: library not loadable (%s)", dlerror());
        return false;
    }
    typedef int (*init_t)();
    typedef int (*reg_t)(sqlite3_vfs*, int);
    init_t init = (init_t)dlsym(h, "sqlite3_initialize");
    reg_t reg = (reg_t)dlsym(h, "sqlite3_vfs_register");
    if (!reg) {
        LOGW("sqlite shim: sqlite3_vfs_register missing");
        return false;
    }
    if (init) init();
    int rc = reg(&g_vfs, 1);  // 1 = make default
    LOGI("sqlite shim: registered as default rc=%d", rc);
    return rc == kOk;
}
