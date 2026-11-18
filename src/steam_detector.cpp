#include "steam_detector.h"
#include <windows.h>
#include <fstream>
#include <sstream>
#include <regex>
#include <iterator>
#pragma comment(lib, "advapi32.lib")

std::optional<fs::path> SteamDetector::detectSteam() {
#ifdef _WIN32
    return detectSteamWindows();
#else
    return std::nullopt;
#endif
}

std::optional<fs::path> SteamDetector::detectDota(const fs::path& steamPath) {
    std::vector<fs::path> libraries;
    libraries.push_back(steamPath / "steamapps");

    fs::path vdfPath = steamPath / "steamapps" / "libraryfolders.vdf";
    if (fs::exists(vdfPath)) {
        std::ifstream file(vdfPath);
        if (file.is_open()) {
            std::string content((std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
            //std::regex re(R"("path"\s+"([^"]+)")");
            std::regex re(R"REGEX("path"\s+"([^"]+)")REGEX");
                std::sregex_iterator it(content.begin(), content.end(), re);
            std::sregex_iterator end;
            for (; it != end; ++it) {
                std::string raw = (*it)[1].str();
                std::string unescaped;
                for (size_t i = 0; i < raw.size(); ++i) {
                    if (raw[i] == '\\' && i + 1 < raw.size() && raw[i + 1] == '\\') {
                        unescaped += '/';
                        ++i;
                    }
                    else {
                        unescaped += raw[i];
                    }
                }
                fs::path lib = fs::path(unescaped) / "steamapps";
                if (fs::exists(lib)) {
                    libraries.push_back(lib);
                }
            }
        }
    }

    for (const auto& lib : libraries) {
        fs::path dotaPath = lib / "common" / "dota 2 beta";
        if (fs::exists(dotaPath)) {
            return dotaPath;
        }
    }
    return std::nullopt;
}

#ifdef _WIN32
std::optional<fs::path> SteamDetector::detectSteamWindows() {
    const wchar_t* regPaths[] = {
        L"SOFTWARE\\WOW6432Node\\Valve\\Steam",
        L"SOFTWARE\\Valve\\Steam"
    };

    for (const auto* regPath : regPaths) {
        HKEY hKey;
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath, 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            wchar_t buffer[MAX_PATH];
            DWORD size = sizeof(buffer);
            if (RegQueryValueExW(hKey, L"InstallPath", nullptr, nullptr,
                reinterpret_cast<LPBYTE>(buffer), &size) == ERROR_SUCCESS) {
                RegCloseKey(hKey);
                fs::path p(buffer);
                if (fs::exists(p)) return p;
                continue;
            }
            RegCloseKey(hKey);
        }
    }

    fs::path candidates[] = {
        "C:/Program Files (x86)/Steam",
        "C:/Program Files/Steam"
    };
    for (const auto& c : candidates) {
        if (fs::exists(c)) return c;
    }
    return std::nullopt;
}
#endif