#include "unity_ui.h"
#include "il2cpp_api.h"

namespace unityui {
namespace {

void* K_TMP_Text = nullptr;
void* K_Canvas = nullptr;
void* K_Object = nullptr;
void* M_FindObjectsOfType = nullptr;
void* M_Instantiate = nullptr;

void* g_label = nullptr;
uint32_t g_label_handle = 0;
void* g_set_text = nullptr;
void* g_set_anchored = nullptr;
void* g_set_size = nullptr;
void* g_set_fontsize = nullptr;
void* g_set_anchor_min = nullptr;
void* g_set_anchor_max = nullptr;
void* g_set_pivot = nullptr;

void* MethodInHierarchy(void* klass, const char* name, int argc) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* m = il2::A.class_get_method_from_name(k, name, argc);
        if (m) return m;
    }
    return nullptr;
}

void* PropGetter(void* klass, const char* prop) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* p = il2::A.class_get_property_from_name(k, prop);
        if (p) {
            void* g = il2::A.property_get_get_method(p);
            if (g) return g;
        }
    }
    return nullptr;
}

void* PropSetter(void* klass, const char* prop) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* p = il2::A.class_get_property_from_name(k, prop);
        if (p) {
            void* s = il2::A.property_get_set_method(p);
            if (s) return s;
        }
    }
    return nullptr;
}

void* FirstOfType(void* klass, int* out_count) {
    void* type = il2::A.type_get_object(il2::A.class_get_type(klass));
    void* arr = il2::Invoke1(M_FindObjectsOfType, nullptr, type);
    int n = il2::ArrayLen(arr);
    if (out_count) *out_count = n;
    for (int i = 0; i < n; ++i) {
        void* o = *(void**)il2::ArrayAt(arr, i);
        if (o) return o;
    }
    return nullptr;
}

void SetVec2(void* setter, void* obj, float x, float y) {
    if (!setter || !obj) return;
    float v[2] = {x, y};
    il2::Invoke1(setter, obj, v);
}

}  // namespace

bool Init() {
    if (K_TMP_Text) return true;
    K_TMP_Text = il2::KlassAny("TMPro", "TMP_Text");
    if (!K_TMP_Text) {
        LOGE("unityui: TMP_Text not found");
        return false;
    }
    K_Canvas = il2::Klass("UnityEngine.UIModule.dll", "UnityEngine", "Canvas");
    K_Object = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    if (!K_Object) return false;
    M_FindObjectsOfType = il2::Method(K_Object, "FindObjectsOfType", 1);
    M_Instantiate = il2::Method(K_Object, "Instantiate", 1);
    LOGI("unityui: classes TMP_Text=%p Canvas=%p Object=%p find=%p instantiate=%p", K_TMP_Text,
         K_Canvas, K_Object, M_FindObjectsOfType, M_Instantiate);
    return M_FindObjectsOfType && M_Instantiate && K_Canvas;
}

bool Spawn(const std::string& text) {
    if (g_label) return true;
    if (!Init()) return false;

    int text_count = 0;
    void* src = FirstOfType(K_TMP_Text, &text_count);
    LOGI("unityui: %d TMP texts, source=%p", text_count, src);
    if (!src) return false;

    void* clone = il2::Invoke1(M_Instantiate, nullptr, src);
    LOGI("unityui: clone=%p", clone);
    if (!clone) return false;
    void* ck = il2::A.object_get_class(clone);

    // Reparent under a root canvas so the clone is rendered by the engine.
    int canvas_count = 0;
    void* canvas = FirstOfType(K_Canvas, &canvas_count);
    LOGI("unityui: %d canvases, canvas=%p", canvas_count, canvas);
    if (!canvas) return false;
    void* cgetTr = PropGetter(il2::A.object_get_class(canvas), "transform");
    void* canvasTr = cgetTr ? il2::Invoke0(cgetTr, canvas) : nullptr;
    void* lgetTr = PropGetter(ck, "transform");
    void* labelTr = lgetTr ? il2::Invoke0(lgetTr, clone) : nullptr;
    void* setParent = MethodInHierarchy(il2::A.object_get_class(labelTr), "SetParent", 2);
    LOGI("unityui: canvasTr=%p labelTr=%p setParent=%p", canvasTr, labelTr, setParent);
    if (!canvasTr || !labelTr || !setParent) return false;
    uint8_t world_stays = 0;
    void* pargs[2] = {canvasTr, &world_stays};
    il2::Invoke(setParent, labelTr, pargs);

    void* getRect = PropGetter(ck, "rectTransform");
    void* rect = getRect ? il2::Invoke0(getRect, clone) : nullptr;
    LOGI("unityui: rect=%p", rect);
    if (rect) {
        void* rk = il2::A.object_get_class(rect);
        g_set_anchor_min = PropSetter(rk, "anchorMin");
        g_set_anchor_max = PropSetter(rk, "anchorMax");
        g_set_pivot = PropSetter(rk, "pivot");
        g_set_anchored = PropSetter(rk, "anchoredPosition");
        g_set_size = PropSetter(rk, "sizeDelta");
        // Top-left anchored, origin at the top-left corner.
        SetVec2(g_set_anchor_min, rect, 0.0f, 1.0f);
        SetVec2(g_set_anchor_max, rect, 0.0f, 1.0f);
        SetVec2(g_set_pivot, rect, 0.0f, 1.0f);
        SetVec2(g_set_anchored, rect, 24.0f, -24.0f);
        SetVec2(g_set_size, rect, 760.0f, 520.0f);
    }
    g_set_fontsize = PropSetter(ck, "fontSize");
    if (g_set_fontsize) {
        float size = 22.0f;
        il2::Invoke1(g_set_fontsize, clone, &size);
    }
    g_set_text = PropSetter(ck, "text");
    LOGI("unityui: set_text=%p fontSize=%p anchored=%p", g_set_text, g_set_fontsize,
         g_set_anchored);
    if (!g_set_text) return false;

    g_label = clone;
    g_label_handle = il2::A.gchandle_new ? il2::A.gchandle_new(clone, false) : 0;
    SetText(text);
    LOGI("unityui: spawned label=%p", g_label);
    return true;
}

void SetText(const std::string& text) {
    if (!g_set_text || !g_label_handle) return;
    void* obj = il2::A.gchandle_get_target(g_label_handle);
    if (!obj) return;
    void* s = il2::NewStr(text);
    if (s) il2::Invoke1(g_set_text, obj, s);
}

void Destroy() {
    if (g_label_handle && il2::A.gchandle_free) {
        il2::A.gchandle_free(g_label_handle);
        g_label_handle = 0;
    }
    g_label = nullptr;
}

bool Active() { return g_label != nullptr; }

}  // namespace unityui
