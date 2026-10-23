#include "bpp_features.h"
#include "game_api.h"
#include "il2cpp_api.h"
#include "ui.h"
#include "catalog.h"
#include "offsets.h"
#include "unity_ui.h"
#include "crash_guard.h"
#include "bpp_net.h"
#include "bpp_ui.h"
#include "card_table.h"
#include "bpp_web_assets.h"
#include <thread>
#include <dlfcn.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cmath>
#include <ctime>
#include <cstdarg>
#include <set>
#include <algorithm>
#include <shadowhook.h>

#include <imgui.h>
#include <atomic>
#include <mutex>

namespace features {
namespace {

std::atomic<bool> g_game_ready{false};
std::atomic<bool> g_blocked{false};
float g_last_toggle_ms = 0.f;
bool g_was_three = false;

// ---- Unity handles ----
void* K_Input = nullptr;
void* K_Screen = nullptr;
void* K_Canvas = nullptr;
void* K_CanvasGroup = nullptr;
void* K_GameObject = nullptr;
void* K_Object = nullptr;

void* M_get_touchCount = nullptr;
void* M_GetTouch = nullptr;
void* M_ScreenHeight = nullptr;
void* M_AddComponent = nullptr;
void* M_GetComponent = nullptr;
void* M_FindObjectsOfType = nullptr;
void* M_set_blocksRaycasts = nullptr;
void* M_set_interactable = nullptr;

std::mutex g_block_mu;
std::vector<void*> g_blocked_groups;

int BoxedInt(void* r) {
    if (!r) return 0;
    return *(int32_t*)((char*)r + il2::ObjectHeaderSize());
}

void ResolveUnity() {
    if (!K_Input) {
        K_Input = il2::KlassAny("UnityEngine", "Input");
        if (!K_Input) K_Input = il2::KlassByName("Input");
        if (K_Input) {
            M_get_touchCount = il2::Method(K_Input, "get_touchCount", 0);
            M_GetTouch = il2::Method(K_Input, "GetTouch", 1);
        }
    }
    if (!K_Screen) {
        K_Screen = il2::KlassAny("UnityEngine", "Screen");
        if (!K_Screen) K_Screen = il2::KlassByName("Screen");
        if (K_Screen) M_ScreenHeight = il2::Method(K_Screen, "get_height", 0);
    }
    if (!K_Canvas) {
        K_Canvas = il2::KlassAny("UnityEngine", "Canvas");
    }
    if (!K_CanvasGroup) {
        K_CanvasGroup = il2::KlassAny("UnityEngine", "CanvasGroup");
    }
    if (!K_GameObject) K_GameObject = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "GameObject");
    if (!K_Object) K_Object = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
    if (K_GameObject && !M_AddComponent) M_AddComponent = il2::Method(K_GameObject, "AddComponent", 1);
    if (K_GameObject && !M_GetComponent) M_GetComponent = il2::Method(K_GameObject, "GetComponent", 1);
    if (K_Object && !M_FindObjectsOfType)
        M_FindObjectsOfType = il2::Method(K_Object, "FindObjectsOfType", 1);
    if (K_CanvasGroup) {
        void* p = il2::A.class_get_property_from_name(K_CanvasGroup, "blocksRaycasts");
        if (p) M_set_blocksRaycasts = il2::A.property_get_set_method(p);
        void* p2 = il2::A.class_get_property_from_name(K_CanvasGroup, "interactable");
        if (p2) M_set_interactable = il2::A.property_get_set_method(p2);
    }
}

void BlockGameUi(bool block) {
    std::lock_guard<std::mutex> lk(g_block_mu);
    if (!K_Canvas || !K_CanvasGroup || !M_FindObjectsOfType || !M_AddComponent) return;

    if (block) {
        void* type_obj = il2::A.type_get_object(il2::A.class_get_type(K_CanvasGroup));
        void* arr = il2::Invoke1(M_FindObjectsOfType, nullptr, type_obj);
        int n = il2::ArrayLen(arr);
        for (int i = 0; i < n; ++i) {
            void* comp = *(void**)il2::ArrayAt(arr, i);
            void* go = nullptr;
            if (comp) {
                void* p = il2::A.class_get_property_from_name(il2::A.object_get_class(comp), "gameObject");
                void* getter = p ? il2::A.property_get_get_method(p) : nullptr;
                if (getter) go = il2::Invoke0(getter, comp);
            }
            if (!go) continue;
            // Reuse an existing CanvasGroup: AddComponent on a [DisallowMultipleComponent]
            // type is a no-op/error and would leave raycasts untouched.
            void* cg = M_GetComponent ? il2::Invoke1(M_GetComponent, go, type_obj) : nullptr;
            if (!cg) cg = il2::Invoke1(M_AddComponent, go, type_obj);
            if (!cg) continue;
            uint8_t zero = 0;
            if (M_set_blocksRaycasts) il2::Invoke1(M_set_blocksRaycasts, cg, &zero);
            if (M_set_interactable) il2::Invoke1(M_set_interactable, cg, &zero);
            g_blocked_groups.push_back(cg);
        }
        LOGI("game ui blocked (%zu canvas groups)", g_blocked_groups.size());
    } else {
        uint8_t one = 1;
        for (void* cg : g_blocked_groups) {
            if (M_set_blocksRaycasts) il2::Invoke1(M_set_blocksRaycasts, cg, &one);
            if (M_set_interactable) il2::Invoke1(M_set_interactable, cg, &one);
        }
        g_blocked_groups.clear();
        LOGI("game ui unblocked");
    }
}

// ---- Input System (the project disables legacy Input, so Input.GetTouch is 0) ----
void* K_Touchscreen = nullptr;
void* K_Mouse = nullptr;

void* PropGetterOrNull(void* klass, const char* name) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* p = il2::A.class_get_property_from_name(k, name);
        if (p) {
            void* g = il2::A.property_get_get_method(p);
            if (g) return g;
        }
    }
    return nullptr;
}

void* PropValue(void* obj, const char* name) {
    if (!obj) return nullptr;
    void* g = PropGetterOrNull(il2::A.object_get_class(obj), name);
    return g ? il2::Invoke0(g, obj) : nullptr;
}

void* StaticPropValue(void* klass, const char* name) {
    if (!klass) return nullptr;
    void* g = PropGetterOrNull(klass, name);
    return g ? il2::Invoke0(g, nullptr) : nullptr;
}

bool BoolProp(void* obj, const char* name) {
    void* v = PropValue(obj, name);
    return v != nullptr && BoxedInt(v) != 0;
}

bool ReadVec2(void* control, float& x, float& y) {
    if (!control) return false;
    static void* read0 = nullptr;
    static void* read2 = nullptr;
    static bool probed = false;
    static bool logged = false;
    if (!probed) {
        probed = true;
        for (void* k = il2::A.object_get_class(control); k; k = il2::A.class_get_parent(k)) {
            if (!read0) {
                void* m = il2::A.class_get_method_from_name(k, "ReadValue", 0);
                if (m) read0 = m;
            }
            if (!read2) {
                void* m = il2::A.class_get_method_from_name(k, "ReadValue", 2);
                if (m) read2 = m;
            }
        }
        LOGI("vec2 reader: ReadValue/0=%p ReadValue/2=%p class=%s", read0, read2,
             il2::ClassName(il2::A.object_get_class(control)).c_str());
    }
    if (read0) {
        void* r = il2::Invoke0(read0, control);
        if (r) {
            const float* v = (const float*)((char*)r + il2::ObjectHeaderSize());
            x = v[0];
            y = v[1];
            if (!logged) { logged = true; LOGI("vec2 via ReadValue() -> %.1f,%.1f", x, y); }
            return true;
        }
    }
    if (read2) {
        float buf[2] = {0.0f, 0.0f};
        int size = 8;
        void* args[2] = {buf, &size};
        il2::Invoke(read2, control, args);
        x = buf[0];
        y = buf[1];
        if (!logged) { logged = true; LOGI("vec2 via ReadValue(buf) -> %.1f,%.1f", x, y); }
        return true;
    }
    return false;
}

// Returns true when the engine reports a pointer this frame.
bool ReadEnginePointer(float& x, float& y, bool& down) {
    if (!K_Touchscreen) K_Touchscreen = il2::KlassAny("UnityEngine.InputSystem", "Touchscreen");
    if (!K_Mouse) K_Mouse = il2::KlassAny("UnityEngine.InputSystem", "Mouse");
    // Touch first, but keep going: desktop emulators inject mouse events while a
    // Touchscreen device still exists, so a pressed mouse must win over an idle touch.
    bool have_pos = false;
    bool touch_down = false;
    if (K_Touchscreen) {
        void* cur = StaticPropValue(K_Touchscreen, "current");
        if (cur) {
            void* touch = PropValue(cur, "primaryTouch");
            if (touch) {
                void* press = PropValue(touch, "press");
                touch_down = press ? BoolProp(press, "isPressed") : false;
                void* pos = PropValue(touch, "position");
                have_pos = ReadVec2(pos, x, y) || have_pos;
                if (touch_down) { down = true; return true; }
            }
        }
    }
    if (K_Mouse) {
        void* cur = StaticPropValue(K_Mouse, "current");
        if (cur) {
            bool mouse_down = BoolProp(PropValue(cur, "leftButton"), "isPressed");
            void* pos = PropValue(cur, "position");
            float mx = x, my = y;
            if (ReadVec2(pos, mx, my) && (mouse_down || !have_pos)) {
                x = mx;
                y = my;
                have_pos = true;
                if (mouse_down) { down = true; return true; }
            }
        }
    }
    down = false;
    return have_pos;
}

int g_force_tab = -1;

void LogInputDiagnostics() {
    static bool done = false;
    if (done) return;
    done = true;
    void* ts_cls = il2::KlassAny("UnityEngine.InputSystem", "Touchscreen");
    void* m_cls = il2::KlassAny("UnityEngine.InputSystem", "Mouse");
    void* kbd = il2::KlassAny("UnityEngine.InputSystem", "Keyboard");
    void* ts_cur = StaticPropValue(ts_cls, "current");
    void* m_cur = StaticPropValue(m_cls, "current");
    void* k_cur = StaticPropValue(kbd, "current");
    LOGI("input diag: class touchscreen=%p mouse=%p keyboard=%p; current ts=%p mouse=%p kbd=%p",
         ts_cls, m_cls, kbd, ts_cur, m_cur, k_cur);
    if (ts_cur) {
        void* touch = PropValue(ts_cur, "primaryTouch");
        void* press = touch ? PropValue(touch, "press") : nullptr;
        LOGI("input diag: primaryTouch=%p press=%p", touch, press);
    }
}

void UpdateTouch() {
    if (!M_get_touchCount || !M_GetTouch) return;
    float ex = 0, ey = 0;
    bool edown = false;
    if (ReadEnginePointer(ex, ey, edown)) {
        int h = M_ScreenHeight ? BoxedInt(il2::Invoke0(M_ScreenHeight, nullptr)) : 0;
        if (h > 0) ey = (float)h - ey;
        ui::UpdateTouch(ex, ey, edown, edown ? 1 : 0);
        static bool last_down = false;
        if (edown != last_down) {
            last_down = edown;
            LOGI("pointer %s at %.0f,%.0f", edown ? "down" : "up", ex, ey);
        }
        static bool logged_es = false;
        if (!logged_es) {
            logged_es = true;
            LOGI("input source: InputSystem (x=%.0f y=%.0f down=%d)", ex, ey, edown ? 1 : 0);
        }
        return;
    }
    void* countObj = il2::Invoke0(M_get_touchCount, nullptr);
    int count = BoxedInt(countObj);
    static bool logged = false;
    if (!logged && countObj) {
        logged = true;
        // Confirms the il2cpp_runtime_invoke value-type convention on device.
        void* k = il2::A.object_get_class(countObj);
        LOGI("touchCount=%d boxedClass=%s (expect System.Int32)", count,
             k ? il2::ClassName(k).c_str() : "?");
    }
    float x = 0, y = 0;
    bool down = false;
    if (count > 0) {
        int idx = 0;
        void* touch = il2::Invoke1(M_GetTouch, nullptr, &idx);
        if (touch) {
            // boxed UnityEngine.Touch: m_Position is a Vector2 at +0x04
            float* pos = (float*)((char*)touch + il2::ObjectHeaderSize() + off::kTouchPosition);
            int phase = *(int32_t*)((char*)touch + il2::ObjectHeaderSize() + off::kTouchPhase);
            x = pos[0];
            y = pos[1];
            down = (phase != 3 /*Ended*/ && phase != 4 /*Canceled*/);
            int h = M_ScreenHeight ? BoxedInt(il2::Invoke0(M_ScreenHeight, nullptr)) : 0;
            if (h > 0) y = (float)h - y;
        }
    }
    ui::UpdateTouch(x, y, down, count);

    // Three-finger tap toggles the overlay.
    bool three = count >= 3;
    if (three && !g_was_three) {
        float now = (float)bpp::NowMs();
        if (now - g_last_toggle_ms > 400.f) {
            g_last_toggle_ms = now;
            ui::RequestToggle();
            LOGI("3-finger toggle");
        }
    }
    g_was_three = three;
}

// ---------- UI ----------
char g_search[64] = {0};

// ---------- persisted settings ----------
struct Settings {
    float speed = 1.0f;
    bool tooltip = true;
    float scale = 1.0f;
    bool floating_button = true;   // floating launcher button by default
    std::string launcher = "side";
    bool companion_floating=true,companion_follow=true,companion_upload=false;
};
Settings g_settings;
std::string g_last_command;
bool g_force_push = true;
std::set<std::string> g_watched;
int g_watch_hits=0;
std::vector<game::ScreenCard> g_screen_cards;
std::string g_capture_status="社区上传尚未接通；开启后十胜截图与阵容先保存到本机。";
std::string g_pending_capture;
bool g_capture_tenwin=false;
bool g_tenwin_captured=false;
uint64_t g_capture_run=0,g_capture_started=0;
bool g_capture_timeout=false;
struct TenwinCapture {
    std::string fingerprint,path,json;
    uint64_t run=0,retry_at=0;
    int attempts=0;
    bool json_ready=false,finished=false;
} g_tenwin;

std::string CaptureFingerprint(const game::BoardSnapshot& snap){
    std::string key=snap.hero+":"+std::to_string(snap.day)+":"+std::to_string(snap.hour)+":"+std::to_string(snap.victories)+":"+std::to_string(snap.losses);
    auto cards=[&](const std::vector<game::CardInfo>& list){key+='|';for(const auto& c:list)
        key+=':'+c.guid+":"+c.instance+":"+std::to_string(c.tier)+":"+std::to_string(c.slot)+":"+std::to_string(c.enchantment);};
    cards(snap.board);cards(snap.skills);return key;
}

bool CaptureMatches(const game::BoardSnapshot& snap){
    return !g_tenwin.finished&&g_tenwin.run==g_capture_run&&snap.in_run&&!snap.in_combat&&
        snap.victories>=10&&!snap.board.empty()&&CaptureFingerprint(snap)==g_tenwin.fingerprint;
}

void ObserveCaptureRun(const game::BoardSnapshot& snap){
    static bool initialized=false,was_in_run=false;
    static int day=0,victories=0;
    static std::string hero;
    if(!initialized||snap.in_run!=was_in_run||snap.day<day||snap.victories<victories||snap.hero!=hero){
        ++g_capture_run;g_tenwin_captured=false;
    }
    initialized=true;was_in_run=snap.in_run;day=snap.day;victories=snap.victories;hero=snap.hero;
    if(!g_tenwin.fingerprint.empty()&&!g_tenwin.finished&&!CaptureMatches(snap)){
        g_tenwin.finished=true;
        g_capture_status="十胜记录未完成：对局或画面已变化，已停止补拍；社区上传尚未接通。";g_force_push=true;
    }
}

bool CapturePngValid(const std::string& path){
    static const unsigned char signature[]={137,80,78,71,13,10,26,10};
    static const unsigned char ending[]={0,0,0,0,73,69,78,68,174,66,96,130};
    FILE* f=fopen(path.c_str(),"rb");if(!f)return false;
    unsigned char head[8]={},tail[12]={};
    bool valid=fread(head,1,sizeof(head),f)==sizeof(head)&&!memcmp(head,signature,sizeof(head))&&
        fseek(f,0,SEEK_END)==0&&ftell(f)>128&&fseek(f,-(long)sizeof(tail),SEEK_END)==0&&
        fread(tail,1,sizeof(tail),f)==sizeof(tail)&&!memcmp(tail,ending,sizeof(tail));
    fclose(f);return valid;
}

void CaptureFailed(){
    g_tenwin.retry_at=bpp::NowMs()+3000;
    g_tenwin.finished=g_tenwin.attempts>=3;
    g_capture_status=g_tenwin.finished?"十胜记录未完成：已尝试三次，停止自动重试；社区上传尚未接通。":
        "十胜记录未完成，3秒后仅在同一对局和阵容画面重试；社区上传尚未接通。";
    g_force_push=true;
}

void StartCapture(const std::string& path,bool tenwin){
    g_pending_capture=path;g_capture_tenwin=tenwin;g_capture_started=bpp::NowMs();g_capture_timeout=false;
    CaptureGameScreen(path);
}

std::string SettingsPath() {
    static std::string p = bpp::MediaDir() + "/bpp_settings.txt";
    return p;
}

void LoadSettings() {
    FILE* f = fopen(SettingsPath().c_str(), "r");
    if (!f) f = fopen((bpp::FilesDir() + "/bpp_settings.txt").c_str(), "r");
    if (!f) return;
    char key[64], val[64];
    while (fscanf(f, "%63[^=]=%63s\n", key, val) == 2) {
        if (!strcmp(key, "speed")) g_settings.speed = (float)atof(val);
        else if (!strcmp(key, "tooltip")) g_settings.tooltip = atoi(val) != 0;
        else if (!strcmp(key, "scale")) g_settings.scale = (float)atof(val);
        else if (!strcmp(key, "floating_button")) g_settings.floating_button = atoi(val) != 0;
        else if (!strcmp(key, "launcher")) g_settings.launcher = !strcmp(val, "bubble") ? "bubble" : "side";
        else if (!strcmp(key,"companion_floating"))g_settings.companion_floating=atoi(val)!=0;
        else if (!strcmp(key,"companion_follow"))g_settings.companion_follow=atoi(val)!=0;
        else if (!strcmp(key,"companion_upload"))g_settings.companion_upload=atoi(val)!=0;
    }
    fclose(f);
    if (!std::isfinite(g_settings.speed) || g_settings.speed < 0 || g_settings.speed > 4) g_settings.speed = 1;
    LOGI("settings loaded: speed=%.2f tooltip=%d scale=%.2f button=%d", g_settings.speed,
         g_settings.tooltip, g_settings.scale, g_settings.floating_button);
}

void SaveSettings() {
    FILE* f = fopen((SettingsPath() + ".tmp").c_str(), "w");
    if (!f) return;
    fprintf(f, "speed=%.3f\ntooltip=%d\nscale=%.3f\nfloating_button=%d\nlauncher=%s\n", g_settings.speed,
            g_settings.tooltip ? 1 : 0, g_settings.scale, g_settings.floating_button ? 1 : 0, g_settings.launcher.c_str());
    fprintf(f,"companion_floating=%d\ncompanion_follow=%d\ncompanion_upload=%d\n",g_settings.companion_floating?1:0,g_settings.companion_follow?1:0,g_settings.companion_upload?1:0);
    fclose(f);
    rename((SettingsPath() + ".tmp").c_str(), SettingsPath().c_str());
}
int g_selected = -1;
float g_ui_scale = 1.0f;

// The UI reads game data through these 1 Hz caches: copying the card catalog
// every rendered frame would allocate megabytes per second.
const std::vector<game::CardInfo>& CardsCached() {
    static std::vector<game::CardInfo> cache;
    static uint64_t ts = 0;
    uint64_t now = bpp::NowMs();
    if (ts == 0 || now - ts > 1000) {
        cache = game::Catalog();
        ts = now;
    }
    return cache;
}

const std::vector<game::BuildMatch>& MatchesCached(const game::BoardSnapshot& snap) {
    static std::vector<game::BuildMatch> cache;
    static uint64_t ts = 0;
    uint64_t now = bpp::NowMs();
    if (ts == 0 || now - ts > 1000) {
        cache = game::MatchBuilds(snap, 5);
        ts = now;
    }
    return cache;
}

void DrawCollection() {
    // Deliberately does NOT start the catalog fetch: on the emulator GetCardMap()
    // from a worker thread faults inside SQLite-net. Build it only on request
    // (bpp_catalog marker) so the panel stays usable without it.
    ImGui::InputTextWithHint("##search", "搜索物品 / 技能", g_search, sizeof(g_search));
    ImGui::SameLine();
    if (ImGui::Button("清空")) g_search[0] = 0;
    ImGui::Separator();

    const auto& cards = CardsCached();
    if (cards.empty()) {
        ImGui::TextDisabled("图鉴数据尚未加载（需要进入对局后初始化）");
        return;
    }
    std::string q = g_search;
    ImGui::BeginChild("##list", ImVec2(0, 0), true);
    for (int i = 0; i < (int)cards.size(); ++i) {
        const auto& c = cards[i];
        if (!q.empty() && c.name.find(q) == std::string::npos &&
            c.hero.find(q) == std::string::npos && c.tags.find(q) == std::string::npos)
            continue;
        char label[256];
        snprintf(label, sizeof(label), "%s##%d", c.name.c_str(), i);
        if (ImGui::Selectable(label, g_selected == i)) g_selected = i;
        ImGui::SameLine();
        ImGui::TextDisabled("%s  T%d  S%d", c.hero.c_str(), c.tier, c.size);
    }
    ImGui::EndChild();
    if (g_selected >= 0 && g_selected < (int)cards.size()) {
        const auto& c = cards[g_selected];
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1, .85f, .4f, 1), "%s", c.name.c_str());
        ImGui::TextDisabled("%s   T%d   S%d", c.hero.c_str(), c.tier, c.size);
        if (!c.tags.empty()) ImGui::Text("标签: %s", c.tags.c_str());
        if (c.build_uses > 0)
            ImGui::TextColored(ImVec4(.5f, .9f, .6f, 1), "十胜阵容中出现 %d 次", c.build_uses);
        if (!c.description.empty()) ImGui::TextWrapped("%s", c.description.c_str());
    }
}

void DrawBoard() {
    game::BoardSnapshot snap = game::Snapshot();
    if (!snap.in_run) {
        ImGui::TextDisabled("未在对局中");
        return;
    }
    ImGui::Text("%s   Day %d   Hour %d%s", snap.hero.c_str(), snap.day, snap.hour,
                snap.in_combat ? "   [战斗中]" : "");
    ImGui::Separator();
    auto list = [](const char* title, const std::vector<game::CardInfo>& v) {
        ImGui::TextColored(ImVec4(.7f, .9f, 1, 1), "%s (%d)", title, (int)v.size());
        for (auto& c : v) ImGui::BulletText("%s", c.name.c_str());
    };
    list("棋盘", snap.board);
    list("仓库", snap.stash);
    list("对手", snap.opponent);
    list("商店/选择", snap.shop);
    list("技能", snap.skills);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1, .85f, .4f, 1), "推荐阵容（来自 BPP 十胜阵容库）");
    const auto& matches = MatchesCached(snap);
    if (matches.empty()) {
        ImGui::TextDisabled("暂不匹配（多上几个物品再看）");
    } else {
        for (auto& m : matches) {
            ImGui::PushID(&m);
            ImGui::Text("%s  匹配 %d 张  胜率 %.1f%%  十胜 %d 场", m.hero.c_str(), m.matched,
                        m.rate_bps / 100.0f, m.ten_win_runs);
            for (auto& g : m.card_guids) {
                const game::CardInfo* c = game::FindCard(g);
                ImGui::SameLine();
                ImGui::TextDisabled("%s", c ? c->name.c_str() : g.substr(0, 8).c_str());
            }
            ImGui::PopID();
        }
    }
    ImGui::Separator();
    ImGui::TextDisabled("%s", catalog::Title().c_str());
}

void DrawSettings() {
    ImGui::Text("战斗速度");
    const float speeds[] = {0.5f, 1.0f, 2.0f, 4.0f};
    for (float s : speeds) {
        char b[32];
        snprintf(b, sizeof(b), "%.2gx", s);
        bool active = game::Speed() == s;
        if (active) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(.2f, .6f, .3f, 1));
        if (ImGui::Button(b)) {
            game::SetSpeed(s);
            g_settings.speed = s;
            SaveSettings();
        }
        if (active) ImGui::PopStyleColor();
        ImGui::SameLine();
    }
    if (ImGui::Button("暂停")) {
        game::SetSpeed(0.0f);
        g_settings.speed = 0.0f;
        SaveSettings();
    }
    ImGui::NewLine();
    bool tip = game::TooltipEnabled();
    if (ImGui::Checkbox("物品提示增强", &tip)) {
        game::SetTooltipEnabled(tip);
        g_settings.tooltip = tip;
        SaveSettings();
    }
    bool btn = g_settings.floating_button;
    if (ImGui::Checkbox("显示右上角悬浮按钮", &btn)) {
        g_settings.floating_button = btn;
        SaveSettings();
    }
    if (ImGui::SliderFloat("界面缩放", &g_ui_scale, 0.6f, 2.0f)) {
        ui::SetUserScale(g_ui_scale);
        g_settings.scale = g_ui_scale;
        SaveSettings();
    }
    ImGui::Separator();
    ImGui::Text("BPP Mobile  ·  只读信息增强，不修改游戏数值");
    ImGui::Text("阵容库: %s", catalog::Title().c_str());
    ImGui::Separator();
    ImGui::Text("三指点击 = 打开/关闭面板");
}

void DrawDiagnostics() {
    auto d = game::GetDiagnostics();
    ImGui::Text("il2cpp domain: %s", d.il2cpp_ok ? "OK" : "NO");
    ImGui::Text("Data class: %s", d.data_resolved ? "OK" : "NO");
    ImGui::Text("图鉴: %s (%d)", d.catalog_ok ? "OK" : "未加载", d.catalog_size);
    ImGui::Text("棋盘: %d 张", d.board_size);
    ImGui::Separator();
    ImGui::Text("tick hook: %s", d.tick_hook ? "OK" : "NO");
    ImGui::Text("egl hook: %s", d.egl_hook ? "OK" : "NO");
    ImGui::Text("tooltip hook: %s", d.tooltip_hook ? "OK" : "NO");
    ImGui::Separator();
    ImGui::TextWrapped("日志: %s", d.log_path.empty() ? "(logcat only)" : d.log_path.c_str());
    if (!d.last_error.empty()) ImGui::TextColored(ImVec4(1, .4f, .4f, 1), "最后错误: %s",
                                                  d.last_error.c_str());
    if (ImGui::Button("导出数据")) {
        std::string p = bpp::MediaDir() + "/bpp_dump.txt";
        game::DumpToFile(p);
    }
    ImGui::SameLine();
    if (ImGui::Button("重新扫描")) game::Init();
}

void DrawTooltipCompanion() {
    if (!game::TooltipEnabled()) return;
    auto tip = game::LastTooltip();
    if (!tip.valid) return;
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x * 0.03f, io.DisplaySize.y * 0.60f),
                            ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x * 0.42f, 0), ImGuiCond_Always);
    ImGui::Begin("##tipcompanion", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextColored(ImVec4(1, .85f, .4f, 1), "%s", tip.card.name.c_str());
    ImGui::TextDisabled("%s  T%d  S%d", tip.card.hero.c_str(), tip.card.tier, tip.card.size);
    if (!tip.card.tags.empty()) ImGui::Text("标签: %s", tip.card.tags.c_str());
    if (tip.card.build_uses > 0)
        ImGui::TextColored(ImVec4(.5f, .9f, .6f, 1), "十胜阵容中出现 %d 次", tip.card.build_uses);
    if (!tip.card.description.empty()) ImGui::TextWrapped("%s", tip.card.description.c_str());
    ImGui::TextDisabled("— BPP 静态信息（升级/附魔数值预览开发中）");
    ImGui::End();
}

}  // namespace

void Boot() {
    LOGI("features boot (worker thread: no Unity API here)");
    LoadSettings();
    // Pure atomics only; Unity calls are deferred to InstallHooks on the main thread.
    ui::SetUserScale(g_settings.scale);
    game::SetTooltipEnabled(g_settings.tooltip);
    // Current recommendations are queried on demand by the ready WebView.
}


// Minimal JSON string quoting for embedded card data.
std::string JsonStr(const char* in) {
    std::string out = "\"";
    for (const char* p = in; p && *p; ++p) {
        unsigned char c = (unsigned char)*p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += (char)c;
        } else if (c < 0x20) {
            char esc[8];
            snprintf(esc, sizeof(esc), "\\u%04x", c);
            out += esc;
        } else {
            out += (char)c;
        }
    }
    out += "\"";
    return out;
}

// Full UI page with the whole card table embedded (no fetch, no file-origin issues).
std::string BuildUiHtml() {
    std::string h = kPanelHtml;
    auto replace = [&](const char* marker, const char* content) {
        size_t p = h.find(marker);
        if (p != std::string::npos) h.replace(p, strlen(marker), content);
    };
    replace("/*BPP_CSS*/", kPanelCss);
    replace("/*BPP_CARDS*/", kCardsJson);
    replace("/*BPP_FRAMES*/", kFramesJson);
    replace("/*BPP_THEME*/", kThemeJson);
    replace("/*BPP_JS*/", kPanelJs);
    return h;
}


void InstallHooks() {
    ResolveUnity();
    LOGI("unity resolved: Input=%p touchCount=%p GetTouch=%p Canvas=%p CanvasGroup=%p", K_Input,
         M_get_touchCount, M_GetTouch, K_Canvas, K_CanvasGroup);
    if (!game::Init()) {
        LOGW("game init deferred");
    }
    // Apply persisted settings here: the main thread is required for Unity APIs.
    if (g_settings.speed < 0.01f) g_settings.speed = 1.0f;
    game::SetSpeed(g_settings.speed);
    LOGI("settings applied on main thread: speed=%.2f tooltip=%d scale=%.2f", g_settings.speed,
         g_settings.tooltip, g_settings.scale);
    // Tooltip augmentation is opt-in: it runs inside EventSystem.Update and crashed
    // on some devices while the game rebuilds its tooltip objects.
    if (bpp::FileExists(bpp::MediaDir() + "/bpp_tooltip")) {
        if (!game::InstallTooltipHook()) LOGW("tooltip hook unavailable");
        else LOGI("tooltip hook enabled by marker");
    } else {
        LOGI("tooltip hook disabled (create bpp_tooltip to enable)");
    }
    // Opt-in Unity-UI probe: works on GLES and Vulkan alike because it only drives
    // an engine label. Enabled by the files/bpp_unityui marker.
    SetWebPanelMode(g_settings.launcher);
    if (!bpp::FileExists(bpp::MediaDir() + "/bpp_legacy_ui")) {
        std::string html_path = bpp::MediaDir() + "/bpp_ui.html";
        FILE* hf = fopen(html_path.c_str(), "w");
        if (hf) {
            std::string page = BuildUiHtml();
            fwrite(page.data(), 1, page.size(), hf);
            fclose(hf);
            bool ok = ShowWebPanel(html_path);
            LOGI("webui probe: %s (html %zu bytes)", ok ? "shown" : "failed", page.size());
        }
    }
    if (bpp::FileExists(bpp::MediaDir() + "/bpp_unityui")) {
        bool ok = unityui::Spawn("BPP 初始化中…");
        LOGI("unityui probe: %s", ok ? "spawned" : "failed");
    }
    g_game_ready.store(true);
}

std::atomic<int> g_tick_count{0};

std::string JoinNames(const std::vector<game::CardInfo>& v, int limit) {
    std::string out;
    int n = 0;
    for (auto& c : v) {
        if (n++ >= limit) { out += " …"; break; }
        if (!out.empty()) out += ", ";
        out += c.name;
    }
    return out.empty() ? "—" : out;
}

// Text HUD for devices where the GL overlay cannot draw (Vulkan): the same board
// data, rendered by the engine's own label.
void UpdateUnityUi() {
    if (!unityui::Active()) return;
    static uint64_t last = 0;
    uint64_t now = bpp::NowMs();
    if (now - last < 1500) return;
    last = now;
    game::BoardSnapshot s = game::Snapshot();
    char head[160];
    snprintf(head, sizeof(head), "BPP  Day %d  Hour %d  %s%s", s.day, s.hour,
             s.hero.empty() ? "-" : s.hero.c_str(), s.in_combat ? "  [战斗]" : "");
    std::string text = head;
    text += "\n棋盘: " + JoinNames(s.board, 8);
    text += "\n仓库: " + JoinNames(s.stash, 6);
    text += "\n对手: " + JoinNames(s.opponent, 8);
    if (!s.shop.empty()) text += "\n商店: " + JoinNames(s.shop, 4);
    if (!s.skills.empty()) text += "\n技能: " + JoinNames(s.skills, 4);
    char tail[96];
    snprintf(tail, sizeof(tail), "\n图鉴 %d 张 · 三指/文件控制", game::CatalogSize());
    text += tail;
    unityui::SetText(text);
}

// One-shot report ~6s after the first tick: tells us whether the Unity build is
// actually presenting through EGL (GLES) or not (very likely Vulkan), which
// decides whether a GL overlay can work at all.
// Late log attach: the first attempt happens during library load, possibly before
// the game has created Android/data/<pkg>/files.
// One-shot diagnostic: reproduce the game's database open from inside the game
// process, so we can tell an environment failure from a SQLite-level one.
// ---- open/openat interception (diagnostic only) ----
struct OpenCall {
    char path[192];
    int flags;
    int fd;
    int err;
    int at;
};
OpenCall g_open_calls[24];
std::atomic<int> g_open_count{0};
thread_local bool t_in_hook = false;

typedef int (*open_t)(const char*, int, ...);
typedef int (*openat_t)(int, const char*, int, ...);
open_t g_orig_open = nullptr;
openat_t g_orig_openat = nullptr;

void RecordOpen(const char* path, int flags, int fd, int at) {
    if (!path || !strstr(path, "GameData")) return;
    int i = g_open_count.load();
    if (i >= 24) return;
    g_open_count.store(i + 1);
    snprintf(g_open_calls[i].path, sizeof(g_open_calls[i].path), "%s", path);
    g_open_calls[i].flags = flags;
    g_open_calls[i].fd = fd;
    g_open_calls[i].err = errno;
    g_open_calls[i].at = at;
}

int HookOpen(const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (t_in_hook) return g_orig_open(path, flags, mode);
    t_in_hook = true;
    int fd = g_orig_open(path, flags, mode);
    int e = errno;
    RecordOpen(path, flags, fd, 0);
    errno = e;
    t_in_hook = false;
    return fd;
}

int HookOpenAt(int dirfd, const char* path, int flags, ...) {
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list ap;
        va_start(ap, flags);
        mode = va_arg(ap, mode_t);
        va_end(ap);
    }
    if (t_in_hook) return g_orig_openat(dirfd, path, flags, mode);
    t_in_hook = true;
    int fd = g_orig_openat(dirfd, path, flags, mode);
    int e = errno;
    RecordOpen(path, flags, fd, 1);
    errno = e;
    t_in_hook = false;
    return fd;
}

void InstallOpenHooks() {
    void* o = dlsym(RTLD_DEFAULT, "open");
    void* oa = dlsym(RTLD_DEFAULT, "openat");
    LOGI("open hook: open=%p openat=%p", o, oa);
    if (o) shadowhook_hook_func_addr(o, (void*)HookOpen, (void**)&g_orig_open);
    if (oa) shadowhook_hook_func_addr(oa, (void*)HookOpenAt, (void**)&g_orig_openat);
}

void FlushOpenCalls() {
    static int last = 0;
    int n = g_open_count.load();
    for (int i = last; i < n && i < 24; ++i) {
        LOGI("opencall[%d] at=%d flags=0x%x fd=%d errno=%d path=%s", i, g_open_calls[i].at,
             g_open_calls[i].flags, g_open_calls[i].fd, g_open_calls[i].err,
             g_open_calls[i].path);
    }
    last = n;
}
void DbProbe() {
    static bool done = false;
    if (done) return;
    done = true;
    if (!bpp::FileExists(bpp::MediaDir() + "/bpp_dbprobe")) return;  // opt-in diagnostic
    InstallOpenHooks();
    const std::string db_path=bpp::FilesDir()+"/prod/cache/GameData.db";
    const char* p=db_path.c_str();
    struct stat st;
    int r = stat(p, &st);
    LOGI("dbprobe stat=%d errno=%d size=%lld", r, errno, (long long)st.st_size);
    int fd = open(p, O_RDWR);
    LOGI("dbprobe open(O_RDWR)=%d errno=%d", fd, errno);
    if (fd >= 0) close(fd);
    fd = open(p, O_RDONLY);
    LOGI("dbprobe open(O_RDONLY)=%d errno=%d", fd, errno);
    if (fd >= 0) close(fd);
    fd = open(p, O_RDWR);
    if (fd >= 0) {
        struct stat fs;
        int fr = fstat(fd, &fs);
        LOGI("dbprobe fstat=%d errno=%d mode=0%o size=%lld", fr, errno, fs.st_mode,
             (long long)fs.st_size);
        struct flock fl;
        memset(&fl, 0, sizeof(fl));
        fl.l_type = F_WRLCK;
        fl.l_whence = SEEK_SET;
        int lr = fcntl(fd, F_SETLK, &fl);
        LOGI("dbprobe lock=%d errno=%d", lr, errno);
        fl.l_type = F_UNLCK;
        fcntl(fd, F_SETLK, &fl);
        close(fd);
    }
    void* h = dlopen("libgilzoide-sqlite-net.so", RTLD_NOW);
    LOGI("dbprobe dlopen=%p errno=%d", h, errno);
    if (!h) return;
    typedef const char* (*libversion_t)();
    libversion_t libver = (libversion_t)dlsym(h, "sqlite3_libversion");
    LOGI("dbprobe sqlite libversion=%s", libver ? libver() : "?");
    typedef int (*init_t)();
    init_t init = (init_t)dlsym(h, "sqlite3_initialize");
    LOGI("dbprobe sqlite3_initialize=%d", init ? init() : -999);
    typedef void* (*vfs_find_t)(const char*);
    vfs_find_t vfs_find = (vfs_find_t)dlsym(h, "sqlite3_vfs_find");
    if (vfs_find) {
        LOGI("dbprobe vfs default=%p unix=%p", vfs_find(nullptr), vfs_find("unix"));
    }
    typedef int (*open_v2_t)(const char*, void**, int, const char*);
    open_v2_t open_v2 = (open_v2_t)dlsym(h, "sqlite3_open_v2");
    LOGI("dbprobe sqlite3_open_v2=%p", open_v2);
    if (!open_v2) return;
    void* db = nullptr;
    int rc = open_v2(p, &db, 0x00000001, nullptr);
    LOGI("dbprobe open_v2 READONLY rc=%d", rc);
    db = nullptr;
    rc = open_v2(p, &db, 0x00000002, nullptr);
    LOGI("dbprobe open_v2 READWRITE rc=%d", rc);
    db = nullptr;
    rc = open_v2(":memory:", &db, 0x00000002 | 0x00000004, nullptr);
    LOGI("dbprobe :memory: rc=%d", rc);
    db = nullptr;
    rc = open_v2(p, &db, 0x00000002 | 0x00000004, "unix");
    LOGI("dbprobe open_v2 RW|CREATE vfs=unix rc=%d", rc);
    db = nullptr;
    struct stat ds;
    const std::string legacy_internal="/data/data/"+bpp::ReadPackageName();
    const std::string internal="/data/user/"+std::to_string(getuid()/100000)+"/"+bpp::ReadPackageName();
    LOGI("dbprobe internal dirs: /data/data=%d errno=%d  files=%d  current_user=%d",
         stat(legacy_internal.c_str(), &ds), errno,
         stat((legacy_internal+"/files").c_str(), &ds),
         stat((internal+"/files").c_str(), &ds));
    db = nullptr;
    rc = open_v2((legacy_internal+"/files/bpp_probe.db").c_str(), &db,
                 0x00000002 | 0x00000004, nullptr);
    LOGI("dbprobe internal /data/data rc=%d errno=%d", rc, errno);
    db = nullptr;
    rc = open_v2((internal+"/files/bpp_probe.db").c_str(), &db,
                 0x00000002 | 0x00000004, nullptr);
    LOGI("dbprobe internal current_user rc=%d errno=%d", rc, errno);
    typedef const char* (*errmsg_t)(void*);
    errmsg_t errmsg = (errmsg_t)dlsym(h, "sqlite3_errmsg");
    if (errmsg && db) LOGI("dbprobe errmsg=%s", errmsg(db));
}

void Maintenance() {
    static uint64_t last = 0;
    static bool attached = false;
    uint64_t now = bpp::NowMs();
    if (now - last < 1000) return;
    last = now;
    DbProbe();
    FlushOpenCalls();
    if (!attached && bpp::LogOpenRetry()) {
        attached = true;
        LOGI("late log attach -> %s", bpp::LogPath());
        bpp::CrashGuardAttachLogFd();
    }
}

void ReportGraphics() {
    static uint64_t t0 = 0;
    static bool reported = false;
    if (reported) return;
    uint64_t now = bpp::NowMs();
    if (t0 == 0) t0 = now;
    if (now - t0 < 6000) return;
    reported = true;
    int swaps = ui::SwapCount();
    bool vk = bpp::VulkanLoaded();
    LOGI("gfx: ticks=%d egl_swaps=%d vulkan_loaded=%d", g_tick_count.load(), swaps, vk ? 1 : 0);
    if (swaps == 0) {
        LOGW("no eglSwapBuffers observed within 6s: the game most likely renders with Vulkan, "
             "so the GL overlay cannot draw. Data channel (bpp_open/bpp_dump) still works.");
    }
}

// File command channel, so the mod stays drivable even if rendering or touch breaks:
//   adb shell touch .../files/bpp_open | bpp_close | bpp_dump
bool ParseWatch(const std::string& value,std::set<std::string>& out){
    size_t i=0;auto space=[&]{while(i<value.size()&&isspace((unsigned char)value[i]))++i;};
    space();if(i==value.size()||value[i++]!='[')return false;space();
    if(i<value.size()&&value[i]==']'){++i;space();return i==value.size();}
    for(int n=0;n<80;++n){
        space();if(i==value.size()||value[i++]!='"')return false;
        size_t start=i;while(i<value.size()&&value[i]!='"')++i;
        if(i==value.size())return false;
        std::string id=value.substr(start,i++-start);
        if(id.size()!=36||id.find_first_not_of("0123456789abcdef-")!=std::string::npos)return false;
        for(int p:{8,13,18,23})if(id[p]!='-')return false;
        out.insert(id);space();if(i==value.size())return false;
        if(value[i]==']'){++i;space();return i==value.size();}
        if(value[i++]!=',')return false;
    }
    return false;
}

void HandleCommand(const std::string& action, const std::string& value) {
    bool accepted = true;
    if (action == "open" || action == "close") {
        bool open = action == "open";
        if (WebPanelCreated()) SetWebPanelVisible(open);
        else ui::SetOpen(open);
    } else if (action == "speed") {
        if (value != "0" && value != "1" && value != "2" && value != "3") accepted = false;
        else { g_settings.speed = (float)atof(value.c_str()); game::SetSpeed(g_settings.speed); SaveSettings(); }
    } else if(action=="speed_check"){
        g_last_command="speed_check "+game::VerifySpeedRecovery();LOGI("%s",g_last_command.c_str());g_force_push=true;return;
    } else if(action=="ui_check"){
        PushJs("window.reportLayout();");
    } else if (action == "launcher") {
        if (value != "side" && value != "bubble") accepted = false;
        else { g_settings.launcher = value; SetWebPanelMode(value); SaveSettings(); }
    } else if (action == "keyboard") {
        HideWebKeyboard();
    } else if (action == "runs") {
        QueueQiuRuns(value);
    } else if(action=="detail"){
        QueueQiuDetail(value);
    } else if(action=="watch"){
        std::set<std::string> ids;accepted=ParseWatch(value,ids);if(accepted)g_watched=std::move(ids);
    } else if(action=="preference"){
        if(value=="floating=1"||value=="floating=0"){
            g_settings.companion_floating=value.back()=='1';if(!g_settings.companion_floating&&WebMiniVisible())SetWebPanelVisible(false);
        }else if(value=="follow=1"||value=="follow=0")g_settings.companion_follow=value.back()=='1';
        else if(value=="upload=1"||value=="upload=0")g_settings.companion_upload=value.back()=='1';
        else accepted=false;
        if(accepted)SaveSettings();
    } else if(action=="float"){
        float w,h,x,y;accepted=g_settings.companion_floating&&sscanf(value.c_str(),"%f,%f,%f,%f",&w,&h,&x,&y)==4&&std::isfinite(w)&&std::isfinite(h)&&std::isfinite(x)&&std::isfinite(y);
        if(accepted)SetWebMini(true,w,h,x,y);
    } else if(action=="float_move"){
        float x,y;accepted=sscanf(value.c_str(),"%f,%f",&x,&y)==2&&std::isfinite(x)&&std::isfinite(y);
        if(accepted)MoveWebMini(x,y);
    } else if(action=="float_drag"){
        accepted=value=="0"||value=="1";if(accepted)SetWebMiniDragging(value=="1");
    } else if(action=="float_tap"){
        PushJs("window.toggleFloatingSkills();");
    } else if(action=="float_commit"){
        float x,y;accepted=sscanf(value.c_str(),"%f,%f",&x,&y)==2&&std::isfinite(x)&&std::isfinite(y);
        if(accepted)PushJs("window.setFloatingPosition("+std::to_string(std::clamp(x,0.f,10000.f))+","+std::to_string(std::clamp(y,0.f,10000.f))+");");
    } else if(action=="dock"){
        SetWebPanelVisible(false);
    } else if(action=="capture_done"){
        bool error=value.rfind("error:",0)==0;
        accepted=!g_pending_capture.empty()&&(error||value==g_pending_capture.substr(g_pending_capture.find_last_of('/')+1));
        if(accepted){
            bool valid=!error&&CapturePngValid(g_pending_capture);
            if(g_capture_tenwin){
                auto snap=game::Snapshot();ObserveCaptureRun(snap);
                if(!CaptureMatches(snap)){
                    g_tenwin.finished=true;remove(g_pending_capture.c_str());
                    g_capture_status="十胜记录未完成：对局或画面已变化，已停止补拍；社区上传尚未接通。";
                    valid=false;
                }else if(valid){
                    // Commit deduplication only after this request produced a complete PNG.
                    g_tenwin_captured=true;g_tenwin.finished=true;
                    std::string stamp=bpp::MediaDir()+"/bpp_tenwin.last",tmp=stamp+".tmp";
                    bool saved=false;
                    if(FILE* f=fopen(tmp.c_str(),"w")){
                        bool written=fwrite(g_tenwin.fingerprint.data(),1,g_tenwin.fingerprint.size(),f)==g_tenwin.fingerprint.size();
                        bool closed=fclose(f)==0;saved=written&&closed&&rename(tmp.c_str(),stamp.c_str())==0;
                    }
                    if(!saved)remove(tmp.c_str());
                    g_capture_status=saved?"十胜截图和阵容已保存到本机；社区上传尚未接通。":
                        "十胜截图和阵容已保存，但去重标记写入失败；社区上传尚未接通。";
                }else{remove(g_pending_capture.c_str());CaptureFailed();}
            }else g_capture_status=valid?"游戏截图测试通过；社区上传尚未接通。":"游戏截图测试失败；社区上传尚未接通。";
            accepted=valid;g_pending_capture.clear();g_capture_tenwin=false;
        }
    } else if(action=="capture"){
        accepted=g_pending_capture.empty();
        if(accepted){
            StartCapture(bpp::MediaDir()+"/bazaarBot-check-"+std::to_string((long long)time(nullptr))+"-"+std::to_string(bpp::NowMs())+".png",false);
            g_capture_status="正在测试游戏截图；社区上传尚未接通。";
        }
    } else if (action == "dump") {
        accepted = game::DumpToFile(bpp::MediaDir() + "/bpp_dump.txt");
    } else if (action != "status") accepted = false;
    if(action=="float_move"||action=="float_drag")return;
    g_last_command = action + (value.empty() || action == "runs" || action=="watch" ? "" : "=" + value) + (accepted ? " OK" : " FAILED");
    LOGI("command: %s", g_last_command.c_str());
    g_force_push = true;
}

std::string StateJson() {
    auto s = game::Snapshot();
    auto d = game::GetDiagnostics();
    std::string network;
    network = QiuNetworkStatus();
    std::string json = "{\"version\":\"0.0.1\",\"updated_at\":" + std::to_string((long long)time(nullptr)) +
        ",\"package\":" + JsonStr(bpp::ReadPackageName().c_str()) +
        ",\"process_id\":" + std::to_string(getpid()) +
        ",\"in_run\":" + (s.in_run ? "true" : "false") + ",\"hero\":" + JsonStr(s.hero.c_str()) +
        ",\"day\":" + std::to_string(s.day) + ",\"hour\":" + std::to_string(s.hour) +
        ",\"in_combat\":" + (s.in_combat ? "true" : "false") + ",\"speed\":" + std::to_string(game::Speed()) +
        ",\"time_scale\":" + std::to_string(game::ActualSpeed()) +
        ",\"victories\":"+std::to_string(s.victories)+",\"losses\":"+std::to_string(s.losses)+
        ",\"preferences\":{\"floating\":"+(g_settings.companion_floating?"true":"false")+",\"follow\":"+(g_settings.companion_follow?"true":"false")+",\"upload\":"+(g_settings.companion_upload?"true":"false")+"}"+
        ",\"mini\":"+(WebMiniVisible()?"true":"false")+",\"capture_status\":"+JsonStr(g_capture_status.c_str())+
        ",\"watch_count\":"+std::to_string(g_watched.size())+
        ",\"watch_hits\":"+std::to_string(g_watch_hits)+
        ",\"marker_status\":"+JsonStr(game::ScreenCardStatus().c_str())+
        ",\"mode\":" + JsonStr(g_settings.launcher.c_str()) + ",\"open\":" + (WebPanelVisible() ? "true" : "false") +
        ",\"last_command\":" + JsonStr(g_last_command.c_str()) + ",\"network\":" + JsonStr(network.c_str());
    auto list = [&](const char* kind, const std::vector<game::CardInfo>& cards) {
        json += "," + JsonStr(kind) + ":[";
        for (size_t i = 0; i < cards.size(); ++i) {
            const auto& c = cards[i];
            if (i) json += ",";
            json += "{\"g\":" + JsonStr(c.guid.c_str()) + ",\"n\":" + JsonStr(c.name.c_str()) +
                ",\"h\":" + JsonStr(c.hero.c_str()) + ",\"tags\":" + JsonStr(c.tags.c_str()) +
                ",\"tier\":" + std::to_string(c.tier) + ",\"size\":" + std::to_string(c.size) +
                ",\"slot\":"+std::to_string(c.slot)+",\"enchantment\":"+std::to_string(c.enchantment)+"}";
        }
        json += "]";
    };
    list("board", s.board); list("stash", s.stash); list("skills", s.skills);
    list("opponent", s.opponent); list("opponent_skills",s.opponent_skills);list("shop", s.shop);
    json+=",\"markers\":[";
    for(size_t i=0;i<g_screen_cards.size();++i){if(i)json+=',';const auto& p=g_screen_cards[i];json+="{\"g\":"+JsonStr(p.guid.c_str())+",\"x\":"+std::to_string(p.x)+",\"y\":"+std::to_string(p.y)+",\"width\":"+std::to_string(p.width)+",\"height\":"+std::to_string(p.height)+"}";}
    json+=']';
    json += std::string(",\"diagnostics\":{\"il2cpp\":") + (d.il2cpp_ok ? "true" : "false") +
        ",\"data\":" + (d.data_resolved ? "true" : "false") + ",\"tick\":" + (d.tick_hook ? "true" : "false") +
        ",\"swaps\":" + std::to_string(ui::SwapCount()) + ",\"vulkan\":" + (bpp::VulkanLoaded() ? "true" : "false") +
        ",\"egl\":" + (d.egl_hook ? "true" : "false") + ",\"tooltip\":" + (d.tooltip_hook ? "true" : "false") +
        ",\"log\":" + JsonStr(d.log_path.c_str()) + ",\"error\":" + JsonStr(d.last_error.c_str()) + "}}";
    return json;
}

void UpdateCompanion(){
    static uint64_t last=0;auto now=bpp::NowMs();if(now-last<200)return;last=now;
    g_screen_cards=g_settings.companion_follow?game::ScreenCards(g_watched):std::vector<game::ScreenCard>();
    std::vector<WebMarker> markers;for(const auto& p:g_screen_cards)markers.push_back({p.x,p.y,p.guid,p.width,p.height});
    auto snap=game::Snapshot();std::set<std::string> found;std::string summary;
    auto match=[&](const std::vector<game::CardInfo>& cards){for(const auto& c:cards){
        if(!g_settings.companion_follow||!g_watched.count(c.guid)||!found.insert(c.guid).second)continue;
        if(found.size()<=3){if(!summary.empty())summary+="、";summary+=c.name;}
    }};
    match(snap.shop);if(snap.in_combat){match(snap.opponent);match(snap.opponent_skills);}
    g_watch_hits=(int)found.size();if(!summary.empty())summary="关注 · "+summary+(found.size()>3?" 等":"");
    static std::string last_markers;
    std::string marker_key;for(const auto& p:markers)marker_key+=p.guid+":"+std::to_string(p.x)+","+std::to_string(p.y)+","+std::to_string(p.width)+","+std::to_string(p.height)+";";
    marker_key+=summary;
    if(marker_key!=last_markers){last_markers=marker_key;UpdateWebMarkers(markers,summary);}
    ObserveCaptureRun(snap);
    if(!g_pending_capture.empty()){
        // Java errors have no request ID. Keep the single-flight slot until its callback,
        // even after a timeout, so a late error can never complete another request.
        if(!g_capture_timeout&&now-g_capture_started>=15000){
            g_capture_timeout=true;
            g_capture_status="截图回调超时，等待当前请求结束；重启前不会再发截图请求。社区上传尚未接通。";
            g_force_push=true;
        }
        return;
    }
    if(g_tenwin.run!=g_capture_run)g_tenwin=TenwinCapture{};
    if(!g_settings.companion_upload||g_tenwin_captured||!snap.in_run||snap.victories<10||snap.in_combat||snap.board.empty())return;
    if(g_tenwin.fingerprint.empty()){
        std::string fingerprint=CaptureFingerprint(snap);
        std::string stamp=bpp::MediaDir()+"/bpp_tenwin.last";
        if(FILE* f=fopen(stamp.c_str(),"r")){
            char old[8192]={};fread(old,1,sizeof(old)-1,f);fclose(f);
            if(fingerprint==old){g_tenwin_captured=true;return;}
        }
        std::string name="bazaarBot-tenwin-"+std::to_string((long long)time(nullptr))+"-"+std::to_string(now)+"-"+snap.hero;
        g_tenwin.fingerprint=fingerprint;g_tenwin.run=g_capture_run;
        g_tenwin.path=bpp::MediaDir()+"/"+name;
        g_tenwin.json="{\"schema\":\"bazaarbot-mobile-local-v1\",\"community_upload\":\"not_connected\",\"screenshot_file\":"+JsonStr((name+".png").c_str())+",\"snapshot\":"+StateJson()+"}";
    }
    if(g_tenwin.finished||now<g_tenwin.retry_at||!CaptureMatches(snap))return;
    ++g_tenwin.attempts;
    if(!g_tenwin.json_ready){
        std::string path=g_tenwin.path+".json",tmp=path+".tmp";
        if(FILE* out=fopen(tmp.c_str(),"w")){
            bool written=fwrite(g_tenwin.json.data(),1,g_tenwin.json.size(),out)==g_tenwin.json.size();
            bool closed=fclose(out)==0;g_tenwin.json_ready=written&&closed&&rename(tmp.c_str(),path.c_str())==0;
        }
        if(!g_tenwin.json_ready){remove(tmp.c_str());CaptureFailed();return;}
    }
    g_capture_status="十胜阵容已保存，正在截取游戏画面；社区上传尚未接通。";
    StartCapture(g_tenwin.path+".png",true);g_force_push=true;
}

void PushWebState() {
    static uint64_t last = 0;
    uint64_t now = bpp::NowMs();
    if (!g_force_push && now - last < 1000) return;
    g_force_push = false;
    last = now;
    std::string json = StateJson();
    std::string path = bpp::MediaDir() + "/bpp_state.json";
    FILE* out = fopen((path + ".tmp").c_str(), "w");
    if (out) {
        size_t written = fwrite(json.data(), 1, json.size(), out);
        bool closed = fclose(out) == 0;
        if (written == json.size() && closed) rename((path + ".tmp").c_str(), path.c_str());
    }
    PushJs("window.setState(" + json + ");");

}

void CheckCommands() {
    static uint64_t last = 0;
    uint64_t now = bpp::NowMs();
    if (now - last < 1000) return;
    last = now;
    for (const std::string& dir : {bpp::FilesDir(), bpp::MediaDir()}) {
    if (dir.empty()) continue;
    auto consume = [](const std::string& p) { remove(p.c_str()); };

    std::string open = dir + "/bpp_open";
    if (bpp::FileExists(open)) { HandleCommand("open", ""); consume(open); }

    std::string close = dir + "/bpp_close";
    if (bpp::FileExists(close)) { HandleCommand("close", ""); consume(close); }

    for (const char* command : {"speed", "launcher", "status", "keyboard", "capture", "speed_check", "ui_check"}) {
        std::string path = dir + "/bpp_" + command;
        if (!bpp::FileExists(path)) continue;
        char value[64] = {0};
        FILE* f = fopen(path.c_str(), "r");
        if (f) { fscanf(f, "%63s", value); fclose(f); }
        consume(path);
        HandleCommand(command, value);
    }

    std::string cat = dir + "/bpp_catalog";
    if (bpp::FileExists(cat)) {
        consume(cat);
        LOGI("cmd: catalog -> probe runtime card");
        game::ProbeCards();
    }

    std::string tab = dir + "/bpp_tab";
    if (bpp::FileExists(tab)) {
        FILE* tf = fopen(tab.c_str(), "r");
        if (tf) {
            int n = 0;
            if (fscanf(tf, "%d", &n) == 1) g_force_tab = n;
            fclose(tf);
        }
        consume(tab);
        LOGI("cmd: tab -> %d", g_force_tab);
    }

    std::string dump = dir + "/bpp_dump";
    if (bpp::FileExists(dump)) {
        HandleCommand("dump", "");
        consume(dump);
    }
    }
}

void Tick() {
    ResolveUnity();
    game::Tick();
    game::SetDiagFlags(true, ui::Hooked());
    CheckCommands();
    // Auto-open the panel once shortly after boot so a floating window is visible
    // without adb; the user can close it with the X or a three-finger tap.
    {
        // The panel no longer auto-opens: a floating BPP button in the corner opens it.
    }
    for (const auto& command : DrainWebCommands()) HandleCommand(command.action, command.value);
    UpdateCompanion();
    PushWebState();
    ReportGraphics();
    Maintenance();
    LogInputDiagnostics();
    UpdateUnityUi();
    g_tick_count.fetch_add(1);
    UpdateTouch();
    if (!g_game_ready.load()) return;
    if (ui::IsOpen() && !g_blocked.exchange(true)) BlockGameUi(true);
    if (!ui::IsOpen() && g_blocked.exchange(false)) BlockGameUi(false);
}

bool WantsOverlay() { return false; }

void DrawOverlay(bool open) {
    if (WebPanelCreated()) return;
    if (!open) {
        // The launcher button is always available when the panel is closed.
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 132, 56), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(112, 62), ImGuiCond_Always);
        ImGui::Begin("##bppbtn", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
                         ImGuiWindowFlags_NoMove);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.62f, 0.18f, 0.72f, 0.92f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.75f, 0.28f, 0.85f, 1.0f));
        if (ImGui::Button("BPP", ImVec2(100, 50))) ui::SetOpen(true);
        ImGui::PopStyleColor(2);
        ImGui::End();
        DrawTooltipCompanion();
        return;
    }
    ImGui::SetNextWindowSize(ImVec2(ImGui::GetIO().DisplaySize.x * 0.9f,
                                    ImGui::GetIO().DisplaySize.y * 0.75f),
                             ImGuiCond_Always);
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x * 0.05f,
                                   ImGui::GetIO().DisplaySize.y * 0.12f),
                            ImGuiCond_Always);
    ImGui::Begin("大巴扎加加 Mobile", &open, ImGuiWindowFlags_NoCollapse);
    if (!open) ui::SetOpen(false);
    if (ImGui::BeginTabBar("##tabs")) {
        const ImGuiTabItemFlags sel = ImGuiTabItemFlags_SetSelected;
        if (ImGui::BeginTabItem("图鉴", nullptr, g_force_tab == 0 ? sel : 0)) { DrawCollection(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("阵容", nullptr, g_force_tab == 1 ? sel : 0)) { DrawBoard(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("设置", nullptr, g_force_tab == 2 ? sel : 0)) { DrawSettings(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("诊断", nullptr, g_force_tab == 3 ? sel : 0)) { DrawDiagnostics(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
        g_force_tab = -1;
    }
    ImGui::End();
    DrawTooltipCompanion();
}

}  // namespace features
