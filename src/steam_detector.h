#pragma once

#include <optional>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

class SteamDetector {
public:
    static std::optional<fs::path> detectSteam();

    static std::optional<fs::path> detectDota(const fs::path& steamPath);

private:
    static std::optional<fs::path> detectSteamWindows();
};