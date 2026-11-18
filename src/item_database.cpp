#include "item_database.h"
#include "vpk_manager.h"
#include <kvpp/KV1.h>
#include <algorithm>
#include <cctype>
#include <functional>
#include <sstream>
#include <unordered_map>

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

static std::string GuessSwatch(const std::string& text)
{
    const std::string t = ToLower(text);
    struct Hint { const char* word; const char* hex; };
    static const Hint hints[] = {
        { "blue",    "#3AA0FF" }, { "azure",   "#3AA0FF" }, { "aqua",   "#3AD6D6" },
        { "red",     "#FF4D4D" }, { "crimson", "#D62B3C" }, { "ruby",   "#D62B3C" },
        { "green",   "#3FCB6B" }, { "emerald", "#2FAF6A" }, { "jade",   "#2FAF6A" },
        { "purple",  "#A96BFF" }, { "violet",  "#A96BFF" }, { "pink",   "#FF6BC1" },
        { "gold",    "#FFC733" }, { "amber",   "#FFB24C" }, { "yellow", "#FFD84C" },
        { "orange",  "#FF8A3D" }, { "white",   "#E8ECF5" }, { "black",  "#22242E" },
        { "dark",    "#3A3D4C" }, { "shadow",  "#5A4B7A" }, { "ice",    "#8FD8FF" },
        { "fire",    "#FF6A2B" }, { "frost",   "#9BE0FF" }, { "void",   "#7A5BD6" },
    };
    for (const auto& h : hints) if (t.find(h.word) != std::string::npos) return h.hex;
    return "";
}

std::string ItemDatabase::deriveDisplayName(const std::string& internal) {
    std::string s = internal;
    const std::string p1 = "npc_dota_hero_";
    const std::string p2 = "npc_dota_";
    if (s.rfind(p1, 0) == 0) s = s.substr(p1.size());
    else if (s.rfind(p2, 0) == 0) s = s.substr(p2.size());

    std::string out;
    bool cap = true;
    for (char c : s) {
        if (c == '_') { out += ' '; cap = true; }
        else if (cap) { out += (char)std::toupper(c); cap = false; }
        else out += c;
    }
    return out;
}

void ItemDatabase::categorizeItem(ItemInfo& item) {
    std::string c = ToLower(item.name + " " + item.prefab + " " + item.rarity);
    if (c.find("arcana")   != std::string::npos) item.isArcana   = true;
    if (c.find("immortal") != std::string::npos) item.isImmortal = true;
    if (c.find("persona")  != std::string::npos) item.isPersona  = true;
}

bool ItemDatabase::loadFromVPK(const fs::path& vpkPath, std::string* logOut) {
    auto content = VPKManager::readFileFromVPK(vpkPath, "scripts/items/items_game.txt");
    if (!content) return false;
    rawContent = std::move(*content);

    try {
        kvpp::KV1 kv1(rawContent);
        const auto& root = kv1["items_game"];
        if (root.isInvalid()) return false;

        const auto& itemsNode = root["items"];
        if (itemsNode.isInvalid()) return false;

        const auto& prefabsNode = root["prefabs"];
        const bool havePrefabs = !prefabsNode.isInvalid();

        std::function<std::string(const std::string&, int)> prefabSlot =
            [&](const std::string& prefabList, int depth) -> std::string {
                if (!havePrefabs || prefabList.empty() || depth > 6) return "";
                std::istringstream iss(prefabList);
                std::string tok;
                while (iss >> tok) {
                    const auto& p = prefabsNode[tok];
                    if (p.isInvalid()) continue;
                    std::string s(p["item_slot"].getValue());
                    if (!s.empty()) return s;
                    std::string up(p["prefab"].getValue());
                    if (!up.empty()) {
                        s = prefabSlot(up, depth + 1);
                        if (!s.empty()) return s;
                    }
                }
                return "";
            };

        for (const auto& itemNode : itemsNode.getChildren()) {
            ItemInfo info;
            info.id             = std::string(itemNode.getKey());
            info.name           = std::string(itemNode["name"].getValue());
            info.prefab         = std::string(itemNode["prefab"].getValue());
            info.slot           = std::string(itemNode["item_slot"].getValue());
            info.rarity         = std::string(itemNode["item_rarity"].getValue());
            info.imageInventory = std::string(itemNode["image_inventory"].getValue());

            if (info.slot.empty() && !info.prefab.empty())
                info.slot = prefabSlot(info.prefab, 0);

            const auto& usedBy = itemNode["used_by_heroes"];
            if (!usedBy.isInvalid()) {
                std::string dv = std::string(usedBy.getValue());
                if (!dv.empty() && dv != "0" && dv.find("npc_dota") != std::string::npos)
                    info.heroes.push_back(dv);
                for (const auto& h : usedBy.getChildren()) {
                    std::string hk = std::string(h.getKey());
                    if (hk.find("npc_dota") != std::string::npos) {
                        if (std::find(info.heroes.begin(), info.heroes.end(), hk) == info.heroes.end())
                            info.heroes.push_back(hk);
                    }
                }
            }

            {
                const auto& vis = itemNode["visuals"];
                const auto& st  = (!vis.isInvalid() && !vis["styles"].isInvalid())
                                  ? vis["styles"] : itemNode["styles"];
                if (!st.isInvalid()) {
                    for (const auto& s : st.getChildren()) {
                        const std::string sk(s.getKey());
                        if (sk.empty()) continue;
                        bool digits = true;
                        for (char c : sk) if (!std::isdigit((unsigned char)c)) { digits = false; break; }
                        if (!digits) continue;

                        StyleInfo si;
                        si.id     = std::atoi(sk.c_str());
                        si.token  = std::string(s["name"].getValue());
                        si.model  = std::string(s["model_player"].getValue());
                        if (si.model.empty()) si.model = std::string(s["skin"].getValue());
                        si.name   = si.token;
                        si.swatch = GuessSwatch(si.token + " " + si.model);
                        info.styles.push_back(std::move(si));
                    }
                    std::sort(info.styles.begin(), info.styles.end(),
                        [](const StyleInfo& a, const StyleInfo& b) { return a.id < b.id; });
                    info.hasStyleChoice = info.styles.size() > 1;
                }
            }

            categorizeItem(info);

            if (info.heroes.empty()) {
                std::string l = ToLower(info.prefab + " " + info.slot);
                if (l.find("courier")   != std::string::npos ||
                    l.find("ward")      != std::string::npos ||
                    l.find("terrain")   != std::string::npos ||
                    l.find("weather")   != std::string::npos ||
                    l.find("music")     != std::string::npos ||
                    l.find("announcer") != std::string::npos ||
                    l.find("effect")    != std::string::npos ||
                    l.find("bundle")    != std::string::npos ||
                    l.find("emblem")    != std::string::npos ||
                    l.find("versus")    != std::string::npos ||
                    l.find("radar")     != std::string::npos ||
                    l.find("creep")     != std::string::npos ||
                    l.find("tower")     != std::string::npos ||
                    l.find("roshan")    != std::string::npos) {
                    info.isWorld = true;
                }
            }

            items.push_back(std::move(info));
        }

        bool anyStyle = false;
        for (const auto& it : items) if (!it.styles.empty()) { anyStyle = true; break; }
        if (anyStyle) {
            std::unordered_map<std::string, std::string> loc;
            for (const char* p : { "resource/localization/dota_english.txt",
                                   "dota/resource/localization/dota_english.txt",
                                   "resource/localization/dota_english.txt" }) {
                auto raw = VPKManager::readFileFromVPK(vpkPath, p);
                if (!raw || raw->size() < 64) continue;
                kvpp::KV1 lkv(*raw);
                const auto& tokens = lkv["lang"]["Tokens"];
                if (tokens.isInvalid()) break;
                loc.reserve(tokens.getChildCount() + 16);
                for (const auto& t : tokens.getChildren())
                    loc.emplace(std::string(t.getKey()), std::string(t.getValue()));
                break;
            }
            for (auto& it : items) {
                for (auto& s : it.styles) {
                    std::string key = s.token;
                    if (!key.empty() && key[0] == '#') key.erase(0, 1);
                    auto f = loc.find(key);
                    if (f != loc.end() && !f->second.empty()) s.name = f->second;
                    else if (s.name.empty() || s.name[0] == '#') s.name = "Style " + std::to_string(s.id + 1);
                    if (s.swatch.empty()) s.swatch = GuessSwatch(s.name + " " + s.model);
                }
            }
        }

        buildHeroIndex();

        if (logOut) {
            *logOut = "Loaded " + std::to_string(items.size()) + " items, "
                    + std::to_string(heroes.size()) + " heroes, "
                    + std::to_string(worldItemIndices.size()) + " world items";
        }
        return true;
    } catch (...) {
        return false;
    }
}

void ItemDatabase::buildHeroIndex() {
    for (size_t i = 0; i < items.size(); i++) {
        for (const auto& hName : items[i].heroes) {
            auto it = heroIndex.find(hName);
            if (it == heroIndex.end()) {
                HeroInfo h;
                h.internalName = hName;
                h.displayName  = deriveDisplayName(hName);
                heroIndex[hName] = heroes.size();
                heroes.push_back(std::move(h));
            }
            size_t idx = heroIndex[hName];
            heroes[idx].itemIndices.push_back(i);
            if (items[i].isArcana)   heroes[idx].arcanaCount++;
            if (items[i].isImmortal) heroes[idx].immortalCount++;
        }
        if (items[i].isWorld) worldItemIndices.push_back(i);
    }

    std::sort(heroes.begin(), heroes.end(),
        [](const HeroInfo& a, const HeroInfo& b) { return a.displayName < b.displayName; });

    heroIndex.clear();
    for (size_t i = 0; i < heroes.size(); i++)
        heroIndex[heroes[i].internalName] = i;
}

std::vector<std::string> ItemDatabase::heroSlots(size_t heroIdx) const {
    std::vector<std::string> out;
    if (heroIdx >= heroes.size()) return out;
    for (size_t idx : heroes[heroIdx].itemIndices) {
        const std::string& s = items[idx].slot;
        if (s.empty()) continue;
        if (std::find(out.begin(), out.end(), s) == out.end()) out.push_back(s);
    }
    std::sort(out.begin(), out.end());
    return out;
}
