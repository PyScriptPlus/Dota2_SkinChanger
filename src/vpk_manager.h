#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// forward declaration
namespace vpkpp { class PackFile; }

class VPKManager {
public:
    static std::optional<fs::path> findGameVPK(const fs::path& dotaPath);

    static std::optional<std::string> readFileFromVPK(
        const fs::path& vpkPath,
        const std::string& internalPath);

    static bool openGameVPK(const fs::path& vpkPath);
    static void closeGameVPK();
    static bool isOpen();
    static std::optional<std::string> readFileCached(const std::string& internalPath);

    static bool fileExists(const std::string& internalPath);
    static std::optional<std::vector<std::byte>> readFileBytes(const std::string& internalPath);
    static std::vector<std::string> listFiles(const std::string& dir, bool recursive = true);

private:
    static std::unique_ptr<vpkpp::PackFile> s_cachedVPK;
};