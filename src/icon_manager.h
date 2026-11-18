#pragma once
#include <string>
#include <unordered_map>
#include <deque>
#include <vector>
#include <cstdint>
#include <d3d11.h>


class IconManager {
public:
    static void init(ID3D11Device* device, const std::wstring& vpkPath);
    static void shutdown();

    static ID3D11ShaderResourceView* get(const std::string& imageInventoryPath);
    static ID3D11ShaderResourceView* peek(const std::string& imageInventoryPath);
    static void request(const std::string& imageInventoryPath);
    static int  pump(int budget);

    static bool ready();
    static int  loadedCount();
    static int  pendingCount();

private:
    static ID3D11Device* s_device;
    static std::wstring  s_vpkPath;
    static std::unordered_map<std::string, ID3D11ShaderResourceView*> s_cache;
    static std::unordered_map<std::string, bool> s_failed;
    static std::unordered_map<std::string, bool> s_queued;
    static std::deque<std::string> s_queue;
    static bool s_ready;

    static ID3D11ShaderResourceView* loadNow(const std::string& invPath);
    static ID3D11ShaderResourceView* createSRV(
        const std::vector<uint8_t>& rgba, int w, int h);
    static std::vector<std::string> makeCandidates(const std::string& invPath);
};
