#include "il2cpp_api.h"
#include "offsets.h"
#include <shadowhook.h>
#include <atomic>
#include <unordered_map>

namespace il2 {
Api A{};
void* runtime_handle = nullptr;

namespace {
std::mutex g_mu;
std::unordered_map<std::string, void*> g_images;
std::unordered_map<std::string, void*> g_classes;
std::string g_last_exc;
std::atomic<uint64_t> g_exc_count{0};

void* Sym(const char* name) {
    void* p = dlsym(runtime_handle, name);
    if (!p) LOGW("missing il2cpp export: %s", name);
    return p;
}

template <class T>
void Bind(T& fn, const char* name) {
    fn = reinterpret_cast<T>(Sym(name));
}
}  // namespace

bool Load(int timeout_ms) {
    // We are a DT_NEEDED dependency of libil2cpp, so at constructor time the host
    // library may still be mid-load. Never call dlopen("libil2cpp.so") from here:
    // that can race the loader. Instead wait until the game (libmain) has loaded it,
    // then take a handle without triggering a load.
    const uint64_t deadline = bpp::NowMs() + (uint64_t)timeout_ms;
    while (bpp::NowMs() < deadline) {
        if (dlsym(RTLD_DEFAULT, "il2cpp_domain_get")) {
            runtime_handle = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
            if (runtime_handle) break;
            // RTLD_NOLOAD unsupported: fall back to a plain handle.
            runtime_handle = dlopen("libil2cpp.so", RTLD_NOW | RTLD_GLOBAL);
            if (runtime_handle) break;
        }
        bpp::SleepMs(100);
    }
    if (!runtime_handle) {
        LOGE("libil2cpp.so never appeared: %s", dlerror() ? dlerror() : "?");
        return false;
    }
    Bind(A.domain_get, "il2cpp_domain_get");
    Bind(A.domain_get_assemblies, "il2cpp_domain_get_assemblies");
    Bind(A.assembly_get_image, "il2cpp_assembly_get_image");
    Bind(A.image_get_name, "il2cpp_image_get_name");
    Bind(A.class_from_name, "il2cpp_class_from_name");
    Bind(A.class_get_name, "il2cpp_class_get_name");
    Bind(A.class_get_namespace, "il2cpp_class_get_namespace");
    Bind(A.class_get_parent, "il2cpp_class_get_parent");
    Bind(A.class_get_method_from_name, "il2cpp_class_get_method_from_name");
    Bind(A.class_get_methods, "il2cpp_class_get_methods");
    Bind(A.class_get_field_from_name, "il2cpp_class_get_field_from_name");
    Bind(A.class_get_fields, "il2cpp_class_get_fields");
    Bind(A.class_get_property_from_name, "il2cpp_class_get_property_from_name");
    Bind(A.property_get_get_method, "il2cpp_property_get_get_method");
    Bind(A.property_get_set_method, "il2cpp_property_get_set_method");
    Bind(A.method_get_name, "il2cpp_method_get_name");
    Bind(A.method_get_param_count, "il2cpp_method_get_param_count");
    Bind(A.method_get_class, "il2cpp_method_get_class");
    Bind(A.runtime_invoke, "il2cpp_runtime_invoke");
    Bind(A.runtime_object_init, "il2cpp_runtime_object_init");
    Bind(A.object_new, "il2cpp_object_new");
    Bind(A.object_unbox, "il2cpp_object_unbox");
    Bind(A.object_get_class, "il2cpp_object_get_class");
    Bind(A.field_get_value, "il2cpp_field_get_value");
    Bind(A.field_static_get_value, "il2cpp_field_static_get_value");
    Bind(A.field_set_value, "il2cpp_field_set_value");
    Bind(A.field_set_static_value, "il2cpp_field_static_set_value");
    Bind(A.field_get_offset, "il2cpp_field_get_offset");
    Bind(A.field_get_name, "il2cpp_field_get_name");
    Bind(A.class_value_size, "il2cpp_class_value_size");
    Bind(A.class_instance_size, "il2cpp_class_instance_size");
    Bind(A.class_is_valuetype, "il2cpp_class_is_valuetype");
    Bind(A.class_is_enum, "il2cpp_class_is_enum");
    Bind(A.class_enum_basetype, "il2cpp_class_enum_basetype");
    Bind(A.class_get_type, "il2cpp_class_get_type");
    Bind(A.type_get_object, "il2cpp_type_get_object");
    Bind(A.class_get_element_class, "il2cpp_class_get_element_class");
    Bind(A.array_length, "il2cpp_array_length");
    Bind(A.array_element_size, "il2cpp_array_element_size");
    Bind(A.array_new, "il2cpp_array_new");
    Bind(A.string_new, "il2cpp_string_new");
    Bind(A.string_chars, "il2cpp_string_chars");
    Bind(A.string_length, "il2cpp_string_length");
    Bind(A.thread_attach, "il2cpp_thread_attach");
    Bind(A.thread_current, "il2cpp_thread_current");
    Bind(A.is_vm_thread, "il2cpp_is_vm_thread");
    Bind(A.class_is_assignable_from, "il2cpp_class_is_assignable_from");
    Bind(A.class_has_parent, "il2cpp_class_has_parent");
    Bind(A.value_box, "il2cpp_value_box");
    Bind(A.format_exception, "il2cpp_format_exception");
    Bind(A.free, "il2cpp_free");
    Bind(A.gchandle_new, "il2cpp_gchandle_new");
    Bind(A.gchandle_get_target, "il2cpp_gchandle_get_target");
    Bind(A.gchandle_free, "il2cpp_gchandle_free");
    Bind(A.domain_assembly_open, "il2cpp_domain_assembly_open");
    Bind(A.image_get_class_count, "il2cpp_image_get_class_count");
    Bind(A.object_header_size, "il2cpp_object_header_size");
    Bind(A.array_object_header_size, "il2cpp_array_object_header_size");
    Bind(A.offset_of_array_length, "il2cpp_offset_of_array_length_in_array_object_header");
    Bind(A.offset_of_array_bounds, "il2cpp_offset_of_array_bounds_in_array_object_header");
    Bind(A.image_get_class, "il2cpp_image_get_class");
    Bind(A.get_corlib, "il2cpp_get_corlib");
    if (!A.domain_get || !A.class_from_name || !A.runtime_invoke) {
        LOGE("core il2cpp exports missing");
        return false;
    }
    LOGI("il2cpp api bound (handle=%p)", runtime_handle);
    return true;
}

int ObjectHeaderSize() {
    static int v = -1;
    if (v < 0) {
        int q = A.object_header_size ? A.object_header_size() : 0;
        v = (q > 0 && q <= 0x40) ? q : (int)off::kObjectHeader;
        LOGI("layout: object header = 0x%x", v);
    }
    return v;
}

int ArrayDataOffset() {
    static int v = -1;
    if (v < 0) {
        int len_off = A.offset_of_array_length ? A.offset_of_array_length() : 0;
        int bounds_off = A.offset_of_array_bounds ? A.offset_of_array_bounds() : 0;
        int cand = 0;
        if (len_off > 0) {
            // length is a 32-bit field followed by padding up to the element alignment (8).
            cand = (len_off + 4 + 7) & ~7;
        }
        if (cand <= 0 || cand > 0x40) cand = (int)off::kArrayData;
        v = cand;
        LOGI("layout: array data = 0x%x (length@0x%x bounds@0x%x)", v, len_off, bounds_off);
    }
    return v;
}

namespace {
std::atomic<bool> g_init_seen{false};
std::atomic<bool> g_sh_inited{false};
int (*g_orig_il2cpp_init)(const char*) = nullptr;
int (*g_orig_il2cpp_init16)(const uint16_t*) = nullptr;

int HookIl2cppInit16(const uint16_t* domain_name) {
    int rc = g_orig_il2cpp_init16 ? g_orig_il2cpp_init16(domain_name) : 0;
    g_init_seen.store(true);
    LOGI("il2cpp_init_utf16 returned %d", rc);
    return rc;
}

int HookIl2cppInit(const char* domain_name) {
    if (g_orig_il2cpp_init) {
        int rc = g_orig_il2cpp_init(domain_name);
        g_init_seen.store(true);
        LOGI("il2cpp_init returned %d", rc);
        return rc;
    }
    return 0;
}
}  // namespace

bool InstallInitHook() {
    if (!runtime_handle) return false;
    void* fn = dlsym(runtime_handle, "il2cpp_init");
    if (!fn) {
        LOGW("il2cpp_init not exported; relying on grace period");
        return false;
    }
    if (!g_sh_inited.exchange(true)) shadowhook_init(SHADOWHOOK_MODE_UNIQUE, false);
    void* stub = shadowhook_hook_func_addr(fn, (void*)HookIl2cppInit, (void**)&g_orig_il2cpp_init);
    // Unity usually calls the UTF-16 variant; hook it too or the gate never opens.
    void* fn16 = dlsym(runtime_handle, "il2cpp_init_utf16");
    if (fn16) {
        void* stub16 = shadowhook_hook_func_addr(fn16, (void*)HookIl2cppInit16,
                                                 (void**)&g_orig_il2cpp_init16);
        LOGI("il2cpp_init_utf16 hook: stub=%p", stub16);
    }
    if (!stub || !g_orig_il2cpp_init) {
        int err = shadowhook_get_errno();
        LOGW("il2cpp_init hook failed (errno=%d msg=%s); relying on grace period", err,
             shadowhook_to_errmsg(err));
        return false;
    }
    LOGI("il2cpp_init hooked @%p", fn);
    return true;
}

bool RuntimeInited() { return g_init_seen.load(); }

bool DomainReady() {
    return A.domain_get && A.domain_get() != nullptr;
}

void AttachCurrentThread() {
    if (!A.thread_current || !A.is_vm_thread || !A.thread_attach) return;
    if (!A.is_vm_thread(A.thread_current())) A.thread_attach(A.domain_get());
}

void* Image(const char* image_name) {
    if (!image_name) return nullptr;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_images.find(image_name);
        if (it != g_images.end()) return it->second;
    }
    void* dom = A.domain_get();
    if (!dom) return nullptr;
    size_t n = 0;
    const void** asms = A.domain_get_assemblies(dom, &n);
    if (!asms) return nullptr;
    void* found = nullptr;
    for (size_t i = 0; i < n; ++i) {
        void* img = A.assembly_get_image((void*)asms[i]);
        if (!img) continue;
        const char* nm = A.image_get_name(img);
        if (nm && std::strcmp(nm, image_name) == 0) {
            found = img;
            break;
        }
    }
    if (found) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_images[image_name] = found;
    }
    return found;
}

void* Klass(const char* image_name, const char* namesp, const char* name) {
    void* img = Image(image_name);
    if (!img) {
        LOGW("image not found: %s", image_name ? image_name : "(null)");
        return nullptr;
    }
    void* k = A.class_from_name(img, namesp, name);
    if (!k) {
        static std::mutex warn_mu;
        static std::unordered_map<std::string, uint64_t> last_warn;
        std::string key = std::string(image_name) + "|" + (namesp ? namesp : "") + "|" + name;
        uint64_t now = bpp::NowMs();
        bool emit = false;
        {
            std::lock_guard<std::mutex> lk(warn_mu);
            auto it = last_warn.find(key);
            if (it == last_warn.end() || now - it->second > 10000) {
                last_warn[key] = now;
                emit = true;
            }
        }
        if (emit) LOGW("class not found: %s.%s in %s", namesp ? namesp : "", name, image_name);
    }
    return k;
}

void* KlassByName(const char* name) {
    if (!name) return nullptr;
    std::string key = std::string("#") + name;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_classes.find(key);
        if (it != g_classes.end()) return it->second;
    }
    void* dom = A.domain_get();
    if (!dom || !A.image_get_class_count || !A.image_get_class) return nullptr;
    size_t n = 0;
    const void** asms = A.domain_get_assemblies(dom, &n);
    void* found = nullptr;
    int scanned = 0;
    for (size_t i = 0; i < n && !found; ++i) {
        void* img = A.assembly_get_image((void*)asms[i]);
        if (!img) continue;
        int count = A.image_get_class_count(img);
        scanned += count;
        for (int c = 0; c < count; ++c) {
            void* k = A.image_get_class(img, (size_t)c);
            if (!k) continue;
            const char* cn = A.class_get_name(k);
            if (cn && std::strcmp(cn, name) == 0) {
                found = k;
                break;
            }
        }
    }
    if (found) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_classes[key] = found;
        LOGI("class '%s' resolved by scan (%d classes scanned)", name, scanned);
    }
    return found;
}

void* KlassAny(const char* namesp, const char* name) {
    std::string key = std::string(namesp ? namesp : "") + "|" + name;
    {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_classes.find(key);
        if (it != g_classes.end()) return it->second;
    }
    void* dom = A.domain_get();
    if (!dom) return nullptr;
    void* found = nullptr;
    // Fast path: exact namespace across all images.
    if (namesp && *namesp) {
        size_t n = 0;
        const void** asms = A.domain_get_assemblies(dom, &n);
        for (size_t i = 0; i < n && !found; ++i) {
            void* img = A.assembly_get_image((void*)asms[i]);
            if (!img) continue;
            found = A.class_from_name(img, namesp, name);
        }
    }
    // Slow path: any namespace with this simple name.
    if (!found) found = KlassByName(name);
    if (found) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_classes[key] = found;
    }
    return found;
}

void* Method(void* klass, const char* name, int argc) {
    if (!klass) return nullptr;
    return A.class_get_method_from_name(klass, name, argc);
}

void* Field(void* klass, const char* name) {
    if (!klass) return nullptr;
    return A.class_get_field_from_name(klass, name);
}

std::string ClassName(void* klass) {
    if (!klass) return "?";
    const char* ns = A.class_get_namespace(klass);
    const char* n = A.class_get_name(klass);
    return std::string(ns && *ns ? std::string(ns) + "." : "") + (n ? n : "?");
}

std::string MethodName(void* m) {
    if (!m) return "?";
    const char* n = A.method_get_name(m);
    void* k = A.method_get_class(m);
    return ClassName(k) + "::" + (n ? n : "?");
}

uint64_t ExceptionCount() { return g_exc_count.load(); }

std::string LastException() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_last_exc;
}

static void CaptureException(void* exc) {
    if (!exc) return;
    char* s = A.format_exception ? A.format_exception(exc) : nullptr;
    std::string msg = s ? s : "(exception)";
    if (s && A.free) A.free(s);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_last_exc = msg;
        g_exc_count.fetch_add(1);
    }
    LOGE("managed exception: %s", msg.c_str());
}

void* Invoke(void* method, void* obj, void** params) {
    if (!method) return nullptr;
    void* exc = nullptr;
    void* r = A.runtime_invoke(method, obj, params, &exc);
    if (exc) CaptureException(exc);
    return r;
}

void* Invoke0(void* method, void* obj) { return Invoke(method, obj, nullptr); }

void* Invoke1(void* method, void* obj, void* a) {
    void* p[1] = {a};
    return Invoke(method, obj, p);
}

void* InvokeStatic0(void* method) { return Invoke(method, nullptr, nullptr); }

void* InvokeStatic1(void* method, void* a) {
    void* p[1] = {a};
    return Invoke(method, nullptr, p);
}

std::string Str(void* s) {
    if (!s) return {};
    const uint16_t* chars = A.string_chars(s);
    int len = A.string_length(s);
    return bpp::Utf16ToUtf8(chars, len);
}

void* NewStr(const std::string& s) {
    return A.string_new(s.c_str());
}

void* FieldPtr(void* obj, void* field) {
    if (!obj || !field) return nullptr;
    size_t off = A.field_get_offset(field);
    return *(void**)((char*)obj + off);
}

int32_t FieldI32(void* obj, void* field) {
    int32_t v = 0;
    if (obj && field) A.field_get_value(obj, field, &v);
    return v;
}

uint8_t FieldU8(void* obj, void* field) {
    uint8_t v = 0;
    if (obj && field) A.field_get_value(obj, field, &v);
    return v;
}

bool FieldBool(void* obj, void* field) {
    uint8_t v = 0;
    if (obj && field) A.field_get_value(obj, field, &v);
    return v != 0;
}

std::string FieldString(void* obj, void* field) {
    return Str(FieldPtr(obj, field));
}

void* ArrayData(void* array) { return array ? (char*)array + ArrayDataOffset() : nullptr; }

int ArrayLen(void* array) {
    if (!array || !A.array_length) return 0;
    return (int)A.array_length(array);
}

void* ArrayAt(void* array, int index) {
    if (!array) return nullptr;
    void* k = A.object_get_class(array);
    int size = A.array_element_size ? A.array_element_size(k) : 0;
    if (size <= 0) {
        // Fallback: reference-type elements are pointers.
        void* elem = A.class_get_element_class(k);
        if (elem && A.class_is_valuetype(elem)) {
            uint32_t align = 0;
            size = A.class_value_size(elem, &align);
        }
        if (size <= 0) size = 8;
    }
    return (char*)ArrayData(array) + (size_t)index * (size_t)size;
}

void* Box(void* klass, const void* data, int size) {
    if (!klass || !data || size <= 0) return nullptr;
    void* tmp = malloc((size_t)size);
    std::memcpy(tmp, data, (size_t)size);
    void* boxed = A.value_box(klass, tmp);
    free(tmp);
    return boxed;
}

void* NewObject(void* klass) {
    void* obj = A.object_new(klass);
    if (obj) {
        void* exc = nullptr;
        A.runtime_object_init(obj, &exc);
        if (exc) CaptureException(exc);
    }
    return obj;
}

bool Enumerate(void* enumerable, const std::function<void(void* item)>& fn) {
    if (!enumerable) return false;
    void* klass = A.object_get_class(enumerable);
    void* m = A.class_get_method_from_name(klass, "GetEnumerator", 0);
    if (!m) {
        LOGE("no GetEnumerator on %s", ClassName(klass).c_str());
        return false;
    }
    void* it = Invoke0(m, enumerable);
    if (!it) return false;
    void* itk = A.object_get_class(it);
    void* move = A.class_get_method_from_name(itk, "MoveNext", 0);
    void* cur = A.class_get_method_from_name(itk, "get_Current", 0);
    if (!move || !cur) {
        LOGE("enumerator missing MoveNext/Current on %s", ClassName(itk).c_str());
        return false;
    }
    int guard = 0;
    while (guard++ < 100000) {
        void* r = Invoke0(move, it);
        if (!r) break;
        uint8_t more = 0;
        std::memcpy(&more, (char*)r + ObjectHeaderSize(), 1);
        if (!more) break;
        void* item = Invoke0(cur, it);
        if (item) fn(item);
    }
    return true;
}

void DumpImages() {
    void* dom = A.domain_get();
    if (!dom) return;
    size_t n = 0;
    const void** asms = A.domain_get_assemblies(dom, &n);
    LOGI("assemblies: %zu", n);
    for (size_t i = 0; i < n; ++i) {
        void* img = A.assembly_get_image((void*)asms[i]);
        const char* nm = img ? A.image_get_name(img) : nullptr;
        LOGI("  [%zu] %s", i, nm ? nm : "?");
    }
}

}  // namespace il2
