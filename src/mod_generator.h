#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include "item_database.h"

namespace fs = std::filesystem;

struct AppliedPick {
    int itemIdx = -1;
    int style   = -1;
};


using AppliedMap = std::unordered_map<std::string, std::vector<AppliedPick>>;

struct BuildReport {
    int heroSlots  = 0;
    int worldSlots = 0;
    int created    = 0;
    int shadowed   = 0;
    int failed     = 0;
    std::string firstError;
};

class ModGenerator {
public:
    struct ExtraEntry {
        std::string path;
        std::vector<std::byte> bytes;
    };

    static std::string buildModifiedItemsGame(
        const std::string& originalContent,
        const AppliedMap& applied,
        const ItemDatabase& db,
        BuildReport* report = nullptr);

    static bool writeModFile(
        const fs::path& dotaPath,
        const std::string& modifiedContent,
        std::string* error);

    static bool writeModFile(
        const fs::path& dotaPath,
        const std::string& modifiedContent,
        const std::vector<ExtraEntry>& extra,
        std::string* error);

    static bool patchGameInfo(
        const fs::path& dotaPath,
        const std::string& folderName,
        std::string* error);

    static bool updateSignatures(
        const fs::path& dotaPath,
        std::string* error);

    static bool restoreGameInfo(
        const fs::path& dotaPath,
        std::string* error);

    static bool writeAutoexec(
        const fs::path& dotaPath,
        const std::vector<std::string>& lines,
        fs::path* outPath,
        std::string* error);

    static bool clearAutoexec(
        const fs::path& dotaPath,
        std::string* error);
};
