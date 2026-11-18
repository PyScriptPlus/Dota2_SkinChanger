#pragma once

#include <string>
#include <unordered_map>
#include <vector>


struct SpellIcon {
    std::string path;  
    std::string base;
    std::string variant;
    std::string hero;
    std::string label;
    bool target = true;
};

class SpellDatabase {
public:
    void build(const std::vector<std::string>& heroNames);

    bool loaded() const { return m_loaded; }
    int  count() const { return (int)m_icons.size(); }
    int  targetCount() const;

    const std::vector<SpellIcon>& icons() const { return m_icons; }
    const SpellIcon* at(int idx) const;

    int indexOf(const std::string& path) const;
    std::vector<int> forHero(const std::string& hero, bool targetsOnly) const;
    std::vector<std::string> heroGroups() const;

    // "drow_ranger_frost_arrows" + "drow_ranger" -> "Frost Arrows"
    static std::string prettyName(const std::string& base, const std::string& hero);

private:
    std::vector<SpellIcon> m_icons;
    std::unordered_map<std::string, std::vector<int>> m_byHero;
    std::unordered_map<std::string, int> m_byPath;
    std::vector<std::string> m_groups;
    bool m_loaded = false;
};
