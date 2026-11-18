#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "mod_generator.h"
#include <fstream>
#include <sstream>
#include <system_error>
#include <cstring>
#include <cstdint>
#include <cstdio>
#include <iomanip>
#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <vector>
#include <string>
#include <unordered_map>
#include <windows.h>
#include "steam_detector.h"
#include "vpk_manager.h"
#include "diag_log.h"
#include <vpkpp/format/VPK.h>
#include <vpkpp/vpkpp.h>

namespace {
    inline uint32_t rol(uint32_t v, int b) { return (v << b) | (v >> (32 - b)); }

    std::string sha1_hex(const std::string& data) {
        uint32_t h0 = 0x67452301, h1 = 0xEFCDAB89, h2 = 0x98BADCFE, h3 = 0x10325476, h4 = 0xC3D2E1F0;
        std::string msg = data;
        uint64_t bitLen = (uint64_t)msg.size() * 8;
        msg += (char)0x80;
        while (msg.size() % 64 != 56) msg += (char)0x00;
        for (int i = 7; i >= 0; --i) msg += (char)((bitLen >> (i * 8)) & 0xFF);
        for (size_t c = 0; c < msg.size(); c += 64) {
            uint32_t w[80];
            for (int i = 0; i < 16; ++i)
                w[i] = ((uint32_t)(uint8_t)msg[c + i * 4] << 24) | ((uint32_t)(uint8_t)msg[c + i * 4 + 1] << 16) | ((uint32_t)(uint8_t)msg[c + i * 4 + 2] << 8) | (uint8_t)msg[c + i * 4 + 3];
            for (int i = 16; i < 80; ++i) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
            uint32_t a = h0, b = h1, cc = h2, d = h3, e = h4;
            for (int i = 0; i < 80; ++i) {
                uint32_t f, k;
                if (i < 20) { f = (b & cc) | ((~b) & d); k = 0x5A827999; }
                else if (i < 40) { f = b ^ cc ^ d; k = 0x6ED9EBA1; }
                else if (i < 60) { f = (b & cc) | (b & d) | (cc & d); k = 0x8F1BBCDC; }
                else { f = b ^ cc ^ d; k = 0xCA62C1D6; }
                uint32_t t = rol(a, 5) + f + e + k + w[i];
                e = d; d = cc; cc = rol(b, 30); b = a; a = t;
            }
            h0 += a; h1 += b; h2 += cc; h3 += d; h4 += e;
        }
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(8) << h0 << std::setw(8) << h1 << std::setw(8) << h2 << std::setw(8) << h3 << std::setw(8) << h4;
        return ss.str();
    }

    uint32_t crc32_compute(const std::string& data) {
        static uint32_t table[256]; static bool init = false;
        if (!init) {
            for (uint32_t i = 0; i < 256; i++) {
                uint32_t c = i;
                for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320 ^ (c >> 1)) : (c >> 1);
                table[i] = c;
            }
            init = true;
        }
        uint32_t crc = 0xFFFFFFFF;
        for (unsigned char b : data) crc = table[(crc ^ b) & 0xFF] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFF;
    }

    void debugLog(const std::string& msg) {
        OutputDebugStringA(("[ModGen] " + msg + "\n").c_str());
        DiagLog::add("[gen] " + msg);
    }
}

namespace KV {

    struct Node {
        std::string key;
        std::string value;
        std::vector<Node> children;
        bool block = false;

        Node() = default;
        explicit Node(std::string k) : key(std::move(k)) {}
    };

    class Parser {
    public:
        explicit Parser(const std::string& s) : s_(s), p_(0) {}

        bool parse(Node& root) {
            p_ = 0;

            for (;;) {
                skipWS();
                if (p_ < s_.size() && s_[p_] == '#') {
                    size_t start = p_;
                    while (p_ < s_.size() && s_[p_] != '\n') ++p_;
                    std::string line = s_.substr(start, p_ - start);
                    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
                    directives_.push_back(line);
                    continue;
                }
                break;
            }

            skipWS();
            if (!readKey(root.key)) return false;
            skipWS();
            if (p_ >= s_.size() || s_[p_] != '{') return false;
            if (!parseBody(root)) return false;
            root.block = true;
            return true;
        }

        const std::vector<std::string>& directives() const { return directives_; }

    private:
        const std::string& s_;
        size_t p_;
        std::vector<std::string> directives_;

        void skipWS() {
            while (p_ < s_.size()) {
                char c = s_[p_];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n') { ++p_; continue; }
                if (c == '/' && p_ + 1 < s_.size() && s_[p_ + 1] == '/') {
                    while (p_ < s_.size() && s_[p_] != '\n') ++p_;
                    continue;
                }
                break;
            }
        }

        bool readQuoted(std::string& out) {
            out.clear();
            if (p_ >= s_.size() || s_[p_] != '"') return false;
            ++p_;
            while (p_ < s_.size() && s_[p_] != '"') {
                char c = s_[p_];
                if (c == '\\' && p_ + 1 < s_.size()) {
                    char e = s_[p_ + 1];
                    switch (e) {
                    case 'n':  out += '\n'; break;
                    case 't':  out += '\t'; break;
                    case 'r':  out += '\r'; break;
                    case '\\': out += '\\'; break;
                    case '"':  out += '"';  break;
                    default:   out += e;    break;
                    }
                    p_ += 2;
                }
                else { out += c; ++p_; }
            }
            if (p_ < s_.size()) ++p_;
            return true;
        }

        bool readBare(std::string& out) {
            out.clear();
            while (p_ < s_.size()) {
                char c = s_[p_];
                if (c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
                    c == '{' || c == '}' || c == '"') break;
                out += c; ++p_;
            }
            return !out.empty();
        }

        bool readKey(std::string& out) {
            if (p_ < s_.size() && s_[p_] == '"') return readQuoted(out);
            return readBare(out);
        }

        bool readValue(std::string& out) {
            if (p_ < s_.size() && s_[p_] == '"') return readQuoted(out);
            return readBare(out);
        }

        bool parseBody(Node& n) {
            ++p_;
            for (;;) {
                skipWS();
                if (p_ >= s_.size()) return false;
                if (s_[p_] == '}') { ++p_; return true; }

                Node child;
                if (!readKey(child.key)) return false;
                skipWS();
                if (p_ >= s_.size()) return false;

                if (s_[p_] == '{') {
                    child.block = true;
                    if (!parseBody(child)) return false;
                }
                else {
                    if (!readValue(child.value)) return false;
                }
                n.children.push_back(std::move(child));
            }
        }
    };

inline Node* findChild(Node& n, const std::string& k) {
    for (auto& c : n.children) if (c.key == k) return &c;
    return nullptr;
}
inline const Node* findChild(const Node& n, const std::string& k) {
    for (const auto& c : n.children) if (c.key == k) return &c;
    return nullptr;
}
inline std::string scalar(const Node& n, const std::string& k) {
    const Node* c = findChild(n, k);
    return (c && !c->block) ? c->value : std::string();
}
inline void setScalar(Node& n, const std::string& k, const std::string& v) {
    Node* c = findChild(n, k);
    if (c) { c->value = v; c->children.clear(); c->block = false; }
    else { Node x(k); x.value = v; n.children.push_back(std::move(x)); }
}

inline bool isRepeatableKey(const std::string& k) {
    return k == "asset_modifier" || k == "particle" ||
        k == "additional_wearable" || k == "sound";
}

inline void mergeInto(Node& acc, const Node& over) {
    for (const auto& c : over.children) {
        if (isRepeatableKey(c.key)) { acc.children.push_back(c); continue; }

        size_t idx = (size_t)-1;
        for (size_t i = 0; i < acc.children.size(); ++i) {
            if (acc.children[i].key == c.key) { idx = i; break; }
        }
        if (idx == (size_t)-1) { acc.children.push_back(c); continue; }

        if (acc.children[idx].block && c.block) mergeInto(acc.children[idx], c);
        else                                    acc.children[idx] = c;
    }
}

inline void resolvePrefabsInto(Node& acc, const Node& item, const Node& prefabs, int depth) {
    if (depth > 8) return;
    std::string pf = scalar(item, "prefab");
    if (!pf.empty()) {
        std::istringstream iss(pf);
        std::string name;
        while (iss >> name) {
            if (name == "default_item" || name == "wearable") continue;
            const Node* p = findChild(prefabs, name);
            if (p && p->block) resolvePrefabsInto(acc, *p, prefabs, depth + 1);
        }
    }
    mergeInto(acc, item);
}

inline Node resolvePrefabs(const Node& item, const Node& prefabs) {
    Node acc(item.key);
    acc.block = true;
    resolvePrefabsInto(acc, item, prefabs, 0);
    return acc;
}

inline bool isModifierKey(const std::string& k) {
    static const std::string base = "asset_modifier";
    if (k.size() < base.size() || k.compare(0, base.size(), base) != 0) return false;
    for (size_t i = base.size(); i < k.size(); ++i)
        if (!std::isdigit((unsigned char)k[i])) return false;
    return true;
}

inline bool digitsOnly(const std::string& s) {
    if (s.empty()) return false;
    for (char c : s) if (!std::isdigit((unsigned char)c)) return false;
    return true;
}

inline bool styleMatches(const std::string& v, const std::string& sid) {
    std::string tok;
    for (size_t i = 0; i <= v.size(); ++i) {
        const char c = (i < v.size()) ? v[i] : ' ';
        if (std::isdigit((unsigned char)c)) tok += c;
        else if (!tok.empty()) { if (tok == sid) return true; tok.clear(); }
    }
    return false;
}

inline void removeChild(Node& n, const std::string& key) {
    n.children.erase(
        std::remove_if(n.children.begin(), n.children.end(),
            [&](const Node& c) { return c.key == key; }),
        n.children.end());
}

inline void flattenStyle(Node& item, int styleId)
{
    Node* vis = findChild(item, "visuals");
    const bool hasVis = (vis && vis->block);

    std::vector<std::string> ids;
    bool stylesOnRoot = false;
    {
        const Node* st = nullptr;
        if (hasVis) {
            st = findChild(*vis, "styles");
            if (!st || !st->block) st = nullptr;
        }
        if (!st) {
            const Node* r = findChild(item, "styles");
            if (r && r->block) { st = r; stylesOnRoot = true; }
        }
        if (st)
            for (const auto& s : st->children)
                if (digitsOnly(s.key)) ids.push_back(s.key);
    }
    std::sort(ids.begin(), ids.end(),
        [](const std::string& a, const std::string& b) { return std::stoi(a) < std::stoi(b); });

    const std::string sid = (styleId >= 0)
        ? std::to_string(styleId)
        : (ids.empty() ? std::string("0") : ids.front());

    if (hasVis) {
        for (auto it = vis->children.begin(); it != vis->children.end(); ) {
            if (!it->block || !isModifierKey(it->key)) { ++it; continue; }
            const Node* st = findChild(*it, "style");
            if (st) {
                if (!styleMatches(st->value, sid)) { it = vis->children.erase(it); continue; }
                removeChild(*it, "style");
            }
            removeChild(*it, "apply_when_equipped_in_ability_effects_slot");
            ++it;
        }
    }

    std::vector<Node> chosen;
    {
        const Node* st = nullptr;
        if (hasVis) {
            st = findChild(*vis, "styles");
            if (!st || !st->block) st = nullptr;
        }
        if (!st) {
            const Node* r = findChild(item, "styles");
            if (r && r->block) { st = r; stylesOnRoot = true; }
        }
        if (st)
            for (const auto& s : st->children)
                if (s.key == sid && s.block) { chosen = s.children; break; }
    }

    for (const auto& kv : chosen) {
        if (kv.key == "name" || kv.key == "icon" ||
            kv.key == "alternate_icon" || kv.key == "unlock") continue;

        if (kv.key == "model_player" && !kv.block) {
            setScalar(item, "model_player", kv.value);
        }
        else if (isModifierKey(kv.key) && kv.block) {
            if (hasVis) {
                Node c = kv;
                c.key = "asset_modifier";
                vis->children.push_back(std::move(c));
            }
        }
        else if (!kv.block && hasVis) {
            setScalar(*vis, kv.key, kv.value);   
        }
    }

    if (hasVis) removeChild(*vis, "styles");
    if (stylesOnRoot) removeChild(item, "styles");
}

inline Node buildOverride(const Node& def, const Node& src, const std::set<std::string>& keep) {
    Node out(def.key);
    out.block = true;
    out.children.reserve(def.children.size() + src.children.size());

    for (const auto& c : def.children) {
        if (keep.count(c.key)) out.children.push_back(c);
    }
    for (const auto& c : src.children) {
        if (keep.count(c.key)) continue;
        out.children.push_back(c);
    }
    return out;
}

inline void escapeInto(const std::string& s, std::string& out) {
    for (char c : s) {
        switch (c) {
        case '\\': out += "\\\\"; break;
        case '"':  out += "\\\""; break;
        case '\n': out += "\\n";  break;
        case '\t': out += "\\t";  break;
        case '\r': out += "\\r";  break;
        default:   out += c;      break;
        }
    }
}

    inline void writeNode(const Node& n, std::string& out, int depth) {
        std::string pad((size_t)depth, '\t');
        out += pad;

        if (!n.key.empty() && n.key[0] == '#') out += n.key;
        else { out += '"'; escapeInto(n.key, out); out += '"'; }

        if (n.block) {
            out += "\n"; out += pad; out += "{\n";
            for (const auto& c : n.children) writeNode(c, out, depth + 1);
            out += pad; out += "}\n";
        }
        else {
            out += "\t\t\"";
            escapeInto(n.value, out);
            out += "\"\n";
        }
    }
}

static const std::set<std::string> heroIdentityKeys = {
    "id", "name", "prefab", "item_slot", "used_by_heroes", "baseitem"
};

static const std::set<std::string> globalIdentityKeys = {
    "id", "name", "prefab", "item_slot", "baseitem",
    "item_class", "player_loadout"
};

std::string ModGenerator::buildModifiedItemsGame(
    const std::string& originalContent,
    const AppliedMap& applied,
    const ItemDatabase& db,
    BuildReport* report)
{
    try {
        std::string freshContent;
        {
            auto steamPath = SteamDetector::detectSteam();
            if (steamPath) {
                auto dotaPath = SteamDetector::detectDota(*steamPath);
                if (dotaPath) {
                    auto vpkPath = VPKManager::findGameVPK(*dotaPath);
                    if (vpkPath) {
                        auto extracted = VPKManager::readFileFromVPK(*vpkPath, "scripts/items/items_game.txt");
                        if (extracted) freshContent = *extracted;
                    }
                }
            }
        }

        const std::string& raw = freshContent.empty() ? originalContent : freshContent;
        if (raw.empty()) { debugLog("ERROR: source content is empty"); return ""; }

        std::string text = raw;
        if (text.size() >= 3 &&
            (uint8_t)text[0] == 0xEF && (uint8_t)text[1] == 0xBB && (uint8_t)text[2] == 0xBF) {
            text = text.substr(3);
            debugLog("BOM stripped");
        }
        debugLog("source size: " + std::to_string(text.size()));

        KV::Node root;
        std::vector<std::string> directives;
        {
            KV::Parser parser(text);
            if (!parser.parse(root)) { debugLog("ERROR: KV parse failed"); return ""; }
            directives = parser.directives();
        }
        if (root.key != "items_game") {
            debugLog("ERROR: root key is '" + root.key + "', expected 'items_game'");
            return "";
        }

        KV::Node* items = KV::findChild(root, "items");
        if (!items || !items->block) { debugLog("ERROR: 'items' block missing"); return ""; }

        KV::Node emptyPrefabs; emptyPrefabs.block = true;
        KV::Node* prefabs = KV::findChild(root, "prefabs");
        const KV::Node& prefabRoot = (prefabs && prefabs->block) ? *prefabs : emptyPrefabs;

        debugLog("parsed: " + std::to_string(items->children.size()) + " items, " +
            std::to_string(prefabRoot.children.size()) + " prefabs");

        std::unordered_map<std::string, size_t> byName, byKey;
        byName.reserve(items->children.size() * 2);
        byKey.reserve(items->children.size() * 2);
        for (size_t i = 0; i < items->children.size(); ++i) {
            const auto& n = items->children[i];
            byKey.emplace(n.key, i);
            std::string nm = KV::scalar(n, "name");
            if (!nm.empty()) byName.emplace(nm, i);
        }

        auto isDefaultItem = [](const KV::Node& n) -> bool {
            if (KV::scalar(n, "baseitem") == "1") return true;

            std::string pf = KV::scalar(n, "prefab");
            if (!pf.empty()) {
                std::istringstream iss(pf);
                std::string t;
                while (iss >> t) if (t == "default_item") return true;
            }

            std::string nm = KV::scalar(n, "name");
            std::transform(nm.begin(), nm.end(), nm.begin(),
                [](unsigned char c) { return (char)std::tolower(c); });
            return nm.rfind("default", 0) == 0;
            };

        auto belongsToHero = [](const KV::Node& n, const std::string& hero) -> bool {
            const KV::Node* ub = KV::findChild(n, "used_by_heroes");
            if (!ub) return false;
            if (ub->block) {
                for (const auto& h : ub->children) if (h.key == hero) return true;
                return false;
            }
            return ub->value == hero;
            };

        auto isGlobalItem = [](const KV::Node& n) -> bool {
            return KV::findChild(n, "used_by_heroes") == nullptr;
            };

        auto findSource = [&](const ItemInfo& it) -> long long {
            auto a = byName.find(it.name);
            if (a != byName.end()) return (long long)a->second;
            auto b = byKey.find(it.id);
            if (b != byKey.end()) return (long long)b->second;
            return -1;
            };

        auto normalizeSlot = [](std::string s) -> std::string {
            if (s == "visual" || s == "global") s.clear();
            return s;
            };

        struct Req {
            long long  targetIdx = -1;
            size_t     sourceIdx = 0;
            std::string hero;
            std::string slot;
            int         style = -1;
            std::string itemName;
        };
        std::vector<Req> reqs;

        BuildReport localReport;
        BuildReport& rep = report ? *report : localReport;
        rep = BuildReport{};

        struct Flat {
            std::string hero, slot;
            int itemIdx = -1;
            int style   = -1;
            std::string name;
        };
        std::vector<Flat> flat;

        {
            std::map<std::string, size_t> lastBySlot;

            for (const auto& heroPair : applied) {
                const std::string& hero = heroPair.first;

                for (const auto& pick : heroPair.second) {
                    if (pick.itemIdx < 0 || pick.itemIdx >= (int)db.getItems().size()) {
                        ++rep.failed;
                        continue;
                    }
                    const ItemInfo& it = db.getItems()[pick.itemIdx];

                    Flat f;
                    f.hero    = hero;
                    f.slot    = normalizeSlot(it.slot);
                    f.itemIdx = pick.itemIdx;
                    f.style   = pick.style;
                    f.name    = it.name;

                    const std::string key = hero + "|" +
                        (f.slot.empty() ? ("#" + it.id) : f.slot);

                    auto found = lastBySlot.find(key);
                    if (found == lastBySlot.end()) {
                        lastBySlot[key] = flat.size();
                        flat.push_back(f);
                    }
                    else {
                        debugLog("  shadowed: '" + flat[found->second].name +
                                 "' (slot " + f.slot + ") replaced by '" + f.name + "'");
                        flat[found->second] = f;
                        ++rep.shadowed;
                    }
                }
            }
        }

        debugLog("picks: " + std::to_string(flat.size()) +
                 " (shadowed " + std::to_string(rep.shadowed) + ")");

        std::map<long long, std::string> targetOwner;

        for (const auto& f : flat) {
            const bool world = (f.hero == "__world__");
            const ItemInfo& appliedItem = db.getItems()[f.itemIdx];

            long long srcIdx = findSource(appliedItem);
            if (srcIdx < 0) {
                debugLog("  ERROR: source not found: " + appliedItem.name +
                         " (id=" + appliedItem.id + ")");
                ++rep.failed;
                if (rep.firstError.empty())
                    rep.firstError = "source not found: " + appliedItem.name;
                continue;
            }

            long long defIdx = -1;

            if (world) {
                for (size_t i = 0; i < items->children.size(); ++i) {
                    const auto& n = items->children[i];
                    if (!isDefaultItem(n) || !isGlobalItem(n)) continue;
                    if (!f.slot.empty() && KV::scalar(n, "item_slot") != f.slot) continue;
                    if (targetOwner.count((long long)i)) continue;
                    defIdx = (long long)i;
                    break;
                }
                if (defIdx < 0 && !f.slot.empty()) {
                    for (size_t i = 0; i < items->children.size(); ++i) {
                        const auto& n = items->children[i];
                        if (!isDefaultItem(n) || !isGlobalItem(n)) continue;
                        if (KV::scalar(n, "item_slot") != f.slot) continue;
                        if (targetOwner.count((long long)i)) continue;
                        defIdx = (long long)i;
                        break;
                    }
                }
                if (defIdx < 0) {
                    std::vector<std::string> toks;
                    {
                        std::istringstream iss(appliedItem.prefab);
                        std::string t;
                        while (iss >> t)
                            if (t != "default_item" && t != "wearable") toks.push_back(t);
                    }
                    if (!toks.empty()) {
                        for (size_t i = 0; i < items->children.size() && defIdx < 0; ++i) {
                            const auto& n = items->children[i];
                            if (!isDefaultItem(n) || !isGlobalItem(n)) continue;
                            if (targetOwner.count((long long)i)) continue;
                            const std::string dpf = KV::scalar(n, "prefab");
                            for (const auto& t : toks)
                                if (dpf.find(t) != std::string::npos) { defIdx = (long long)i; break; }
                        }
                    }
                }
                if (defIdx < 0) {
                    debugLog("  WARN: no global default for " + appliedItem.name + " -> skipped");
                    ++rep.failed;
                    if (rep.firstError.empty())
                        rep.firstError = "no global default for " + appliedItem.name;
                    continue;
                }
            }
            else {
                if (!f.slot.empty()) {
                    for (size_t i = 0; i < items->children.size(); ++i) {
                        const auto& n = items->children[i];
                        if (!isDefaultItem(n) || !belongsToHero(n, f.hero)) continue;
                        if (KV::scalar(n, "item_slot") != f.slot) continue;
                        if (targetOwner.count((long long)i)) continue;
                        defIdx = (long long)i;
                        break;
                    }
                }
                if (defIdx < 0) {
                    for (size_t i = 0; i < items->children.size(); ++i) {
                        const auto& n = items->children[i];
                        if (!isDefaultItem(n) || !belongsToHero(n, f.hero)) continue;
                        if ((long long)i == srcIdx) continue;
                        if (targetOwner.count((long long)i)) continue;

                        std::string ms = KV::scalar(n, "item_slot");
                        if (!f.slot.empty() && !ms.empty() && ms != f.slot) continue;

                        std::string pf = KV::scalar(n, "prefab");
                        if (pf.find("arcana") != std::string::npos) continue;
                        if (pf.find("persona") != std::string::npos) continue;

                        defIdx = (long long)i;
                        break;
                    }
                }
                if (defIdx < 0)
                    debugLog("  -> no free default for slot '" + f.slot +
                             "', a new item will be created");
            }

            if (defIdx >= 0) targetOwner[defIdx] = f.slot;

            Req r;
            r.targetIdx = defIdx;
            r.sourceIdx = (size_t)srcIdx;
            r.hero      = f.hero;
            r.slot      = f.slot;
            r.style     = f.style;
            r.itemName  = f.name;
            reqs.push_back(r);

            if (world) ++rep.worldSlots; else ++rep.heroSlots;
        }

        if (reqs.empty()) {
            debugLog("ERROR: nothing to apply");
            if (rep.firstError.empty()) rep.firstError = "nothing to apply";
            return "";
        }

        {
            std::map<long long, std::string> owner;
            std::vector<Req> filtered;
            filtered.reserve(reqs.size());

            for (auto& r : reqs) {
                if (r.targetIdx < 0) { filtered.push_back(r); continue; }

                auto it = owner.find(r.targetIdx);
                if (it == owner.end()) {
                    owner[r.targetIdx] = r.slot;
                    filtered.push_back(r);
                }
                else if (it->second == r.slot) {
                    debugLog("DEDUPE: slot '" + r.slot + "' already queued, skipped");
                }
                else {
                    debugLog("DEDUPE: target #" + std::to_string(r.targetIdx) +
                        " already used by slot '" + it->second +
                        "' -> new item for slot '" + r.slot + "'");
                    r.targetIdx = -1;
                    filtered.push_back(r);
                }
            }
            reqs.swap(filtered);
        }

        debugLog("requests: " + std::to_string(reqs.size()));

        std::map<std::pair<size_t, int>, KV::Node> resolvedCache;
        for (const auto& r : reqs) {
            const auto key = std::make_pair(r.sourceIdx, r.style);
            if (resolvedCache.find(key) != resolvedCache.end()) continue;

            KV::Node node = KV::resolvePrefabs(items->children[r.sourceIdx], prefabRoot);

            KV::flattenStyle(node, r.style);

            resolvedCache.emplace(key, std::move(node));
        }

        long long maxKey = 0;
        for (const auto& n : items->children) {
            try { long long k = std::stoll(n.key); if (k > maxKey) maxKey = k; }
            catch (...) {}
        }

        std::vector<KV::Node> newItems;
        int replaced = 0, created = 0;

        for (const auto& r : reqs) {
            const KV::Node& src = resolvedCache[std::make_pair(r.sourceIdx, r.style)];
            const std::set<std::string>& keep =
                (r.hero == "__world__") ? globalIdentityKeys : heroIdentityKeys;

            if (r.targetIdx >= 0) {
                KV::Node& def = items->children[(size_t)r.targetIdx];
                KV::Node built = KV::buildOverride(def, src, keep);
                def = std::move(built);
                ++replaced;
                debugLog("  override #" + def.key + " (" + KV::scalar(def, "name") + ")" +
                         (r.style >= 0 ? " style " + std::to_string(r.style) : ""));
            }
            else {
                std::string heroShort = r.hero;
                const std::string prefix = "npc_dota_hero_";
                if (heroShort.rfind(prefix, 0) == 0) heroShort = heroShort.substr(prefix.size());

                std::string slot = r.slot;
                if (slot.empty()) slot = KV::scalar(src, "item_slot");

                ++maxKey;
                KV::Node base(std::to_string(maxKey));
                base.block = true;

                std::string newName = "default_" + heroShort;
                if (!slot.empty()) newName += "_" + slot;

                KV::setScalar(base, "name", newName);
                KV::setScalar(base, "prefab", "default_item");
                if (!slot.empty()) KV::setScalar(base, "item_slot", slot);
                if (!r.hero.empty() && r.hero != "__world__") {
                    KV::Node ub("used_by_heroes");
                    ub.block = true;
                    KV::Node h(r.hero);
                    h.value = "1";
                    ub.children.push_back(std::move(h));
                    base.children.push_back(std::move(ub));
                }
                KV::setScalar(base, "baseitem", "1");

                newItems.push_back(KV::buildOverride(base, src, keep));
                ++created;
                debugLog("  created #" + std::to_string(maxKey) + " (" + newName + ")");
            }
        }

        for (auto& n : newItems) items->children.push_back(std::move(n));

        rep.created = created;

        debugLog("applied: " + std::to_string(replaced) + " overrides, " +
            std::to_string(created) + " new items  (hero slots " +
            std::to_string(rep.heroSlots) + ", world " + std::to_string(rep.worldSlots) +
            ", shadowed " + std::to_string(rep.shadowed) + ")");

        {
            std::unordered_map<std::string, int> idCount;
            int dupes = 0;
            for (const auto& n : items->children) {
                std::string id = KV::scalar(n, "id");
                if (id.empty()) continue;
                if (++idCount[id] > 1) {
                    ++dupes;
                    if (dupes <= 10)
                        debugLog("WARN duplicate id " + id + " on item #" + n.key);
                }
            }
            if (dupes > 0)
                debugLog("WARN: " + std::to_string(dupes) + " duplicate item ids detected!");
        }

        std::string out;
        out.reserve(text.size() + text.size() / 2);

        for (const auto& d : directives) { out += d; out += "\n"; }
        KV::writeNode(root, out, 0);

        debugLog("output size: " + std::to_string(out.size()) +
            " (source: " + std::to_string(text.size()) + ")");

        return out;
    }
    catch (const std::exception& e) {
        debugLog("EXCEPTION: " + std::string(e.what()));
        return "";
    }
    catch (...) {
        debugLog("EXCEPTION: unknown");
        return "";
    }
}

bool ModGenerator::writeModFile(
    const fs::path& dotaPath,
    const std::string& modifiedContent,
    std::string* error)
{
    return writeModFile(dotaPath, modifiedContent, {}, error);
}

bool ModGenerator::writeModFile(
    const fs::path& dotaPath,
    const std::string& modifiedContent,
    const std::vector<ExtraEntry>& extra,
    std::string* error)
{
if (modifiedContent.empty() && extra.empty()) {
    *error = "Nothing to write"; return false;
}

fs::path modDir = dotaPath / "game" / "PyScriptPlus";
std::error_code ec;
fs::create_directories(modDir, ec);
if (ec) { *error = "Failed to create dir: " + ec.message(); return false; }

fs::path vpkPath = modDir / "pak01_dir.vpk";
fs::remove(vpkPath, ec);
fs::remove(modDir / "pak01_000.vpk", ec);
fs::remove(modDir / "pak01_001.vpk", ec);

auto pack = vpkpp::VPK::create(vpkPath.string());
if (!pack) { *error = "Failed to create VPK"; return false; }

if (!modifiedContent.empty()) {
    std::vector<std::byte> buffer(modifiedContent.size());
    std::memcpy(buffer.data(), modifiedContent.data(), modifiedContent.size());

    if (!pack->addEntry("scripts/items/items_game.txt", buffer, {})) {
        *error = "Failed to add entry"; return false;
    }
}

for (const auto& e : extra) {
    if (e.path.empty() || e.bytes.empty()) continue;
    std::vector<std::byte> buf = e.bytes;
    if (!pack->addEntry(e.path, std::move(buf), {})) {
        *error = "Failed to add entry: " + e.path; return false;
    }
}

    if (!pack->bake()) { *error = "Failed to bake VPK"; return false; }

    debugLog("writeModFile: wrote " + vpkPath.string() +
             " (+" + std::to_string(extra.size()) + " extra)");
    return true;
}

bool ModGenerator::patchGameInfo(
    const fs::path& dotaPath,
    const std::string& folderName,
    std::string* error)
{
    fs::path giMainPath = dotaPath / "game" / "dota" / "gameinfo.gi";
    fs::path giBranchPath = dotaPath / "game" / "dota" / "gameinfo_branchspecific.gi";
    fs::path backupPath = giBranchPath; backupPath += ".backup_PyScriptPlus";

    if (!fs::exists(giMainPath)) { *error = "gameinfo.gi not found"; return false; }
    if (!fs::exists(giBranchPath)) { *error = "gameinfo_branchspecific.gi not found"; return false; }

    const std::string TAG = "// PyScriptPlus";

    auto readFile = [](const fs::path& fp, std::string& out, std::string* err) -> bool {
        std::ifstream in(fp, std::ios::binary);
        if (!in) { if (err) *err = "Cannot open"; return false; }
        std::stringstream ss; ss << in.rdbuf(); out = ss.str(); in.close();
        if (out.empty()) { if (err) *err = "Empty"; return false; }
        if (out.size() >= 3 &&
            (uint8_t)out[0] == 0xEF && (uint8_t)out[1] == 0xBB && (uint8_t)out[2] == 0xBF)
            out = out.substr(3);
        return true;
        };

    std::string mainContent;
    if (!readFile(giMainPath, mainContent, error)) { *error = "Read gameinfo.gi failed: " + *error; return false; }
    debugLog("patchGameInfo: read " + std::to_string(mainContent.size()) + " bytes from gameinfo.gi");

    size_t spPos = mainContent.find("SearchPaths");
    if (spPos == std::string::npos) { *error = "SearchPaths not found in gameinfo.gi"; return false; }
    size_t spBrace = mainContent.find('{', spPos);
    if (spBrace == std::string::npos) { *error = "SearchPaths malformed"; return false; }

    int depth = 0;
    size_t spEnd = std::string::npos;
    for (size_t i = spBrace; i < mainContent.size(); ++i) {
        if (mainContent[i] == '{') depth++;
        else if (mainContent[i] == '}') { depth--; if (depth == 0) { spEnd = i; break; } }
    }
    if (spEnd == std::string::npos) { *error = "SearchPaths unbalanced"; return false; }

    std::string spInner = mainContent.substr(spBrace + 1, spEnd - spBrace - 1);
    debugLog("patchGameInfo: extracted SearchPaths inner (" + std::to_string(spInner.size()) + " bytes)");

    std::vector<std::string> spLines;
    {
        std::istringstream iss(spInner);
        std::string line;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            spLines.push_back(line);
        }
    }

    std::vector<std::string> out;
    bool addedGame = false, addedMod = false;

    for (auto& line : spLines) {
        auto checkLine = [&line](const char* kw) -> std::pair<std::string, std::string> {
            size_t i = 0;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            std::string indent = line.substr(0, i);
            size_t kwLen = strlen(kw);
            if (line.size() < i + kwLen || line.substr(i, kwLen) != kw) return { "", "" };
            i += kwLen;
            size_t gapStart = i;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t')) i++;
            std::string gap = line.substr(gapStart, i - gapStart);
            if (line.size() < i + 4 || line.substr(i, 4) != "dota") return { "", "" };
            i += 4;
            while (i < line.size() && (line[i] == ' ' || line[i] == '\t' || line[i] == '\r')) i++;
            if (i != line.size()) return { "", "" };
            return { indent, gap };
            };

        auto gp = checkLine("Game");
        if (!gp.first.empty() && !addedGame) {
            out.push_back(gp.first + "Game" + gp.second + folderName + "\t\t" + TAG);
            addedGame = true;
        }

        auto mp = checkLine("Mod");
        if (!mp.first.empty() && !addedMod) {
            out.push_back(mp.first + "Mod" + mp.second + folderName + "\t\t" + TAG);
            addedMod = true;
        }

        out.push_back(line);
    }

    if (!addedGame || !addedMod) {
        *error = "Could not find 'Game dota' or 'Mod dota' in SearchPaths (gameinfo.gi format changed?)";
        return false;
    }

    std::string moddedInner;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i > 0) moddedInner += "\r\n";
        moddedInner += out[i];
    }
    debugLog("patchGameInfo: injected Game/Mod lines (inner size=" +
        std::to_string(moddedInner.size()) + ")");

    std::string branchContent;
    if (!readFile(giBranchPath, branchContent, error)) { *error = "Read branchspecific failed"; return false; }
    debugLog("patchGameInfo: read " + std::to_string(branchContent.size()) + " bytes from branchspecific");

    if (branchContent.find(TAG) != std::string::npos) {
        debugLog("patchGameInfo: already injected (tag found), skipping");
        return true;
    }

    if (!fs::exists(backupPath)) {
        std::ofstream bak(backupPath, std::ios::binary);
        if (!bak) { *error = "Cannot create backup"; return false; }
        bak.write(branchContent.data(), branchContent.size());
        bak.close();
        debugLog("patchGameInfo: backup created");
    }

    std::string indented;
    {
        std::istringstream iss(moddedInner);
        std::string line;
        bool first = true;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t s = line.find_first_not_of(" \t");
            if (s == std::string::npos) continue;
            size_t e = line.find_last_not_of(" \t");
            std::string trimmed = line.substr(s, e - s + 1);
            if (!first) indented += "\r\n";
            indented += "\t\t\t" + trimmed;
            first = false;
        }
    }

    std::string wrappedSP = "\r\n\t\tSearchPaths\r\n\t\t{\r\n" + indented + "\r\n\t\t}\r\n";

    size_t fsPos = branchContent.find("FileSystem");
    if (fsPos != std::string::npos) {
        size_t fsBrace = branchContent.find('{', fsPos);
        if (fsBrace != std::string::npos) {
            depth = 0;
            size_t fsEnd = std::string::npos;
            for (size_t i = fsBrace; i < branchContent.size(); ++i) {
                if (branchContent[i] == '{') depth++;
                else if (branchContent[i] == '}') { depth--; if (depth == 0) { fsEnd = i; break; } }
            }
            if (fsEnd != std::string::npos) {
                branchContent.insert(fsEnd, wrappedSP);
                debugLog("patchGameInfo: inserted SearchPaths into existing FileSystem");
            }
            else { *error = "FileSystem unbalanced in branchspecific"; return false; }
        }
        else { *error = "FileSystem malformed in branchspecific"; return false; }
    }
    else {
        size_t rootBrace = branchContent.find('{');
        if (rootBrace == std::string::npos) { *error = "GameInfo root not found"; return false; }
        depth = 0;
        size_t rootEnd = std::string::npos;
        for (size_t i = rootBrace; i < branchContent.size(); ++i) {
            if (branchContent[i] == '{') depth++;
            else if (branchContent[i] == '}') { depth--; if (depth == 0) { rootEnd = i; break; } }
        }
        if (rootEnd == std::string::npos) { *error = "GameInfo root unbalanced"; return false; }

        std::string fsBlock = "\r\n\tFileSystem\r\n\t{" + wrappedSP + "\t}\r\n";
        branchContent.insert(rootEnd, fsBlock);
        debugLog("patchGameInfo: created new FileSystem block inside GameInfo root");
    }

    std::ofstream outF(giBranchPath, std::ios::binary);
    if (!outF) { *error = "Cannot write (need admin?)"; return false; }
    outF.write(branchContent.data(), branchContent.size());
    outF.close();

    debugLog("patchGameInfo: wrote " + std::to_string(branchContent.size()) + " bytes");
    return true;
}

bool ModGenerator::updateSignatures(const fs::path& dotaPath, std::string* error) {
    fs::path giPath = dotaPath / "game" / "dota" / "gameinfo_branchspecific.gi";
    fs::path sigPath = dotaPath / "game" / "bin" / "win64" / "dota.signatures";

    if (!fs::exists(giPath)) { *error = "gameinfo_branchspecific.gi not found"; return false; }
    if (!fs::exists(sigPath)) { *error = "dota.signatures not found"; return false; }

    std::ifstream giIn(giPath, std::ios::binary);
    std::stringstream giSs; giSs << giIn.rdbuf();
    std::string giContent = giSs.str(); giIn.close();

    if (giContent.empty()) { *error = "gameinfo_branchspecific.gi is empty"; return false; }

    std::string sha1 = sha1_hex(giContent);
    for (auto& c : sha1) c = (char)toupper((unsigned char)c);

    uint32_t crc = crc32_compute(giContent);
    char crcHex[9];
    snprintf(crcHex, sizeof(crcHex), "%02X%02X%02X%02X",
        (crc) & 0xFF,
        (crc >> 8) & 0xFF,
        (crc >> 16) & 0xFF,
        (crc >> 24) & 0xFF);

    const std::string PREFIX = "...\\..\\..\\dota\\gameinfo_branchspecific.gi";
    std::string sigLine = PREFIX + "~SHA1:" + sha1 + ";CRC:" + crcHex;

    std::ifstream sigIn(sigPath, std::ios::binary);
    std::stringstream sigSs; sigSs << sigIn.rdbuf();
    std::string sigContent = sigSs.str(); sigIn.close();

    std::vector<std::string> lines;
    int digestIdx = -1;
    {
        std::istringstream in(sigContent);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("DIGEST:", 0) == 0) digestIdx = (int)lines.size();
            lines.push_back(line);
        }
    }

    if (digestIdx < 0) { *error = "dota.signatures has no DIGEST: line"; return false; }

    std::string cleaned;
    for (size_t i = 0; i < lines.size(); ++i) {
        bool isOur = false;
        if ((int)i > digestIdx && lines[i].rfind(PREFIX + "~", 0) == 0)
            isOur = true;
        if (!isOur) cleaned += lines[i] + "\r\n";
    }

    cleaned += sigLine + "\r\n";

    std::ofstream sigOut(sigPath, std::ios::binary | std::ios::trunc);
    if (!sigOut) { *error = "Cannot write dota.signatures (need admin?)"; return false; }
    sigOut.write(cleaned.data(), cleaned.size());
    sigOut.close();

    debugLog("updateSignatures: " + sigLine);
    return true;
}

bool ModGenerator::restoreGameInfo(const fs::path& dotaPath, std::string* error)
{
    fs::path giBranch = dotaPath / "game" / "dota" / "gameinfo_branchspecific.gi";
    fs::path backup = giBranch; backup += ".backup_PyScriptPlus";
    fs::path modDir = dotaPath / "game" / "PyScriptPlus";

    if (!fs::exists(giBranch)) { *error = "gameinfo_branchspecific.gi not found"; return false; }

    if (fs::exists(backup)) {
        std::string original;
        {
            std::ifstream in(backup, std::ios::binary);
            if (!in) { *error = "Cannot read the backup file"; return false; }
            std::stringstream ss; ss << in.rdbuf(); original = ss.str();
        }
        if (original.empty()) { *error = "Backup file is empty"; return false; }

        std::ofstream out(giBranch, std::ios::binary | std::ios::trunc);
        if (!out) { *error = "Cannot write gameinfo_branchspecific.gi (need admin?)"; return false; }
        out.write(original.data(), (std::streamsize)original.size());
        out.close();

        debugLog("restoreGameInfo: branchspecific restored from backup");
    }
    else {
        std::string content;
        {
            std::ifstream in(giBranch, std::ios::binary);
            if (in) { std::stringstream ss; ss << in.rdbuf(); content = ss.str(); }
        }
        if (content.find("// PyScriptPlus") == std::string::npos) {
            *error = "Nothing to restore (no backup and no PyScriptPlus patch found)";
            debugLog("restoreGameInfo: no backup, file is already clean");
            return false;
        }
        *error = "Backup file is missing - cannot safely undo the gameinfo patch";
        debugLog("restoreGameInfo: patch present but backup missing");
        return false;
    }

    std::error_code ec;
    if (fs::exists(modDir)) {
        if (!fs::exists(modDir / "pak01_dir.vpk")) {
            debugLog("restoreGameInfo: mod dir has no pak01_dir.vpk, leaving it untouched");
        }
        else {
            fs::remove_all(modDir, ec);
            if (ec)
                debugLog("restoreGameInfo: could not remove mod dir: " + ec.message());
            else
                debugLog("restoreGameInfo: mod folder removed");
        }
    }

    std::string sigErr;
    if (!updateSignatures(dotaPath, &sigErr)) {
        *error = "gameinfo restored but signature update failed: " + sigErr;
        return false;
    }

    debugLog("restoreGameInfo: done");
    return true;
}

namespace {
const char* kCfgBegin = "// >>> DotaCheat view settings (auto-generated)";
const char* kCfgEnd   = "// <<< DotaCheat view settings";
}

bool ModGenerator::writeAutoexec(
    const fs::path& dotaPath,
    const std::vector<std::string>& lines,
    fs::path* outPath,
    std::string* error)
{
    fs::path cfgDir = dotaPath / "game" / "dota" / "cfg";
    std::error_code ec;
    fs::create_directories(cfgDir, ec);

    if (!fs::exists(cfgDir)) { *error = "cfg folder not found: " + cfgDir.string(); return false; }

    fs::path cfg = cfgDir / "autoexec.cfg";

    std::string existing;
    {
        std::ifstream in(cfg, std::ios::binary);
        if (in) { std::stringstream ss; ss << in.rdbuf(); existing = ss.str(); }
    }

    std::string cleaned;
    {
        size_t pos = 0;
        while (pos < existing.size()) {
            size_t b = existing.find(kCfgBegin, pos);
            if (b == std::string::npos) { cleaned += existing.substr(pos); break; }

            cleaned += existing.substr(pos, b - pos);

            size_t e = existing.find(kCfgEnd, b);
            if (e == std::string::npos) { pos = b; break; }

            pos = existing.find('\n', e);
            if (pos == std::string::npos) pos = existing.size();
            else ++pos;
        }
    }

    bool cleanedHasContent = cleaned.find_first_not_of(" \t\r\n") != std::string::npos;
    if (lines.empty() && !cleanedHasContent) {
        fs::remove(cfg, ec);
        debugLog("writeAutoexec: autoexec.cfg removed (nothing left)");
        if (outPath) *outPath = cfg;
        return true;
    }

    if (!cleaned.empty() && cleaned.back() != '\n') cleaned += "\r\n";

    std::string block;
    if (!lines.empty()) {
        block += kCfgBegin;
        block += "\r\n";
        for (const auto& l : lines) {
            if (l.empty()) continue;
            block += l;
            block += "\r\n";
        }
        block += kCfgEnd;
        block += "\r\n";
    }

    std::ofstream out(cfg, std::ios::binary | std::ios::trunc);
    if (!out) { *error = "Cannot write autoexec.cfg (need admin?)"; return false; }
    out << cleaned << block;
    out.close();

    if (outPath) *outPath = cfg;
    debugLog("writeAutoexec: " + std::to_string(lines.size()) + " lines -> " + cfg.string());
    return true;
}

bool ModGenerator::clearAutoexec(const fs::path& dotaPath, std::string* error)
{
    return writeAutoexec(dotaPath, {}, nullptr, error);
}
