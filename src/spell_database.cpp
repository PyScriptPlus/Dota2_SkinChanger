#include "spell_database.h"

#include "vpk_manager.h"

#include <algorithm>
#include <cctype>

namespace {

const char* kIconDir = "panorama/images/spellicons";

const char* kVariants[] = {
    "_arcana_alt1", "_arcana_alt2", "_arcana_alt3", "_arcana",
    "_persona_alt1", "_persona",
    "_ti10", "_ti9", "_ti8", "_ti7",
    "_immortal", "_alt1", "_alt2", "_alt3", "_alt",
    "_v2", "_ex",
};

bool endsWith(const std::string& s, const std::string& suf) {
    return s.size() >= suf.size() && s.compare(s.size() - suf.size(), suf.size(), suf) == 0;
}

bool heroPrefix(const std::string& base, const std::string& hero) {
    if (hero.empty() || base.size() <= hero.size()) return false;
    if (base.compare(0, hero.size(), hero) != 0) return false;
    return base[hero.size()] == '_';
}

std::string lower(std::string s) {
    for (auto& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

}

std::string SpellDatabase::prettyName(const std::string& base, const std::string& hero) {
    std::string s = base;
    if (!hero.empty() && s.size() > hero.size() && s.compare(0, hero.size(), hero) == 0)
        s = s.substr(hero.size());
    while (!s.empty() && s.front() == '_') s.erase(s.begin());

    if (s.empty()) return "Base";

    std::string out;
    out.reserve(s.size());
    bool cap = true;
    for (char c : s) {
        if (c == '_') { out.push_back(' '); cap = true; continue; }
        out.push_back(cap ? (char)std::toupper((unsigned char)c) : c);
        cap = false;
    }
    return out;
}

void SpellDatabase::build(const std::vector<std::string>& heroNames) {
    m_icons.clear();
    m_byHero.clear();
    m_byPath.clear();
    m_groups.clear();
    m_loaded = false;

    std::vector<std::string> files = VPKManager::listFiles(kIconDir, true);
    if (files.empty()) return;

    std::vector<std::string> heroes;
    heroes.reserve(heroNames.size());
    for (const auto& h : heroNames) {
        std::string s = h;
        const std::string pre = "npc_dota_hero_";
        if (s.size() > pre.size() && s.compare(0, pre.size(), pre) == 0) s = s.substr(pre.size());
        if (!s.empty()) heroes.push_back(s);
    }
    std::sort(heroes.begin(), heroes.end(),
        [](const std::string& a, const std::string& b) { return a.size() > b.size(); });

    const size_t dirLen = std::string(kIconDir).size();

    for (const auto& full : files) {
        if (full.size() <= dirLen + 1) continue;
        if (full.compare(0, dirLen, kIconDir) != 0) continue;
        if (full[dirLen] != '/') continue;

        std::string rel = full.substr(dirLen + 1);
        if (!endsWith(rel, ".vtex_c")) continue;
        std::string stem = rel.substr(0, rel.size() - 7);   // ".vtex_c"
        if (endsWith(stem, "_png")) stem.resize(stem.size() - 4);
        else if (endsWith(stem, "_jpg")) stem.resize(stem.size() - 4);
        if (stem.empty()) continue;

        SpellIcon ic;
        ic.path = full;

        const size_t slash = stem.rfind('/');
        if (slash != std::string::npos) {
            ic.hero  = stem.substr(0, slash);
            ic.base  = stem;
            ic.target = false;
            ic.label = prettyName(stem.substr(slash + 1), std::string());
        }
        else {
            ic.base = stem;

            for (const char* v : kVariants) {
                const std::string suf = v;
                if (!endsWith(ic.base, suf)) continue;
                if (ic.base.size() <= suf.size() + 3) break;
                ic.base.resize(ic.base.size() - suf.size());
                ic.variant = suf.substr(1);
                break;
            }

            for (const auto& h : heroes) {
                if (heroPrefix(ic.base, h)) { ic.hero = h; break; }
            }
            ic.target = true;
            ic.label  = prettyName(ic.base, ic.hero);
        }

        m_icons.push_back(std::move(ic));
    }

    if (m_icons.empty()) return;

    std::sort(m_icons.begin(), m_icons.end(), [](const SpellIcon& a, const SpellIcon& b) {
        if (a.hero != b.hero) return a.hero < b.hero;
        if (a.base != b.base) return a.base < b.base;
        return a.variant < b.variant;
    });

    m_byPath.reserve(m_icons.size() * 2);
    for (int i = 0; i < (int)m_icons.size(); ++i) {
        const SpellIcon& ic = m_icons[(size_t)i];
        m_byPath.emplace(ic.path, i);
        if (!ic.hero.empty()) m_byHero[ic.hero].push_back(i);
    }

    m_groups.reserve(m_byHero.size());
    for (const auto& kv : m_byHero) m_groups.push_back(kv.first);
    std::sort(m_groups.begin(), m_groups.end());

    m_loaded = true;
}

int SpellDatabase::targetCount() const {
    int n = 0;
    for (const auto& ic : m_icons) if (ic.target) ++n;
    return n;
}

const SpellIcon* SpellDatabase::at(int idx) const {
    if (idx < 0 || idx >= (int)m_icons.size()) return nullptr;
    return &m_icons[(size_t)idx];
}

int SpellDatabase::indexOf(const std::string& path) const {
    auto it = m_byPath.find(path);
    return (it == m_byPath.end()) ? -1 : it->second;
}

std::vector<int> SpellDatabase::forHero(const std::string& hero, bool targetsOnly) const {
    std::vector<int> out;
    auto it = m_byHero.find(hero);
    if (it == m_byHero.end()) return out;
    out.reserve(it->second.size());
    for (int i : it->second) {
        if (targetsOnly && !m_icons[(size_t)i].target) continue;
        out.push_back(i);
    }
    return out;
}

std::vector<std::string> SpellDatabase::heroGroups() const {
    return m_groups;
}
