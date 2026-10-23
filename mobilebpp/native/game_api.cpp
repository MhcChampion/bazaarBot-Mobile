// Game-specific access built on top of the IL2CPP reflection layer.
// Type/field/RVA facts come from work/il2cpp-dump/dump.cs (Unity 6000.3.11f1, metadata v39).
#include "game_api.h"
#include "il2cpp_api.h"
#include "catalog.h"
#include "offsets.h"
#include "card_table.h"
#include <thread>
#include <functional>
#include "crash_guard.h"

#include <unordered_map>
#include <sys/stat.h>
#include <cmath>

namespace game {

std::string LocalizeText(const std::string&, const std::string&);
namespace {

std::mutex g_mu;
std::vector<CardInfo> g_catalog;
std::unordered_map<std::string, int> g_by_guid;
BoardSnapshot g_snapshot;
std::string g_projection_status="未选择关注阵容";
bool g_inited = false;
bool g_catalog_done = false;
float g_speed = 1.0f;
uint64_t g_last_scan = 0;

// ---- handles ----
void* K_Data = nullptr;
void* K_JsonGameDataManager = nullptr;
void* K_Object = nullptr;
void* M_IsManagerCreated = nullptr;
void* M_GetStatic = nullptr;
void* M_GetCardMap = nullptr;
void* M_FindObjectsOfType = nullptr;
void* M_set_timeScale = nullptr;

const char* kHeroNames[] = {"Common", "Pygmalien", "Vanessa", "Stelle", "Jules",
                            "Dooley", "Mak",      "Karnok",  "TheDragons"};

void* MethodInHierarchy(void* klass, const char* name, int argc) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* m = il2::A.class_get_method_from_name(k, name, argc);
        if (m) return m;
    }
    return nullptr;
}

void* FieldInHierarchy(void* klass, const char* name) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* f = il2::A.class_get_field_from_name(k, name);
        if (f) return f;
    }
    return nullptr;
}

// il2cpp_runtime_invoke boxes value types; payload starts after the 0x10 object header.
int32_t BoxedI32(void* r) { return r ? *(int32_t*)((char*)r + il2::ObjectHeaderSize()) : 0; }

std::string GuidString(const uint8_t* g) {
    char buf[40];
    snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             *(const uint32_t*)(g + 0), *(const uint16_t*)(g + 4), *(const uint16_t*)(g + 6),
             g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
    return buf;
}

std::string InvokeString(void* method, void* obj) {
    return il2::Str(il2::Invoke0(method, obj));
}

std::string CardTitle(void* itcard, void* klass) {
    // Localization.Title.Text, falling back to InternalName.
    void* getLoc = MethodInHierarchy(klass, "get_Localization", 0);
    if (getLoc) {
        void* loc = il2::Invoke0(getLoc, itcard);
        if (loc) {
            void* lk = il2::A.object_get_class(loc);
            void* getTitle = MethodInHierarchy(lk, "get_Title", 0);
            if (getTitle) {
                void* title = il2::Invoke0(getTitle, loc);
                if (title) {
                    void* tk = il2::A.object_get_class(title);
                    void* getText = MethodInHierarchy(tk, "get_Text", 0);
                    if (getText) {
                        std::string s = InvokeString(getText, title);
                        if (!s.empty()) return s;
                    }
                }
            }
        }
    }
    void* getName = MethodInHierarchy(klass, "get_InternalName", 0);
    if (getName) return InvokeString(getName, itcard);
    return {};
}

void* K_ECardTag = nullptr;

std::string EnumName(void* klass, int value) {
    if (!klass) return std::to_string(value);
    static std::mutex m;
    static std::unordered_map<long long, std::string> cache;
    long long key = ((long long)(uintptr_t)klass << 32) ^ (uint32_t)value;
    {
        std::lock_guard<std::mutex> lk(m);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
    }
    std::string out;
    void* iter = nullptr;
    while (const void* cf = il2::A.class_get_fields(klass, &iter)) {
        void* f = (void*)cf;
        const char* n = il2::A.field_get_name(f);
        if (!n || strstr(n, "value__")) continue;
        int v = 0;
        il2::A.field_static_get_value(f, &v);
        if (v == value) {
            out = n;
            break;
        }
    }
    if (out.empty()) out = std::to_string(value);
    {
        std::lock_guard<std::mutex> lk(m);
        cache[key] = out;
    }
    return out;
}

std::string ReadTags(void* klass, void* val) {
    if (!K_ECardTag) K_ECardTag = il2::KlassAny("", "ECardTag");
    void* getTags = MethodInHierarchy(klass, "get_Tags", 0);
    if (!getTags) return {};
    void* set = il2::Invoke0(getTags, val);
    if (!set) return {};
    std::string out;
    const uint64_t exc0 = il2::ExceptionCount();
    il2::Enumerate(set, [&](void* item) {
        int ev = BoxedI32(item);
        if (!out.empty()) out += "/";
        out += EnumName(K_ECardTag, ev);
    });
    if (il2::ExceptionCount() != exc0) return {};
    return out;
}

// Fetching the card map is expensive (SQLite + JSON for the whole table) and must
// never run on the Unity main thread: it blocks the loading screen and gets the
// process killed. The worker below performs it; the main thread only enumerates.
bool IsInstanceOf(void* obj, void* klass) {
    if (!obj || !klass) return false;
    for (void* k = il2::A.object_get_class(obj); k; k = il2::A.class_get_parent(k)) {
        if (k == klass) return true;
    }
    return false;
}

void* PropInHierarchy(void* klass, const char* name) {
    for (void* k = klass; k; k = il2::A.class_get_parent(k)) {
        void* pr = il2::A.class_get_property_from_name(k, name);
        if (pr) {
            void* g = il2::A.property_get_get_method(pr);
            if (g) return g;
        }
    }
    return nullptr;
}

void* FetchCardMap() {
    if (!M_GetCardMap || !M_GetStatic) return nullptr;
    if (M_IsManagerCreated && !BoxedI32(il2::InvokeStatic0(M_IsManagerCreated))) return nullptr;

    void* raw = il2::InvokeStatic0(M_GetStatic);
    if (raw) {
        LOGI("GetStatic -> %p class=%s", raw,
             il2::ClassName(il2::A.object_get_class(raw)).c_str());
    }
    void* mgr = IsInstanceOf(raw, K_JsonGameDataManager) ? raw : nullptr;
    if (!mgr && raw) {
        void* mk = il2::A.object_get_class(raw);
        void* get_completed = PropInHierarchy(mk, "IsCompleted");
        const bool completed =
            get_completed ? BoxedI32(il2::Invoke0(get_completed, raw)) != 0 : false;
        void* get_result = PropInHierarchy(mk, "Result");
        if (completed && get_result) {
            void* inner = il2::Invoke0(get_result, raw);
            if (IsInstanceOf(inner, K_JsonGameDataManager)) {
                LOGI("GetStatic task -> .Result=%p", inner);
                mgr = inner;
            }
        }
    }
    if (!mgr) {
        void* f = FieldInHierarchy(K_Data, "manager");
        if (f) {
            void* out = nullptr;
            il2::A.field_static_get_value(f, &out);
            if (IsInstanceOf(out, K_JsonGameDataManager)) {
                LOGI("static manager field -> %p", out);
                mgr = out;
            }
        }
    }
    if (!mgr) {
        LOGW("no JsonGameDataManager (GetStatic class=%s)",
             raw ? il2::ClassName(il2::A.object_get_class(raw)).c_str() : "(null)");
        return nullptr;
    }
    return il2::Invoke0(M_GetCardMap, mgr);
}

void BuildCatalogFromDict(void* dict) {
    if (!dict) return;

    std::vector<CardInfo> out;
    out.reserve(600);
    const uint64_t exc_before = il2::ExceptionCount();
    il2::Enumerate(dict, [&](void* kv) {
        void* kvk = il2::A.object_get_class(kv);
        void* vf = FieldInHierarchy(kvk, "value");
        if (!vf) return;
        void* val = il2::FieldPtr(kv, vf);
        if (!val) return;
        void* klass = il2::A.object_get_class(val);

        CardInfo ci;
        void* getId = MethodInHierarchy(klass, "get_Id", 0);
        if (getId) {
            void* guid = il2::Invoke0(getId, val);
            if (guid) ci.guid = GuidString((const uint8_t*)((char*)guid + il2::ObjectHeaderSize()));
        }
        ci.name = CardTitle(val, klass);
        if (ci.name.empty()) ci.name = "(未命名)";
        void* getDesc = MethodInHierarchy(klass, "get_InternalDescription", 0);
        if (getDesc) ci.description = InvokeString(getDesc, val);
        void* getType = MethodInHierarchy(klass, "get_Type", 0);
        if (getType) ci.kind = std::to_string(BoxedI32(il2::Invoke0(getType, val)));
        void* getSize = MethodInHierarchy(klass, "get_Size", 0);
        if (getSize) ci.size = BoxedI32(il2::Invoke0(getSize, val));
        void* getTier = MethodInHierarchy(klass, "get_StartingTier", 0);
        if (getTier) ci.tier = BoxedI32(il2::Invoke0(getTier, val));
        void* getHeroes = MethodInHierarchy(klass, "get_Heroes", 0);
        if (getHeroes) {
            void* set = il2::Invoke0(getHeroes, val);
            std::string heroes;
            il2::Enumerate(set, [&](void* item) {
                int ev = BoxedI32(item);
                if (ev >= 0 && ev <= 8) {
                    if (!heroes.empty()) heroes += "/";
                    heroes += kHeroNames[ev];
                }
            });
            ci.hero = heroes;
        }
        ci.tags = ReadTags(klass, val);
        ci.build_uses = catalog::UsageCount(ci.guid);
        ci.valid = true;
        out.push_back(std::move(ci));
    });

    if (il2::ExceptionCount() != exc_before) {
        LOGW("catalog enumeration aborted by a managed exception; will retry");
        return;
    }
    LOGI("catalog built: %zu cards", out.size());
    for (size_t i = 0; i < out.size() && i < 5; ++i)
        LOGI("  sample card[%zu] %s | %s | T%d S%d | builds=%d", i, out[i].name.c_str(),
             out[i].hero.c_str(), out[i].tier, out[i].size, out[i].build_uses);
    {
        std::lock_guard<std::mutex> lk(g_mu);
        g_catalog = std::move(out);
        g_by_guid.clear();
        for (int i = 0; i < (int)g_catalog.size(); ++i) g_by_guid[g_catalog[i].guid] = i;
        g_catalog_done = !g_catalog.empty();
    }
}

// ---- authoritative run model: Data.Run -> Player.Hand / Player.Stash ----
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

bool SnapshotFromRun(BoardSnapshot& snap);  // defined after ReadICard helpers

void* K_GameServiceManager = nullptr;
void* M_PauseOrUnpause = nullptr;

void* FindServiceManager() {
    if (!K_GameServiceManager) K_GameServiceManager = il2::KlassAny("", "GameServiceManager");
    if (!K_GameServiceManager || !M_FindObjectsOfType) return nullptr;
    void* type = il2::A.type_get_object(il2::A.class_get_type(K_GameServiceManager));
    void* arr = il2::Invoke1(M_FindObjectsOfType, nullptr, type);
    if (il2::ArrayLen(arr) == 0) return nullptr;
    void* cached = *(void**)il2::ArrayAt(arr, 0);
    if (cached) {
        M_PauseOrUnpause = MethodInHierarchy(il2::A.object_get_class(cached), "PauseOrUnpauseGame", 1);
    }
    return cached;
}

// ---- authoritative board model (ClientBoardModel.bazaarBoardModel) ----
void* K_ClientBoardModel = nullptr;

void AppendModelCards(void* board, const char* prop, std::vector<CardInfo>& out) {
    if (!board) return;
    void* kb = il2::A.object_get_class(board);
    void* getter = PropGetter(kb, prop);
    if (!getter) return;
    void* arr = il2::Invoke0(getter, board);
    int n = il2::ArrayLen(arr);
    for (int i = 0; i < n; ++i) {
        void* card = *(void**)il2::ArrayAt(arr, i);
        if (!card) continue;
        void* kc = il2::A.object_get_class(card);
        CardInfo ci;
        void* getId = MethodInHierarchy(kc, "get_Id", 0);
        if (getId) {
            void* g = il2::Invoke0(getId, card);
            if (g) ci.guid = GuidString((const uint8_t*)((char*)g + il2::ObjectHeaderSize()));
        }
        void* getName = MethodInHierarchy(kc, "get_Name", 0);
        if (getName) ci.name = InvokeString(getName, card);
        if (ci.name.empty()) {
            void* getInternal = MethodInHierarchy(kc, "get_InternalName", 0);
            if (getInternal) ci.name = InvokeString(getInternal, card);
        }
        void* getTier = MethodInHierarchy(kc, "get_Tier", 0);
        if (getTier) ci.tier = BoxedI32(il2::Invoke0(getTier, card));
        void* getSize = MethodInHierarchy(kc, "get_CardSize", 0);
        if (getSize) ci.size = BoxedI32(il2::Invoke0(getSize, card));
        if (!ci.guid.empty()) {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = g_by_guid.find(ci.guid);
            if (it != g_by_guid.end()) {
                if (ci.name.empty()) ci.name = g_catalog[it->second].name;
                ci.hero = g_catalog[it->second].hero;
                ci.tags = g_catalog[it->second].tags;
                ci.build_uses = g_catalog[it->second].build_uses;
            }
        }
        if (ci.name.empty() && !ci.guid.empty()) {
        if (const StaticCard* sc = FindStaticCard(ci.guid.c_str())) {
            ci.name = (sc->name_zh && *sc->name_zh) ? sc->name_zh : sc->name;
            std::string zh = LocalizeText(sc->title_key ? sc->title_key : "", sc->name);
            if (!zh.empty()) ci.name = zh;
            if (ci.hero.empty()) ci.hero = sc->heroes;
            if (ci.tags.empty()) ci.tags = sc->tags;
        }
    }
    if (ci.name.empty()) ci.name = ci.guid.empty() ? "?" : "Card " + ci.guid.substr(0, 8);
        ci.valid = true;
        out.push_back(std::move(ci));
    }
}

CardInfo ReadICard(void* card) {
    CardInfo ci;
    if (!card) return ci;
    void* k = il2::A.object_get_class(card);
    void* instance_get=PropGetter(k,"InstanceId");
    void* instance=instance_get?il2::Invoke0(instance_get,card):nullptr;
    if(instance)ci.instance=il2::Str(*(void**)((char*)instance+il2::ObjectHeaderSize()));
    void* gid = PropGetter(k, "TemplateId");
    if (gid) {
        void* g = il2::Invoke0(gid, card);
        if (g) ci.guid = GuidString((const uint8_t*)((char*)g + il2::ObjectHeaderSize()));
    }
    void* gt = PropGetter(k, "Tier");
    if (gt) ci.tier = BoxedI32(il2::Invoke0(gt, card));
    void* gs = PropGetter(k, "Size");
    if (gs) ci.size = BoxedI32(il2::Invoke0(gs, card));
    void* gn = PropGetter(k, "Name");
    if (gn) ci.name = InvokeString(gn, card);
    void* slot=PropGetter(k,"LeftSocketId");
    if(slot){void* boxed=il2::Invoke0(slot,card);if(boxed)ci.slot=BoxedI32(boxed);}
    void* enchantment=PropGetter(k,"Enchantment");
    if(enchantment){void* boxed=il2::Invoke0(enchantment,card);if(boxed)ci.enchantment=BoxedI32(boxed);}
    if (!ci.guid.empty()) {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_by_guid.find(ci.guid);
        if (it != g_by_guid.end()) {
            if (ci.name.empty()) ci.name = g_catalog[it->second].name;
            ci.hero = g_catalog[it->second].hero;
            ci.tags = g_catalog[it->second].tags;
            ci.build_uses = g_catalog[it->second].build_uses;
        }
    }
    if (ci.name.empty() && !ci.guid.empty()) {
        if (const StaticCard* sc = FindStaticCard(ci.guid.c_str())) {
            ci.name = (sc->name_zh && *sc->name_zh) ? sc->name_zh : sc->name;
            std::string zh = LocalizeText(sc->title_key ? sc->title_key : "", sc->name);
            if (!zh.empty()) ci.name = zh;
            if (ci.hero.empty()) ci.hero = sc->heroes;
            if (ci.tags.empty()) ci.tags = sc->tags;
        }
    }
    if (ci.name.empty()) ci.name = ci.guid.empty() ? "?" : "Card " + ci.guid.substr(0, 8);
    ci.valid = true;
    return ci;
}


// Index-based traversal: enumerating a live List<> throws "Collection was modified"
// whenever the game touches it between MoveNext() calls. get_Count + get_Item never do.
bool ForEachListItem(void* list, const std::function<void(void*)>& fn, int* out_count) {
    if (!list) return false;
    void* klass = il2::A.object_get_class(list);
    void* get_count = MethodInHierarchy(klass, "get_Count", 0);
    void* get_item = MethodInHierarchy(klass, "get_Item", 1);
    if (!get_count || !get_item) return false;
    int n = BoxedI32(il2::Invoke0(get_count, list));
    if (out_count) *out_count = n;
    for (int i = 0; i < n; ++i) {
        void* args[1] = {&i};
        void* item = il2::Invoke(get_item, list, args);
        if (item) fn(item);
    }
    return true;
}

void AppendInventory(void* owner, const char* prop, std::vector<CardInfo>& out) {
    if (!owner) return;
    void* getInv = PropGetter(il2::A.object_get_class(owner), prop);
    void* inv = getInv ? il2::Invoke0(getInv, owner) : nullptr;
    if (!inv) return;
    void* getItems = MethodInHierarchy(il2::A.object_get_class(inv), "GetItems", 0);
    void* list = getItems ? il2::Invoke0(getItems, inv) : nullptr;
    if (!list) return;
    ForEachListItem(list, [&](void* item) { out.push_back(ReadICard(item)); }, nullptr);
}

bool SnapshotFromRun(BoardSnapshot& snap) {
    if (!K_Data) return false;
    const uint64_t exc_before = il2::ExceptionCount();
    static void* has_active=nullptr;
    if(!has_active)has_active=PropGetter(K_Data,"HasActiveRun");
    if(has_active&&!BoxedI32(il2::Invoke0(has_active,nullptr)))return true;
    static void* getRun = nullptr;
    if (!getRun) getRun = PropGetter(K_Data, "Run");
    if (!getRun) return false;
    void* run = il2::Invoke0(getRun, nullptr);
    if (!run) return false;
    void* rk = il2::A.object_get_class(run);
    void* gDay = PropGetter(rk, "Day");
    if (gDay) snap.day = BoxedI32(il2::Invoke0(gDay, run));
    void* gHour = PropGetter(rk, "Hour");
    if (gHour) snap.hour = BoxedI32(il2::Invoke0(gHour, run));
    void* wins=PropGetter(rk,"Victories"),*losses=PropGetter(rk,"Losses");
    if(wins)snap.victories=BoxedI32(il2::Invoke0(wins,run));
    if(losses)snap.losses=BoxedI32(il2::Invoke0(losses,run));
    void* gPlayer = PropGetter(rk, "Player");
    void* player = gPlayer ? il2::Invoke0(gPlayer, run) : nullptr;
    if (player) {
        void* gHero = PropGetter(il2::A.object_get_class(player), "Hero");
        if (gHero) {
            int h = BoxedI32(il2::Invoke0(gHero, player));
            if (h >= 0 && h <= 8) snap.hero = kHeroNames[h];
        }
        AppendInventory(player, "Hand", snap.board);
        AppendInventory(player, "Stash", snap.stash);
    }
    void* pSkills = player ? PropGetter(il2::A.object_get_class(player), "Skills") : nullptr;
    if (pSkills) {
        void* list = il2::Invoke0(pSkills, player);
        ForEachListItem(list, [&](void* item) { snap.skills.push_back(ReadICard(item)); },
                        nullptr);
    }
    if (il2::ExceptionCount() != exc_before) {
        LOGW("board snapshot aborted: inventory changed while enumerating");
        return false;
    }
    void* gOpp = PropGetter(rk, "Opponent");
    void* opp = gOpp ? il2::Invoke0(gOpp, run) : nullptr;
    if (opp) AppendInventory(opp, "Hand", snap.opponent);
    void* opp_skills=opp?PropGetter(il2::A.object_get_class(opp),"Skills"):nullptr;
    if(opp_skills)ForEachListItem(il2::Invoke0(opp_skills,opp),[&](void* item){snap.opponent_skills.push_back(ReadICard(item));},nullptr);
    // Shop / reward choices live on RunState, not on the board.
    static void* getState = nullptr;
    static void* getCombat = nullptr;
    if (!getState) getState = PropGetter(K_Data, "CurrentState");
    if (!getCombat) getCombat = PropGetter(K_Data, "IsInCombat");
    if (getCombat) snap.in_combat = BoxedI32(il2::Invoke0(getCombat, nullptr)) != 0;
    if (getState) {
        void* state = il2::Invoke0(getState, nullptr);
        if (state) {
            void* getSel = MethodInHierarchy(il2::A.object_get_class(state),
                                             "GetCardsInSelectionSet", 0);
            void* list = getSel ? il2::Invoke0(getSel, state) : nullptr;
            if (list) il2::Enumerate(list, [&](void* item) {
                if (item) snap.shop.push_back(ReadICard(item));
            });
        }
    }
    snap.in_run = player != nullptr;
    return snap.in_run || snap.day > 0;
}

bool ScanBoardFromModel(BoardSnapshot& snap) {
    if (!M_FindObjectsOfType) return false;
    if (!K_ClientBoardModel) K_ClientBoardModel = il2::KlassAny("", "ClientBoardModel");
    if (!K_ClientBoardModel) return false;
    void* type_obj = il2::A.type_get_object(il2::A.class_get_type(K_ClientBoardModel));
    void* arr = il2::Invoke1(M_FindObjectsOfType, nullptr, type_obj);
    int n = il2::ArrayLen(arr);
    if (n == 0) return false;
    void* model = *(void**)il2::ArrayAt(arr, 0);
    if (!model) return false;
    void* kModel = il2::A.object_get_class(model);
    void* getBoard = MethodInHierarchy(kModel, "get_bazaarBoardModel", 0);
    void* board = nullptr;
    if (getBoard) {
        board = il2::Invoke0(getBoard, model);
    } else {
        void* f = FieldInHierarchy(kModel, "bazaarBoardModel");
        if (f) board = il2::FieldPtr(model, f);
    }
    if (!board) return false;
    AppendModelCards(board, "PlayerCardHand", snap.board);
    AppendModelCards(board, "PlayerStorageHand", snap.stash);
    AppendModelCards(board, "OpponentCardHand", snap.opponent);
    snap.in_run = !snap.board.empty() || !snap.stash.empty() || !snap.opponent.empty();
    if (!snap.board.empty()) snap.hero = snap.board.front().hero;
    return true;
}

std::string GameObjectName(void* comp) {
    static void* K_Component = nullptr;
    static void* M_get_go = nullptr;
    static void* M_get_name = nullptr;
    if (!K_Component) {
        K_Component = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Component");
        if (K_Component) {
            void* p = il2::A.class_get_property_from_name(K_Component, "gameObject");
            if (p) M_get_go = il2::A.property_get_get_method(p);
            void* po = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
            if (po) {
                void* pn = il2::A.class_get_property_from_name(po, "name");
                if (pn) M_get_name = il2::A.property_get_get_method(pn);
            }
        }
    }
    if (!M_get_go || !M_get_name) return {};
    void* go = il2::Invoke0(M_get_go, comp);
    if (!go) return {};
    return il2::Str(il2::Invoke0(M_get_name, go));
}

void ScanBoard() {
    BoardSnapshot snap;
    if (SnapshotFromRun(snap)) {
        std::lock_guard<std::mutex> lk(g_mu);
        g_snapshot = std::move(snap);
        static uint64_t last_log = 0;
        if (bpp::NowMs() - last_log > 5000) {
            last_log = bpp::NowMs();
            LOGI("board: day=%d hero=%s board=%zu stash=%zu opp=%zu shop=%zu skills=%zu",
                 g_snapshot.day, g_snapshot.hero.c_str(), g_snapshot.board.size(),
                 g_snapshot.stash.size(), g_snapshot.opponent.size(), g_snapshot.shop.size(),
                 g_snapshot.skills.size());
        }
        return;
    }
    // Fallbacks (ClientBoardModel / UI scan) removed: they crashed on device.
}
}  // namespace

std::vector<CardInfo> Catalog() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_catalog;
}

int CatalogSize() {
    std::lock_guard<std::mutex> lk(g_mu);
    return (int)g_catalog.size();
}

const CardInfo* FindCard(const std::string& guid) {
    std::lock_guard<std::mutex> lk(g_mu);
    auto it = g_by_guid.find(guid);
    if (it == g_by_guid.end()) return nullptr;
    return &g_catalog[it->second];
}

namespace {
bool BoxedBool(void* value){return value&&*(uint8_t*)((char*)value+il2::ObjectHeaderSize());}

void* ActiveGameObject(void* view){
    void* get=MethodInHierarchy(il2::A.object_get_class(view),"get_gameObject",0);
    void* go=get?il2::Invoke0(get,view):nullptr;
    void* active=go?PropGetter(il2::A.object_get_class(go),"activeInHierarchy"):nullptr;
    return active&&BoxedBool(il2::Invoke0(active,go))?go:nullptr;
}

bool ProjectCard(void* view,void* go,ScreenCard& card){
    struct V3{float x,y,z;};struct Bounds{V3 center,extents;};
    void* k=il2::A.object_get_class(view);
    void* camera_field=FieldInHierarchy(k,"mainCamera");
    void* camera=camera_field?il2::FieldPtr(view,camera_field):nullptr;
    if(!camera){
        static void* klass=il2::Klass("UnityEngine.CoreModule.dll","UnityEngine","Camera");
        void* get=klass?PropGetter(klass,"main"):nullptr;
        camera=get?il2::Invoke0(get,nullptr):nullptr;
    }
    if(!camera)return false;
    void* ck=il2::A.object_get_class(camera);
    void* project=MethodInHierarchy(ck,"WorldToScreenPoint",1);
    void* get_w=PropGetter(ck,"pixelWidth"),*get_h=PropGetter(ck,"pixelHeight");
    int w=get_w?BoxedI32(il2::Invoke0(get_w,camera)):0,h=get_h?BoxedI32(il2::Invoke0(get_h,camera)):0;
    if(!project||w<1||h<1)return false;
    Bounds bounds{};bool has_bounds=false;
    auto include=[&](void* component){
        void* get=component?PropGetter(il2::A.object_get_class(component),"bounds"):nullptr;
        void* boxed=get?il2::Invoke0(get,component):nullptr;if(!boxed)return;
        Bounds next;memcpy(&next,(char*)boxed+il2::ObjectHeaderSize(),sizeof(next));
        if(!std::isfinite(next.center.x)||!std::isfinite(next.center.y)||!std::isfinite(next.center.z)||
           !std::isfinite(next.extents.x)||!std::isfinite(next.extents.y)||!std::isfinite(next.extents.z)||
           next.extents.x<=0||next.extents.y<0||next.extents.z<0)return;
        if(!has_bounds){bounds=next;has_bounds=true;return;}
        auto merge=[](float& c,float& e,float nc,float ne){float low=std::min(c-e,nc-ne),high=std::max(c+e,nc+ne);c=(low+high)*.5f;e=(high-low)*.5f;};
        merge(bounds.center.x,bounds.extents.x,next.center.x,next.extents.x);
        merge(bounds.center.y,bounds.extents.y,next.center.y,next.extents.y);
        merge(bounds.center.z,bounds.extents.z,next.center.z,next.extents.z);
    };
    void* get_collider=PropGetter(k,"BoxCollider");
    if(get_collider)include(il2::Invoke0(get_collider,view));
    if(!has_bounds){
        static void* renderer=il2::Klass("UnityEngine.CoreModule.dll","UnityEngine","MeshRenderer");
        void* get=MethodInHierarchy(il2::A.object_get_class(go),"GetComponentsInChildren",1);
        if(renderer&&get){
            void* type=il2::A.type_get_object(il2::A.class_get_type(renderer));
            void* renderers=il2::Invoke1(get,go,type);
            for(int i=0;i<std::min(il2::ArrayLen(renderers),32);++i){
                void* r=*(void**)il2::ArrayAt(renderers,i);if(!r)continue;
                void* enabled=PropGetter(il2::A.object_get_class(r),"enabled");
                if(enabled&&!BoxedBool(il2::Invoke0(enabled,r)))continue;
                include(r);
            }
        }
    }
    if(!has_bounds)return false;
    float top=-1,bottom=(float)h,left=(float)w,right=0;
    for(int corner=0;corner<8;++corner){
        V3 p={bounds.center.x+((corner&1)?1:-1)*bounds.extents.x,bounds.center.y+((corner&2)?1:-1)*bounds.extents.y,bounds.center.z+((corner&4)?1:-1)*bounds.extents.z};
        void* args[]={&p};void* result=il2::Invoke(project,camera,args);if(!result)return false;
        V3 point;memcpy(&point,(char*)result+il2::ObjectHeaderSize(),sizeof(point));
        if(!std::isfinite(point.x)||!std::isfinite(point.y)||point.z<=0)return false;
        left=std::min(left,point.x);right=std::max(right,point.x);
        top=std::max(top,point.y);bottom=std::min(bottom,point.y);
    }
    float x=(left+right)*.5f;
    if(x<0||x>w||top<=0||top>h||right<=left||top<=bottom)return false;
    card.x=x/w;card.y=1-top/h;card.width=(right-left)/w;card.height=(top-bottom)/h;return true;
}
}

std::vector<ScreenCard> ScreenCards(const std::set<std::string>& wanted) {
    std::vector<ScreenCard> out;
    g_projection_status=wanted.empty()?"未选择关注阵容":"等待卡牌视图";
    if(wanted.empty()||!M_FindObjectsOfType)return out;
    auto snap=Snapshot();std::set<std::string> instances;
    auto collect=[&](const std::vector<CardInfo>& cards){for(const auto& c:cards)if(wanted.count(c.guid)&&!c.instance.empty())instances.insert(c.instance);};
    collect(snap.shop);if(snap.in_combat){collect(snap.opponent);collect(snap.opponent_skills);}
    int views=0,matching=0;
    std::set<std::string> marked;
    // Mobile skills use SkillProxyRenderer rather than CardController.
    for(const char* kind:{"CardController","SkillProxyRenderer"}){
        bool skill=!strcmp(kind,"SkillProxyRenderer");
        void* klass=il2::KlassAny("",kind);if(!klass)continue;
        void* type=il2::A.type_get_object(il2::A.class_get_type(klass));
        void* objects=il2::Invoke1(M_FindObjectsOfType,nullptr,type);views+=il2::ArrayLen(objects);
        for(int i=0;i<std::min(il2::ArrayLen(objects),160);++i){
            void* view=*(void**)il2::ArrayAt(objects,i);if(!view)continue;
            void* go=ActiveGameObject(view);if(!go)continue;
            void* k=il2::A.object_get_class(view);
            void* data_get=PropGetter(k,skill?"Card":"CardData");
            void* data=data_get?il2::Invoke0(data_get,view):nullptr;if(!data)continue;
            void* dk=il2::A.object_get_class(data);
            void* id_get=PropGetter(dk,"TemplateId");
            void* boxed=id_get?il2::Invoke0(id_get,data):nullptr;if(!boxed)continue;
            std::string id=GuidString((const uint8_t*)boxed+il2::ObjectHeaderSize());if(!wanted.count(id))continue;
            void* get_instance=PropGetter(dk,"InstanceId");
            void* instance=get_instance?il2::Invoke0(get_instance,data):nullptr;
            std::string key=instance?il2::Str(*(void**)((char*)instance+il2::ObjectHeaderSize())):"";
            if(skill){if(key.empty()||!instances.count(key))continue;}
            else{
                // An active opponent/merchant controller is authoritative even
                // when RunState.GetCardsInSelectionSet() omits that shop choice.
                void* section=FieldInHierarchy(k,"boardSection");
                if(!section||il2::FieldI32(view,section)!=0)continue;
                void* visible=PropGetter(k,"IsCardVisible");
                if(visible&&!BoxedBool(il2::Invoke0(visible,view)))continue;
            }
            ++matching;if(!key.empty()&&marked.count(key))continue;
            ScreenCard point{id};
            if(ProjectCard(view,go,point)){out.push_back(point);if(!key.empty())marked.insert(key);}
            if(out.size()>=24)break;
        }
    }
    g_projection_status="视图 "+std::to_string(views)+" · 命中 "+std::to_string(matching)+" · 已标记 "+std::to_string(out.size());
    return out;
}

std::string ScreenCardStatus(){return g_projection_status;}

BoardSnapshot Snapshot() {
    std::lock_guard<std::mutex> lk(g_mu);
    return g_snapshot;
}

void SetSpeed(float scale) {
    g_speed = scale;
    // Prefer the game's own pause API: it coordinates with the combat state
    // instead of freezing the whole engine.
    void* svc = FindServiceManager();
    if(svc&&scale>0){
        void* baseline=FieldInHierarchy(il2::A.object_get_class(svc),"NormalSpeedTimeScaleValue");
        if(baseline)il2::A.field_set_value(svc,baseline,&scale);
    }
    if (svc && M_PauseOrUnpause) {
        uint8_t want_pause = (scale <= 0.01f) ? 1 : 0;
        il2::Invoke1(M_PauseOrUnpause, svc, &want_pause);
        LOGI("PauseOrUnpauseGame(%d)", (int)want_pause);
    }
    if (!M_set_timeScale) {
        void* K_Time = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Time");
        if (K_Time) M_set_timeScale = il2::Method(K_Time, "set_timeScale", 1);
    }
    if (M_set_timeScale) {
        float v = scale;
        il2::Invoke1(M_set_timeScale, nullptr, &v);
        LOGI("timeScale -> %.2f", v);
    }
}

namespace {
std::atomic<bool> g_dict_ready{false};
std::atomic<bool> g_worker_started{false};
void* g_dict = nullptr;
uint32_t g_dict_handle = 0;

bool ManagerCreated() {
    if (!M_IsManagerCreated) return true;
    return BoxedI32(il2::InvokeStatic0(M_IsManagerCreated)) != 0;
}

void CatalogWorker() {
    il2::AttachCurrentThread();
    for (int i = 0; i < 1200; ++i) {  // up to 10 minutes
        if (g_dict_ready.load()) return;
        if (ManagerCreated()) {
            void* dict = FetchCardMap();
            if (dict) {
                g_dict = dict;
                g_dict_handle = il2::A.gchandle_new ? il2::A.gchandle_new(dict, false) : 0;
                g_dict_ready.store(true);
                LOGI("catalog dictionary fetched on worker thread (%p)", dict);
                return;
            }
        }
        bpp::SleepMs(500);
    }
    LOGW("catalog worker gave up waiting for static data");
}
}  // namespace

void RequestCatalog() { StartCatalogWorker(); }

// Synchronous variant used for testing: some runtimes fault when SQLite-net is
// driven from a worker thread, so allow the caller to build on the main thread.
void BuildCatalogOnMainThread() {
    if (g_catalog_done) return;
    void* dict = FetchCardMap();
    if (!dict) {
        LOGW("catalog: FetchCardMap returned null");
        return;
    }
    BuildCatalogFromDict(dict);
}

void StartCatalogWorker() {
    if (g_worker_started.exchange(true)) return;
    std::thread(CatalogWorker).detach();
}

// Called from the Unity main thread: enumeration is cheap (a few thousand property
// reads), only the fetch above is expensive.
void MaybeBuildCatalog() {
    if (!g_dict_ready.load()) return;
    void* dict = g_dict;
    if (g_dict_handle && il2::A.gchandle_get_target) dict = il2::A.gchandle_get_target(g_dict_handle);
    if (dict) BuildCatalogFromDict(dict);
    if (g_dict_handle && il2::A.gchandle_free) il2::A.gchandle_free(g_dict_handle);
    g_dict_handle = 0;
    g_dict = nullptr;
    g_dict_ready.store(false);
}

float Speed() { return g_speed; }

float ActualSpeed() {
    static void* getter = nullptr;
    if (!getter) {
        void* klass = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Time");
        if (klass) getter = il2::Method(klass, "get_timeScale", 0);
    }
    void* result = getter ? il2::Invoke0(getter, nullptr) : nullptr;
    return result ? *(float*)((char*)result + il2::ObjectHeaderSize()) : -1.0f;
}

// Scene/combat transitions restore the game's normal speed. Keep that baseline
// aligned with the user's selection, without unpausing or cancelling slow motion.
void MaintainSpeed() {
    if(g_speed<=0)return;
    void* svc=FindServiceManager();if(!svc)return;
    void* k=il2::A.object_get_class(svc);
    void* baseline=FieldInHierarchy(k,"NormalSpeedTimeScaleValue");
    if(baseline)il2::A.field_set_value(svc,baseline,&g_speed);
    for(const char* name:{"<GamePaused>k__BackingField","IsTimeLerping","IsCountingDownToLerp","gameIsSlowingDown"}){
        void* field=FieldInHierarchy(k,name);
        if(field&&il2::FieldBool(svc,field))return;
    }
    const float actual=ActualSpeed();
    if(M_set_timeScale&&std::fabs(actual-1.f)<.001f&&std::fabs(g_speed-1.f)>.001f){
        il2::Invoke1(M_set_timeScale,nullptr,&g_speed);
        LOGI("restored selected speed after game reset: %.2f",g_speed);
    }
}

std::string VerifySpeedRecovery(){
    void* svc=FindServiceManager();
    float before=ActualSpeed();
    if(!svc||!M_set_timeScale||!M_PauseOrUnpause||g_speed<=1||before<=0)return "SKIP select 2x/3x while game is running";
    void* k=il2::A.object_get_class(svc);
    for(const char* name:{"<GamePaused>k__BackingField","IsTimeLerping","IsCountingDownToLerp","gameIsSlowingDown"}){
        void* f=FieldInHierarchy(k,name);if(f&&il2::FieldBool(svc,f))return "SKIP paused or transitioning";
    }
    float reset=1;il2::Invoke1(M_set_timeScale,nullptr,&reset);MaintainSpeed();float restored=ActualSpeed();
    uint8_t pause=1;il2::Invoke1(M_PauseOrUnpause,svc,&pause);MaintainSpeed();float paused=ActualSpeed();
    pause=0;il2::Invoke1(M_PauseOrUnpause,svc,&pause);float resumed=ActualSpeed();
    il2::Invoke1(M_set_timeScale,nullptr,&before);
    bool ok=std::fabs(restored-g_speed)<.01f&&std::fabs(paused)<.01f&&std::fabs(resumed-g_speed)<.01f;
    char text[128];snprintf(text,sizeof(text),"%s reset=%.2f paused=%.2f resumed=%.2f selected=%.2f",ok?"PASS":"FAIL",restored,paused,resumed,g_speed);
    return text;
}

bool Init() {
    if (g_inited) return true;
    if (!K_Data) K_Data = il2::KlassAny("TheBazaar", "Data");
    if (!K_JsonGameDataManager) {
        K_JsonGameDataManager = il2::KlassAny("TheBazaar.DataManagement.Json", "JsonGameDataManager");
    }
    if (!K_Data || !K_JsonGameDataManager) {
        static uint64_t last = 0;
        if (bpp::NowMs() - last > 5000) {
            last = bpp::NowMs();
            LOGW("game api not ready: Data=%p manager=%p", K_Data, K_JsonGameDataManager);
        }
        return false;
    }
    if (!M_GetStatic) M_GetStatic = il2::Method(K_Data, "GetStatic", 0);
    if (!M_IsManagerCreated) M_IsManagerCreated = il2::Method(K_Data, "IsManagerCreated", 0);
    if (!M_GetCardMap) M_GetCardMap = il2::Method(K_JsonGameDataManager, "GetCardMap", 0);
    if (!M_FindObjectsOfType) {
        void* kobj = il2::Klass("UnityEngine.CoreModule.dll", "UnityEngine", "Object");
        if (!kobj) kobj = il2::KlassAny("UnityEngine", "Object");
        if (kobj) M_FindObjectsOfType = il2::Method(kobj, "FindObjectsOfType", 1);
    }
    g_inited = true;
    LOGI("game api: Data=%p mgr=%p getStatic=%p getCardMap=%p find=%p", K_Data,
         K_JsonGameDataManager, M_GetStatic, M_GetCardMap, M_FindObjectsOfType);
    return true;
}

// Runtime probe: read one card straight from the game's own data manager.
// Uses GetCardById only (the full-table GetCardMap crashed before).
std::string LocalizeText(const std::string& hash, const std::string& text);

void ProbeCards() {
    if (!M_GetStatic) {
        LOGW("probe: M_GetStatic null (Init not run?)");
        return;
    }
    void* mgr = il2::InvokeStatic0(M_GetStatic);
    LOGI("probe: GetStatic=%p class=%s", mgr,
         mgr ? il2::ClassName(il2::A.object_get_class(mgr)).c_str() : "(null)");
    if (!IsInstanceOf(mgr, K_JsonGameDataManager)) {
        LOGW("probe: not a JsonGameDataManager");
        return;
    }
    BoardSnapshot s = Snapshot();
    if (s.board.empty()) {
        LOGW("probe: board empty, nothing to look up");
        return;
    }
    const std::string guid = s.board.front().guid;
    static void* K_Guid = nullptr;
    static void* M_Parse = nullptr;
    static void* M_GetById = nullptr;
    if (!K_Guid) K_Guid = il2::KlassAny("System", "Guid");
    if (K_Guid && !M_Parse) M_Parse = il2::Method(K_Guid, "Parse", 1);
    if (!M_GetById) M_GetById = il2::Method(K_JsonGameDataManager, "GetCardById", 1);
    LOGI("probe: guid=%s parse=%p getById=%p", guid.c_str(), M_Parse, M_GetById);
    if (!M_Parse || !M_GetById) return;
    void* gs = il2::NewStr(guid);
    const uint64_t exc0 = il2::ExceptionCount();
    void* boxed = il2::Invoke1(M_Parse, nullptr, gs);
    if (!boxed) {
        LOGW("probe: Guid.Parse failed");
        return;
    }
    void* args[1] = {(char*)boxed + il2::ObjectHeaderSize()};
    void* card = il2::Invoke(M_GetById, mgr, args);
    LOGI("probe: GetCardById -> %p exc=%llu", card,
         (unsigned long long)(il2::ExceptionCount() - exc0));
    if (!card) return;
    void* ck = il2::A.object_get_class(card);
    LOGI("probe: card class=%s", il2::ClassName(ck).c_str());
    const char* props[] = {"Title", "Name", "InternalName", "DisplayName", "LocalizedName",
                           "Description", "InternalDescription"};
    for (const char* prop : props) {
        void* g = PropInHierarchy(ck, prop);
        if (!g) {
            LOGI("probe: %s -> (no getter)", prop);
            continue;
        }
        void* v = il2::Invoke0(g, card);
        if (!v) {
            LOGI("probe: %s -> null", prop);
            continue;
        }
        std::string s = InvokeString(g, card);
        LOGI("probe: %s -> [%s]", prop, s.c_str());
        std::string loc = LocalizeText("", s);
        if (!loc.empty()) LOGI("probe: localized(%s) -> [%s]", prop, loc.c_str());
    }
}

// Localization: PlatformKeywordManager holds a LocalizationService at +0x28.
void* FindLocalizationService() {
    static void* svc = nullptr;
    if (svc) return svc;
    static void* klass = nullptr;
    if (!klass) klass = il2::KlassAny("", "PlatformKeywordManager");
    if (!klass || !M_FindObjectsOfType) return nullptr;
    void* type = il2::A.type_get_object(il2::A.class_get_type(klass));
    void* arr = il2::Invoke1(M_FindObjectsOfType, nullptr, type);
    if (il2::ArrayLen(arr) == 0) return nullptr;
    void* mgr = *(void**)il2::ArrayAt(arr, 0);
    if (!mgr) return nullptr;
    svc = *(void**)((char*)mgr + 0x28);
    LOGI("localization: manager=%p service=%p", mgr, svc);
    return svc;
}

std::string LocalizeText(const std::string& hash, const std::string& text) {
    void* svc = FindLocalizationService();
    if (!svc) return {};
    static void* m3 = nullptr;
    static void* m1 = nullptr;
    if (!m3) m3 = il2::Method(il2::A.object_get_class(svc), "TryGetText", 3);
    if (!m1) m1 = il2::Method(il2::A.object_get_class(svc), "TryGetText", 1);
    if (m1) {
        void* t = il2::NewStr(text);
        void* out = nullptr;
        void* args[2] = {t, &out};
        void* ok = il2::Invoke(m1, svc, args);
        if (ok && BoxedI32(ok) && out) {
            std::string s = il2::Str(out);
            if (!s.empty()) return s;
        }
    }
    if (m3) {
        void* h = il2::NewStr(hash);
        void* t = il2::NewStr(text);
        void* out = nullptr;
        void* args[3] = {h, t, &out};
        void* ok = il2::Invoke(m3, svc, args);
        if (ok && BoxedI32(ok) && out) return il2::Str(out);
    }
    return {};
}

void Tick() {
    if (!g_inited) {
        if (!Init()) return;
    }
    uint64_t now = bpp::NowMs();
    if (now - g_last_scan < 1000) return;
    g_last_scan = now;
    if (!g_catalog_done) {
        MaybeBuildCatalog();
        if (!g_catalog_done) {
            static uint64_t last_warn = 0;
            if (now - last_warn > 15000) {
                last_warn = now;
                int created = M_IsManagerCreated ? BoxedI32(il2::InvokeStatic0(M_IsManagerCreated)) : -1;
                LOGW("catalog not ready (IsManagerCreated=%d, GetStatic=%p, GetCardMap=%p)",
                     created, M_GetStatic, M_GetCardMap);
            }
        }
    }
    ScanBoard();
    MaintainSpeed();
}

std::vector<BuildMatch> MatchBuilds(const BoardSnapshot& snap, int limit) {
    return catalog::Match(snap.board, snap.stash, snap.shop, limit);
}


// ================= tooltip companion + diagnostics (v0.2) =================
#include <shadowhook.h>

namespace {
uint32_t g_tooltip_handle = 0;
uint64_t g_tooltip_seen = 0;
std::atomic<bool> g_tooltip_enabled{true};
std::atomic<bool> g_tooltip_hooked{false};
void* g_orig_render_tooltip = nullptr;
bool g_diag_tick = false;
bool g_diag_egl = false;
std::string g_last_error;

void* TooltipMethodPtr(void* mi) {
    Dl_info info{};
    void* a = mi ? *(void**)mi : nullptr;
    if (a && dladdr(a, &info) && info.dli_fname && strstr(info.dli_fname, "libil2cpp")) return a;
    void* b = mi ? *(void**)((char*)mi + 8) : nullptr;
    if (b && dladdr(b, &info) && info.dli_fname && strstr(info.dli_fname, "libil2cpp")) return b;
    return nullptr;
}

CardInfo ReadITCardBasic(void* val) {
    CardInfo ci;
    if (!val) return ci;
    void* klass = il2::A.object_get_class(val);
    void* getId = MethodInHierarchy(klass, "get_Id", 0);
    if (getId) {
        void* guid = il2::Invoke0(getId, val);
        if (guid) ci.guid = GuidString((const uint8_t*)((char*)guid + il2::ObjectHeaderSize()));
    }
    ci.name = CardTitle(val, klass);
    void* getDesc = MethodInHierarchy(klass, "get_InternalDescription", 0);
    if (getDesc) ci.description = InvokeString(getDesc, val);
    void* getType = MethodInHierarchy(klass, "get_Type", 0);
    if (getType) ci.kind = std::to_string(BoxedI32(il2::Invoke0(getType, val)));
    void* getSize = MethodInHierarchy(klass, "get_Size", 0);
    if (getSize) ci.size = BoxedI32(il2::Invoke0(getSize, val));
    void* getTier = MethodInHierarchy(klass, "get_StartingTier", 0);
    if (getTier) ci.tier = BoxedI32(il2::Invoke0(getTier, val));
    void* getHeroes = MethodInHierarchy(klass, "get_Heroes", 0);
    if (getHeroes) {
        void* set = il2::Invoke0(getHeroes, val);
        std::string heroes;
        il2::Enumerate(set, [&](void* item) {
            int ev = BoxedI32(item);
            if (ev >= 0 && ev <= 8) {
                if (!heroes.empty()) heroes += "/";
                heroes += kHeroNames[ev];
            }
        });
        ci.hero = heroes;
    }
    ci.tags = ReadTags(klass, val);
    ci.build_uses = catalog::UsageCount(ci.guid);
    ci.valid = true;
    return ci;
}

void* AugmentTooltip(void* orig, void* tmpl);  // defined below

void* RenderTooltipHook(void* self, void* mi, void* builder) {
    bpp::HookScope scope("CardTooltipData.RenderTooltip");
    void* tmpl = nullptr;
    if (g_tooltip_enabled.load() && self) {
        // CardTooltipData._cardTemplate at 0x18, _cardInstance at 0x10.
        tmpl = *(void**)((char*)self + off::kTooltipCardTemplate);
        if (tmpl) {
            if (g_tooltip_handle && il2::A.gchandle_free) il2::A.gchandle_free(g_tooltip_handle);
            g_tooltip_handle = il2::A.gchandle_new ? il2::A.gchandle_new(tmpl, false) : 0;
            g_tooltip_seen = bpp::NowMs();
        }
    }
    if (!g_orig_render_tooltip) return nullptr;
    void* orig = ((void* (*)(void*, void*, void*))g_orig_render_tooltip)(self, mi, builder);
    if (g_tooltip_enabled.load() && tmpl) return AugmentTooltip(orig, tmpl);
    return orig;
}

// ---------- tooltip text augmentation (works on GLES and Vulkan alike) ----------
// The game renders its own tooltip; we only extend the string it produced, so no
// custom graphics code is involved.
void* K_String = nullptr;
void* M_Concat2 = nullptr;
uint32_t g_suffix_handle = 0;
void* g_suffix_tmpl = nullptr;
uint64_t g_suffix_ts = 0;
uint32_t g_result_handle = 0;
void* g_result_orig = nullptr;
void* g_result_suffix = nullptr;

std::string BuildSuffixText(void* tmpl) {
    CardInfo ci = ReadITCardBasic(tmpl);
    if (!ci.valid) return {};
    std::string s = "\n\n[BPP] ";
    if (!ci.hero.empty()) s += ci.hero + "  ";
    s += "T" + std::to_string(ci.tier) + "  S" + std::to_string(ci.size);
    if (!ci.tags.empty()) s += "\n标签: " + ci.tags;
    if (ci.build_uses > 0) s += "\n十胜阵容出现 " + std::to_string(ci.build_uses) + " 次";
    return s;
}

void* AugmentTooltip(void* orig, void* tmpl) {
    if (!orig || !tmpl) return orig;
    if (!K_String) {
        K_String = il2::Klass("mscorlib.dll", "System", "String");
        if (K_String) M_Concat2 = il2::Method(K_String, "Concat", 2);
    }
    if (!M_Concat2) return orig;

    uint64_t now = bpp::NowMs();
    if (g_suffix_tmpl != tmpl || now - g_suffix_ts > 2000 || !g_suffix_handle) {
        std::string text = BuildSuffixText(tmpl);
        if (text.empty()) return orig;
        if (g_suffix_handle) il2::A.gchandle_free(g_suffix_handle);
        void* str = il2::NewStr(text);
        g_suffix_handle = (str && il2::A.gchandle_new) ? il2::A.gchandle_new(str, false) : 0;
        g_suffix_tmpl = tmpl;
        g_suffix_ts = now;
        g_result_orig = nullptr;
        g_result_suffix = nullptr;
        if (!g_suffix_handle) return orig;
    }
    void* suffix = il2::A.gchandle_get_target(g_suffix_handle);
    if (!suffix) return orig;

    if (g_result_handle && g_result_orig == orig && g_result_suffix == suffix) {
        void* cached = il2::A.gchandle_get_target(g_result_handle);
        if (cached) return cached;
    }
    void* args[2] = {orig, suffix};
    void* res = il2::Invoke(M_Concat2, nullptr, args);
    if (!res) return orig;
    if (g_result_handle) il2::A.gchandle_free(g_result_handle);
    g_result_handle = il2::A.gchandle_new ? il2::A.gchandle_new(res, false) : 0;
    g_result_orig = orig;
    g_result_suffix = suffix;
    return res;
}

std::unordered_map<int, std::string> g_sections;
bool g_sections_built = false;
}  // namespace

bool InstallTooltipHook() {
    if (g_tooltip_hooked.load()) return true;
    void* k = il2::KlassAny("TheBazaar.Tooltips", "CardTooltipData");
    if (!k) {
        g_last_error = "CardTooltipData not found";
        LOGE("%s", g_last_error.c_str());
        return false;
    }
    void* m = il2::Method(k, "RenderTooltip", 1);
    if (!m) {
        g_last_error = "RenderTooltip(TooltipBuilder) not found";
        LOGE("%s", g_last_error.c_str());
        return false;
    }
    void* fn = TooltipMethodPtr(m);
    if (!fn) {
        g_last_error = "RenderTooltip has no method pointer";
        LOGE("%s", g_last_error.c_str());
        return false;
    }
    void* stub = shadowhook_hook_func_addr(fn, (void*)RenderTooltipHook, &g_orig_render_tooltip);
    if (!stub || !g_orig_render_tooltip) {
        g_last_error = "shadowhook failed for RenderTooltip";
        LOGE("%s", g_last_error.c_str());
        return false;
    }
    g_tooltip_hooked.store(true);
    LOGI("tooltip hook installed: CardTooltipData::RenderTooltip @%p", fn);
    return true;
}

void SetTooltipEnabled(bool on) { g_tooltip_enabled.store(on); }
bool TooltipEnabled() { return g_tooltip_enabled.load(); }

TooltipInfo LastTooltip() {
    TooltipInfo out;
    if (!g_tooltip_handle || !il2::A.gchandle_get_target) return out;
    if (bpp::NowMs() - g_tooltip_seen > 6000) return out;
    void* tmpl = il2::A.gchandle_get_target(g_tooltip_handle);
    if (!tmpl) return out;
    out.card = ReadITCardBasic(tmpl);
    out.valid = out.card.valid;
    out.seen_ms = g_tooltip_seen;
    return out;
}

std::string SectionName(int value) {
    if (!g_sections_built) {
        g_sections_built = true;
        void* k = il2::KlassAny("", "BazaarBoard.ESections");
        if (k) {
            void* it = nullptr;
            while (const void* cf = il2::A.class_get_fields(k, &it)) {
                void* f = (void*)cf;
                const char* n = il2::A.field_get_name(f);
                int v = 0;
                il2::A.field_static_get_value(f, &v);
                if (n && *n) g_sections[v] = n;
            }
            LOGI("ESections resolved: %zu values", g_sections.size());
        } else {
            LOGE("ESections enum not found");
        }
    }
    auto it = g_sections.find(value);
    return it != g_sections.end() ? it->second : ("Section" + std::to_string(value));
}

Diagnostics GetDiagnostics() {
    Diagnostics d;
    d.il2cpp_ok = il2::DomainReady();
    d.data_resolved = K_Data != nullptr;
    d.catalog_ok = !g_catalog.empty();
    d.catalog_size = (int)g_catalog.size();
    d.board_size = (int)g_snapshot.board.size();
    d.tick_hook = g_diag_tick;
    d.egl_hook = g_diag_egl;
    d.tooltip_hook = g_tooltip_hooked.load();
    d.log_path = bpp::LogPath();
    d.last_error = g_last_error;
    return d;
}

void SetDiagFlags(bool tick, bool egl) {
    g_diag_tick = tick;
    g_diag_egl = egl;
}

// ---------- diagnostics dump (file channel) ----------
namespace {
std::string Sanitize(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) out.push_back((c == '\t' || c == '\n' || c == '\r') ? ' ' : c);
    if (out.size() > 400) out.resize(400);
    return out;
}
void DumpCard(FILE* f, const CardInfo& c) {
    fprintf(f, "card\t%s\t%s\t%s\t%d\t%d\t%s\t%s\t%d\n", c.guid.c_str(),
            Sanitize(c.name).c_str(), Sanitize(c.hero).c_str(), c.tier, c.size,
            Sanitize(c.tags).c_str(), Sanitize(c.kind).c_str(), c.build_uses);
}
}  // namespace

bool DumpToFile(const std::string& path) {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) {
        LOGE("dump: cannot open %s", path.c_str());
        return false;
    }
    Diagnostics d = GetDiagnostics();
    fprintf(f, "# BPP mobile dump\n");
    {
        std::string builds = bpp::MediaDir() + "/bpp_builds.json";
        struct stat st;
        if (stat(builds.c_str(), &st) == 0) {
            fprintf(f, "bpp_builds\tbytes=%lld\tmtime=%lld\n", (long long)st.st_size,
                    (long long)st.st_mtime);
        }
    }
    fprintf(f, "log\t%s\n", d.log_path.c_str());
    fprintf(f, "state\til2cpp=%d\tdata=%d\tcatalog=%d/%d\tboard=%d\ttick=%d\tegl=%d\ttooltip=%d\n",
            d.il2cpp_ok, d.data_resolved, d.catalog_size, d.catalog_ok, d.board_size, d.tick_hook,
            d.egl_hook, d.tooltip_hook);
    if (!d.last_error.empty()) fprintf(f, "error\t%s\n", Sanitize(d.last_error).c_str());

    std::vector<CardInfo> cat = Catalog();
    fprintf(f, "catalog_count\t%zu\n", cat.size());
    for (auto& c : cat) DumpCard(f, c);

    BoardSnapshot s = Snapshot();
    fprintf(f,
            "run\tin_run=%d\tin_combat=%d\thero=%s\tday=%d\thour=%d\tboard=%zu\tstash=%zu\t"
            "opponent=%zu\tshop=%zu\tskills=%zu\n",
            s.in_run, s.in_combat, Sanitize(s.hero).c_str(), s.day, s.hour, s.board.size(),
            s.stash.size(), s.opponent.size(), s.shop.size(), s.skills.size());
    for (auto& c : s.board) DumpCard(f, c);
    for (auto& c : s.stash) DumpCard(f, c);
    for (auto& c : s.opponent) DumpCard(f, c);
    for (auto& c : s.shop) DumpCard(f, c);
    for (auto& c : s.skills) DumpCard(f, c);
    fclose(f);
    LOGI("dump written: %s (%zu cards)", path.c_str(), cat.size());
    return true;
}
}  // namespace game
