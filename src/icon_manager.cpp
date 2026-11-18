#include "icon_manager.h"
#include "vtex_decoder.h"
#include "vpk_manager.h"
#include <algorithm>
#include <cstring>

ID3D11Device* IconManager::s_device = nullptr;
std::wstring  IconManager::s_vpkPath;
std::unordered_map<std::string, ID3D11ShaderResourceView*> IconManager::s_cache;
std::unordered_map<std::string, bool> IconManager::s_failed;
std::unordered_map<std::string, bool> IconManager::s_queued;
std::deque<std::string> IconManager::s_queue;
bool IconManager::s_ready = false;

void IconManager::init(ID3D11Device* device, const std::wstring& vpkPath) {
    s_device = device;
    s_vpkPath = vpkPath;
    if (!VPKManager::isOpen()) VPKManager::openGameVPK(vpkPath);
    s_ready = true;
}

void IconManager::shutdown() {
    for (auto& [k, v] : s_cache) if (v) v->Release();
    s_cache.clear();
    s_failed.clear();
    s_queued.clear();
    s_queue.clear();
    s_ready = false;
    VPKManager::closeGameVPK();
}

bool IconManager::ready() { return s_ready; }
int IconManager::loadedCount() { return (int)s_cache.size(); }
int IconManager::pendingCount() { return (int)s_queue.size(); }

std::vector<std::string> IconManager::makeCandidates(const std::string& raw) {
    std::string p = raw;
    for (auto& c : p) if (c == '\\') c = '/';
    for (const char* pre : { "s2r://", "panorama://", "file://" }) {
        size_t l = strlen(pre);
        if (p.compare(0, l, pre) == 0) p.erase(0, l);
    }
    while (!p.empty() && p[0] == '/') p.erase(0, 1);

    if (p.compare(0, 16, "panorama/images/") == 0) p = p.substr(16);
    else if (p.compare(0, 7, "images/") == 0) p = p.substr(7);

    for (const char* suf : { ".vtex_c", "_png.png", "_jpg.jpg", ".png", ".jpg", ".vtex", "_png", "_jpg" }) {
        size_t l = strlen(suf);
        if (p.size() > l && p.compare(p.size() - l, l, suf) == 0) {
            p.erase(p.size() - l);
            break;
        }
    }
    if (p.empty()) return {};

    std::vector<std::string> out;

    out.push_back("panorama/images/" + p + "_png.vtex_c");
    out.push_back("panorama/images/" + p + "_jpg.vtex_c");
    out.push_back("panorama/images/" + p + ".vtex_c");
    if (p.compare(0, 5, "econ/") == 0) {
        std::string stripped = p.substr(5);
        out.push_back("panorama/images/" + stripped + "_png.vtex_c");
        out.push_back("panorama/images/" + stripped + ".vtex_c");
    }

    out.push_back("resource/flash3/images/" + p + ".vtex_c");
    out.push_back("resource/flash3/images/" + p + "_png.vtex_c");
    out.push_back("resource/flash3/images/" + p + "_jpg.vtex_c");
    out.push_back("resource/flash2/images/" + p + ".vtex_c");

    if (p.find("econ/") != std::string::npos) {
        out.push_back("resource/flash3/images/" + p + ".vtex_c");
        out.push_back("resource/flash3/images/" + p + "_png.vtex_c");
        out.push_back("resource/flash2/images/" + p + ".vtex_c");
    }

    return out;
}

ID3D11ShaderResourceView* IconManager::createSRV(
    const std::vector<uint8_t>& rgba, int w, int h)
{
    if (!s_device || w <= 0 || h <= 0) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA init = {};
    init.pSysMem = rgba.data();
    init.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    if (FAILED(s_device->CreateTexture2D(&desc, &init, &tex))) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    s_device->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();
    return srv;
}

ID3D11ShaderResourceView* IconManager::loadNow(const std::string& invPath) {
    if (invPath.size() > 7 && invPath.compare(invPath.size() - 7, 7, ".vtex_c") == 0) {
        auto raw = VPKManager::readFileCached(invPath);
        if (!raw) return nullptr;
        auto tex = VTEXDecoder::decode(std::vector<uint8_t>(raw->begin(), raw->end()));
        if (!tex.valid) return nullptr;
        return createSRV(tex.rgba, tex.width, tex.height);
    }

    auto candidates = makeCandidates(invPath);
    for (const auto& path : candidates) {
        auto content = VPKManager::readFileCached(path);
        if (!content) continue;

        auto tex = VTEXDecoder::decode(std::vector<uint8_t>(content->begin(), content->end()));
        if (!tex.valid) continue;

        auto srv = createSRV(tex.rgba, tex.width, tex.height);
        if (srv) return srv;
    }
    return nullptr;
}

ID3D11ShaderResourceView* IconManager::peek(const std::string& invPath) {
    if (invPath.empty()) return nullptr;
    auto it = s_cache.find(invPath);
    if (it != s_cache.end()) return it->second;
    return nullptr;
}

void IconManager::request(const std::string& invPath) {
    if (invPath.empty() || !s_ready) return;
    if (s_cache.count(invPath) || s_failed.count(invPath) || s_queued.count(invPath)) return;
    s_queued[invPath] = true;
    s_queue.push_back(invPath);
}

int IconManager::pump(int budget) {
    if (!s_ready || budget <= 0) return 0;

    int done = 0;
    while (done < budget && !s_queue.empty()) {
        std::string path = std::move(s_queue.front());
        s_queue.pop_front();
        s_queued.erase(path);
        ++done;

        if (s_cache.count(path) || s_failed.count(path)) continue;

        auto srv = loadNow(path);
        if (srv) s_cache[path] = srv;
        else     s_failed[path] = true;
    }
    return done;
}

ID3D11ShaderResourceView* IconManager::get(const std::string& invPath) {
    if (invPath.empty() || !s_ready) return nullptr;

    auto it = s_cache.find(invPath);
    if (it != s_cache.end()) return it->second;
    if (s_failed.count(invPath)) return nullptr;

    auto srv = loadNow(invPath);
    if (srv) { s_cache[invPath] = srv; return srv; }

    s_failed[invPath] = true;
    return nullptr;
}
