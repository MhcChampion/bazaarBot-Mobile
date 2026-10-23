// Thin, cached wrapper over the IL2CPP runtime exported API.
#pragma once
#include "bpp_common.h"
#include <functional>

namespace il2 {

// ---- raw runtime signatures (only the subset we use) ----
struct Api {
    void* (*domain_get)();
    const void** (*domain_get_assemblies)(void* domain, size_t* size);
    void* (*assembly_get_image)(void* assembly);
    const char* (*image_get_name)(void* image);
    void* (*class_from_name)(void* image, const char* ns, const char* name);
    const char* (*class_get_name)(void* klass);
    const char* (*class_get_namespace)(void* klass);
    void* (*class_get_parent)(void* klass);
    void* (*class_get_method_from_name)(void* klass, const char* name, int argc);
    const void* (*class_get_methods)(void* klass, void** iter);
    void* (*class_get_field_from_name)(void* klass, const char* name);
    const void* (*class_get_fields)(void* klass, void** iter);
    void* (*class_get_property_from_name)(void* klass, const char* name);
    void* (*property_get_get_method)(void* prop);
    void* (*property_get_set_method)(void* prop);
    const char* (*method_get_name)(void* method);
    int (*method_get_param_count)(void* method);
    void* (*method_get_class)(void* method);
    void* (*runtime_invoke)(void* method, void* obj, void** params, void** exc);
    int (*runtime_object_init)(void* obj, void** exc);
    void* (*object_new)(void* klass);
    void* (*object_unbox)(void* obj);
    void* (*object_get_class)(void* obj);
    void* (*field_get_value)(void* obj, void* field, void* value);
    void* (*field_static_get_value)(void* field, void* value);
    void* (*field_set_value)(void* obj, void* field, void* value);
    void* (*field_set_static_value)(void* field, void* value);
    size_t (*field_get_offset)(void* field);
    const char* (*field_get_name)(void* field);
    int (*class_value_size)(void* klass, uint32_t* align);
    int (*class_instance_size)(void* klass);
    int (*class_is_valuetype)(void* klass);
    int (*class_is_enum)(void* klass);
    void* (*class_enum_basetype)(void* klass);
    void* (*class_get_type)(void* klass);
    void* (*type_get_object)(void* type);
    void* (*class_get_element_class)(void* klass);
    uintptr_t (*array_length)(void* array);
    int (*array_element_size)(void* array_class);
    void* (*array_new)(void* element_class, uintptr_t length);
    void* (*string_new)(const char* str);
    const uint16_t* (*string_chars)(void* str);
    int32_t (*string_length)(void* str);
    void* (*thread_attach)(void* domain);
    void* (*thread_current)();
    int (*is_vm_thread)(void* thread);
    int (*class_is_assignable_from)(void* klass, void* from);
    int (*class_has_parent)(void* klass, void* parent);
    void* (*value_box)(void* klass, void* data);
    char* (*format_exception)(void* exc);
    void (*free)(void* p);
    uint32_t (*gchandle_new)(void* obj, bool pinned);
    void* (*gchandle_get_target)(uint32_t handle);
    void (*gchandle_free)(uint32_t handle);
    void* (*domain_assembly_open)(void* domain, const char* name);
    int (*image_get_class_count)(void* image);
    int (*object_header_size)();
    int (*array_object_header_size)();
    int (*offset_of_array_length)();
    int (*offset_of_array_bounds)();
    void* (*image_get_class)(void* image, size_t index);
    void* (*get_corlib)();
};

extern Api A;
extern void* runtime_handle;

bool Load(int timeout_ms);
bool InstallInitHook();   // hooks il2cpp_init so we never touch the runtime too early
bool RuntimeInited();     // true once il2cpp_init has returned
// Runtime-queried layout constants (falls back to the dump-verified defaults).
int ObjectHeaderSize();
int ArrayDataOffset();
bool DomainReady();
void AttachCurrentThread();

// Cached lookups. Return nullptr when not found.
void* Image(const char* image_name);
void* Klass(const char* image_name, const char* namesp, const char* name);
void* KlassAny(const char* namesp, const char* name);   // namespace hint, then full name scan
void* KlassByName(const char* name);                    // full scan by simple name (any namespace)
void* Method(void* klass, const char* name, int argc);
void* Field(void* klass, const char* name);

std::string ClassName(void* klass);
std::string MethodName(void* method);

// Invoke helpers: params are raw pointers to argument storage (value types) or object pointers.
void* Invoke(void* method, void* obj, void** params);
void* Invoke0(void* method, void* obj);
void* Invoke1(void* method, void* obj, void* a);
void* InvokeStatic0(void* method);
void* InvokeStatic1(void* method, void* a);
std::string LastException();
uint64_t ExceptionCount();   // monotonically increasing

std::string Str(void* il2cpp_string);
void* NewStr(const std::string& s);
std::string FieldString(void* obj, void* field);   // reads an IL2CPP string field
int32_t FieldI32(void* obj, void* field);
uint8_t FieldU8(void* obj, void* field);
void* FieldPtr(void* obj, void* field);
bool FieldBool(void* obj, void* field);
void* ArrayData(void* array);                       // pointer to first element
int ArrayLen(void* array);
void* ArrayAt(void* array, int index);              // element pointer
void* Box(void* klass, const void* data, int size);
void* NewObject(void* klass);                       // object_new + default ctor

// Enumerate IEnumerable<T>: returns boxed items (caller must not free).
bool Enumerate(void* enumerable, const std::function<void(void* item)>& fn);

void DumpImages();

}  // namespace il2
