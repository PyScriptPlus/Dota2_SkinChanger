#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>

namespace fs = std::filesystem;


struct StyleInfo {
    int         id = 0;
    std::string name;
    std::string token;
    std::string model;
    std::string swatch;
};

struct ItemInfo {
    std::string id;
    std::string name;
    std::string prefab;
    std::string slot;
    std::string rarity;
    std::string imageInventory;
    std::vector<std::string> heroes;
    std::vector<StyleInfo>   styles;
    bool isArcana = false;
    bool isImmortal = false;
    bool isPersona = false;
    bool isWorld = false;              // courier/ward/terrain/...
    bool hasStyleChoice = false;
};

struct HeroInfo {
    std::string internalName;           // npc_dota_hero_drow_ranger
    std::string displayName;            // Drow Ranger
    std::vector<size_t> itemIndices;
    size_t arcanaCount = 0;
    size_t immortalCount = 0;
};

class ItemDatabase {
public:
    bool loadFromVPK(const fs::path& vpkPath, std::string* logOut = nullptr);

    const std::vector<HeroInfo>& getHeroes() const { return heroes; }
    const std::vector<ItemInfo>& getItems() const { return items; }
    const std::vector<size_t>& getWorldItems() const { return worldItemIndices; }

    std::vector<std::string> heroSlots(size_t heroIdx) const;

    std::string getRawContent() const { return rawContent; }

private:
    std::vector<ItemInfo> items;
    std::vector<HeroInfo> heroes;
    std::vector<size_t> worldItemIndices;
    std::map<std::string, size_t> heroIndex;
    std::string rawContent;

    void categorizeItem(ItemInfo& item);
    void buildHeroIndex();
    std::string deriveDisplayName(const std::string& internal);
};
