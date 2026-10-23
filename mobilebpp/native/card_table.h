#pragma once

// Static card metadata generated from the game's GameData.db.
// Lets the mod resolve GUID -> name/hero/tags without calling GetCardMap().
struct StaticCard {
    const char* guid;
    const char* name;
    const char* title_key;
    const char* heroes;
    const char* tags;
    const char* tier;
    const char* size;
    const char* name_zh;
    const char* art_key;
};

const StaticCard* FindStaticCard(const char* guid);
int StaticCardCount();
const StaticCard* AllStaticCards(int* count);
