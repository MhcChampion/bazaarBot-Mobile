#pragma once
#include "bpp_common.h"
#include "game_api.h"

namespace catalog {

struct Row {
    int card_ref;   // index into the build's card refs
    int slot;
    int tier;
    int enchant;
    int size;
};

struct Build {
    std::vector<int> refs;   // indices into the global card guid table
    std::vector<Row> rows;
    int ten_win_runs = 0;
    int rate_bps = 0;
    int score = 0;
};

struct HeroBuilds {
    std::string hero;
    std::vector<Build> builds;
};

const std::vector<std::string>& CardGuids();
const std::vector<HeroBuilds>& All();
std::string Title();  // window/source info for the UI

// How many ten-win builds contain this card template.
int UsageCount(const std::string& guid);

std::vector<game::BuildMatch> Match(const std::vector<game::CardInfo>& board,
                                    const std::vector<game::CardInfo>& stash,
                                    const std::vector<game::CardInfo>& shop, int limit);

}  // namespace catalog
