#include "vpk_manager.h"
#include <vpkpp/format/VPK.h>
#include <vpkpp/vpkpp.h>

std::unique_ptr<vpkpp::PackFile> VPKManager::s_cachedVPK;

bool VPKManager::openGameVPK(const fs::path& vpkPath) {
    s_cachedVPK = vpkpp::VPK::open(vpkPath.string());
    return s_cachedVPK != nullptr;
}

void VPKManager::closeGameVPK() {
    s_cachedVPK.reset();
}

bool VPKManager::isOpen() {
    return s_cachedVPK != nullptr;
}

bool VPKManager::fileExists(const std::string& internalPath) {
    if (!s_cachedVPK || internalPath.empty()) return false;
    return s_cachedVPK->hasEntry(internalPath);
}

std::optional<std::vector<std::byte>> VPKManager::readFileBytes(const std::string& internalPath) {
    if (!s_cachedVPK || internalPath.empty()) return std::nullopt;
    return s_cachedVPK->readEntry(internalPath);
}

std::vector<std::string> VPKManager::listFiles(const std::string& dir, bool recursive) {
    std::vector<std::string> out;
    if (!s_cachedVPK || dir.empty()) return out;

    s_cachedVPK->runForAllEntries(
        dir,
        [&out](const std::string& path, const vpkpp::Entry&) {
            out.push_back(path);
        },
        recursive);

    return out;
}

std::optional<std::string> VPKManager::readFileCached(const std::string& internalPath) {
    if (!s_cachedVPK) return std::nullopt;

    auto entry = s_cachedVPK->readEntry(internalPath);
    if (!entry) return std::nullopt;

    return std::string(
        reinterpret_cast<const char*>(entry->data()),
        entry->size());
}

std::optional<fs::path> VPKManager::findGameVPK(const fs::path& dotaPath) {
    fs::path mainVPK = dotaPath / "game" / "dota" / "pak01_dir.vpk";
    if (fs::exists(mainVPK)) return mainVPK;

    fs::path coreVPK = dotaPath / "game" / "core" / "pak01_dir.vpk";
    if (fs::exists(coreVPK)) return coreVPK;

    return std::nullopt;
}

std::optional<std::string> VPKManager::readFileFromVPK(
    const fs::path& vpkPath,
    const std::string& internalPath)
{
    auto vpk = vpkpp::VPK::open(vpkPath.string());
    if (!vpk) return std::nullopt;

    auto entry = vpk->readEntry(internalPath);
    if (!entry) return std::nullopt;

    return std::string(
        reinterpret_cast<const char*>(entry->data()),
        entry->size());
}