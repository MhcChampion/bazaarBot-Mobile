#include "catalog.h"
#include <unordered_map>

// Generated data lives in catalog_data.cpp:
//   namespace catalog_data { extern const char* const kCardGuids[]; extern const int kCardGuidCount; ... }
#include "catalog_data.h"

namespace catalog {
namespace {
std::vector<std::string> g_guids;
std::vector<HeroBuilds> g_heroes;
bool g_loaded = false;

void EnsureLoaded() {
    if (g_loaded) return;
    g_loaded = true;
    g_guids.assign(catalog_data::kCardGuids, catalog_data::kCardGuids + catalog_data::kCardGuidCount);
    for (int h = 0; h < catalog_data::kHeroCount; ++h) {
        const auto& src = catalog_data::kHeroes[h];
        HeroBuilds hb;
        hb.hero = src.hero;
        for (int b = 0; b < src.buildCount; ++b) {
            const auto& sb = src.builds[b];
            Build build;
            build.refs.assign(sb.refs, sb.refs + sb.refCount);
            for (int r = 0; r < sb.rowCount; ++r) {
                const auto& sr = sb.rows[r];
                build.rows.push_back(Row{sr.card_ref, sr.slot, sr.tier, sr.enchant, sr.size});
            }
            build.ten_win_runs = sb.ten_win_runs;
            build.rate_bps = sb.rate_bps;
            build.score = sb.score;
            hb.builds.push_back(std::move(build));
        }
        g_heroes.push_back(std::move(hb));
    }
    LOGI("catalog loaded: %zu guids, %zu heroes", g_guids.size(), g_heroes.size());
}
}  // namespace

const std::vector<std::string>& CardGuids() {
    EnsureLoaded();
    return g_guids;
}

const std::vector<HeroBuilds>& All() {
    EnsureLoaded();
    return g_heroes;
}

std::string Title() { return catalog_data::kTitle; }

int UsageCount(const std::string& guid) {
    EnsureLoaded();
    if (guid.empty()) return 0;
    int n = 0;
    for (auto& hero : g_heroes)
        for (auto& b : hero.builds)
            for (int ref : b.refs)
                if (ref >= 0 && ref < (int)g_guids.size() && g_guids[ref] == guid) {
                    ++n;
                    break;
                }
    return n;
}

std::vector<game::BuildMatch> Match(const std::vector<game::CardInfo>& board,
                                    const std::vector<game::CardInfo>& stash,
                                    const std::vector<game::CardInfo>& shop, int limit) {
    EnsureLoaded();
    std::vector<game::BuildMatch> out;
    if (g_guids.empty()) return out;

    std::unordered_map<std::string, int> weight;
    for (auto& c : shop)
        if (!c.guid.empty()) weight[c.guid] = std::max(weight[c.guid], 1);
    for (auto& c : stash)
        if (!c.guid.empty()) weight[c.guid] = std::max(weight[c.guid], 2);
    for (auto& c : board)
        if (!c.guid.empty()) weight[c.guid] = std::max(weight[c.guid], 3);

    for (auto& hero : g_heroes) {
        for (auto& b : hero.builds) {
            int matched = 0, weight_sum = 0;
            for (int ref : b.refs) {
                if (ref < 0 || ref >= (int)g_guids.size()) continue;
                auto it = weight.find(g_guids[ref]);
                if (it != weight.end()) {
                    ++matched;
                    weight_sum += it->second;
                }
            }
            if (matched == 0) continue;
            game::BuildMatch m;
            m.hero = hero.hero;
            m.matched = matched;
            m.ten_win_runs = b.ten_win_runs;
            m.rate_bps = b.rate_bps;
            m.score = weight_sum * 1000 + b.rate_bps / 100 + b.ten_win_runs;
            for (int ref : b.refs)
                if (ref >= 0 && ref < (int)g_guids.size()) m.card_guids.push_back(g_guids[ref]);
            out.push_back(std::move(m));
        }
    }
    std::sort(out.begin(), out.end(),
              [](const game::BuildMatch& a, const game::BuildMatch& b) { return a.score > b.score; });
    if ((int)out.size() > limit) out.resize(limit);
    return out;
}

}  // namespace catalog
