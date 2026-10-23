#pragma once
#include "bpp_common.h"
#include <set>

namespace game {

struct CardInfo {
    std::string guid;
    std::string instance;
    std::string name;
    std::string description;
    std::string hero;
    std::string kind;   // Item / Skill / Monster / ...
    std::string tags;   // ECardTag names, "/" separated
    int build_uses = 0; // occurrences in the ten-win build catalog
    int tier = 0;
    int size = 0;
    int cost = 0;
    int slot = -1;
    int enchantment = -1;
    bool valid = false;
};

struct BoardSnapshot {
    bool in_run = false;
    std::string hero;
    int day = 0;
    int hour = 0;
    int gold = 0;
    int victories = 0;
    int losses = 0;
    std::vector<CardInfo> board;    // BazaarBoard.ESections.Player (1)
    std::vector<CardInfo> stash;    // Storage (2)
    std::vector<CardInfo> opponent; // Opponent (0)
    std::vector<CardInfo> shop;     // RunState.GetCardsInSelectionSet()
    std::vector<CardInfo> skills;   // Player.Skills
    std::vector<CardInfo> opponent_skills;
    bool in_combat = false;
};

bool Init();                      // resolve game types (main thread)
bool CatalogReady();
void Tick();  // main thread, resolves data lazily
std::vector<CardInfo> Catalog();
int CatalogSize();   // cheap size query for HUD text
void StartCatalogWorker();  // heavy GetCardMap() off the main thread
void RequestCatalog();      // lazy: only fetch when the UI/dump actually needs it
void BuildCatalogOnMainThread();  // diagnostic: synchronous build
void ProbeCards();                // diagnostic: single-card lookup via the game manager
void MaybeBuildCatalog();   // main thread: enumerate the fetched dictionary
const CardInfo* FindCard(const std::string& guid);

BoardSnapshot Snapshot();
struct ScreenCard { std::string guid; float x=0,y=0,width=0,height=0; };
std::vector<ScreenCard> ScreenCards(const std::set<std::string>& wanted);
std::string ScreenCardStatus();

void SetSpeed(float scale);       // 0.5 / 1 / 2 / 4
float Speed();
float ActualSpeed();             // Unity Time.timeScale; call only on the main thread
std::string VerifySpeedRecovery(); // explicit, one-shot device diagnostic

// Ten-win build catalog (BPP metrics snapshot), matched against a live snapshot.
struct BuildMatch {
    std::string hero;
    int matched = 0;
    int ten_win_runs = 0;
    int rate_bps = 0;
    int score = 0;
    std::vector<std::string> card_guids;  // ordered by slot
};
std::vector<BuildMatch> MatchBuilds(const BoardSnapshot& snap, int limit);

// ---- tooltip companion (v0.2) ----
// Hooks CardTooltipData::RenderTooltip(TooltipBuilder) and remembers which card the
// game is currently showing a tooltip for. Read-only.
bool InstallTooltipHook();
void SetTooltipEnabled(bool on);
bool TooltipEnabled();

struct TooltipInfo {
    bool valid = false;
    CardInfo card;
    uint64_t seen_ms = 0;
};
TooltipInfo LastTooltip();

// Human readable name for BazaarBoard.ESections (resolved from the enum at runtime).
std::string SectionName(int value);

// Diagnostics for the overlay.
struct Diagnostics {
    bool il2cpp_ok = false;
    bool data_resolved = false;
    bool catalog_ok = false;
    int catalog_size = 0;
    int board_size = 0;
    bool tick_hook = false;
    bool egl_hook = false;
    bool tooltip_hook = false;
    std::string log_path;
    std::string last_error;
};
Diagnostics GetDiagnostics();
bool DumpToFile(const std::string& path);   // full catalog + board snapshot, tab separated
void SetDiagFlags(bool tick, bool egl);

}  // namespace game
