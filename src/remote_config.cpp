#include "remote_config.h"
#include "diag_log.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>
#include <vector>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

static const wchar_t* kHost = L"api.cppcore.ir";
static const wchar_t* kPath = L"/api/status";


static RemoteStatus        g_rs;
static std::mutex          g_mtx;
static std::atomic<bool>   g_done{ false };
static std::atomic<bool>   g_stop{ false };
static std::thread         g_thread;


static const int kPollSeconds = 120;

static std::string jsonValue(const std::string& js, const char* key)
{
    std::string pat = std::string("\"") + key + "\"";
    size_t p = js.find(pat);
    if (p == std::string::npos) return {};

    p = js.find(':', p + pat.size());
    if (p == std::string::npos) return {};
    ++p;

    while (p < js.size() && (js[p] == ' ' || js[p] == '\t' || js[p] == '\r' || js[p] == '\n')) ++p;
    if (p >= js.size()) return {};

    if (js[p] == '"') {
        size_t e = js.find('"', p + 1);
        if (e == std::string::npos) return {};
        return js.substr(p + 1, e - p - 1);
    }

    size_t e = p;
    while (e < js.size() && js[e] != ',' && js[e] != '}' && js[e] != '\r' && js[e] != '\n') ++e;
    std::string v = js.substr(p, e - p);
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.pop_back();
    return v;
}

namespace {

struct Handle {
    HINTERNET h = nullptr;
    ~Handle() { if (h) WinHttpCloseHandle(h); }
    operator HINTERNET() const { return h; }
};

std::wstring userAgent()
{
    std::wstring ua = L"dota2PyScriptPlus/";
    for (const char* p = APP_VERSION; *p; ++p) ua.push_back((wchar_t)*p);
    return ua;
}

} // namespace

static bool httpGet(const std::wstring& host, const std::wstring& path,
                    std::string* out, std::string* err)
{
    Handle session, connect, request;

    session.h = WinHttpOpen(userAgent().c_str(),
                            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session.h) { *err = "WinHttpOpen failed"; return false; }

    WinHttpSetTimeouts(session.h, 3000, 3000, 3000, 5000);

    connect.h = WinHttpConnect(session.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect.h) { *err = "connect failed"; return false; }

    request.h = WinHttpOpenRequest(connect.h, L"GET", path.c_str(), nullptr,
                                   WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                   WINHTTP_FLAG_SECURE);
    if (!request.h) { *err = "open request failed"; return false; }

    if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        *err = "send failed";
        return false;
    }
    if (!WinHttpReceiveResponse(request.h, nullptr)) {
        *err = "receive failed";
        return false;
    }

    {
        DWORD code = 0, len = sizeof(code);
        WinHttpQueryHeaders(request.h,
                            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) {
            char b[64];
            snprintf(b, sizeof(b), "http %lu", (unsigned long)code);
            *err = b;
            return false;
        }
    }

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(request.h, &avail)) { *err = "query failed"; return false; }
        if (avail == 0) break;

        std::vector<char> buf(avail);
        DWORD got = 0;
        if (!WinHttpReadData(request.h, buf.data(), avail, &got)) { *err = "read failed"; return false; }
        out->append(buf.data(), got);

        if (out->size() > 64 * 1024) break;
    }

    return true;
}

static void pollOnce()
{
    std::string body, err;

    if (!httpGet(kHost, kPath, &body, &err)) {
        DiagLog::add("[remote] status failed: " + err);

        std::lock_guard<std::mutex> lk(g_mtx);
        if (!g_rs.fetched) {
            g_rs.failed = true;
            g_rs.error  = err;
        }
        g_done.store(true);
        return;
    }

    RemoteStatus rs;
    rs.fetched     = true;
    rs.enabled     = jsonValue(body, "enabled") != "false";
    rs.latest      = jsonValue(body, "latest");
    rs.minVersion  = jsonValue(body, "minVersion");
    rs.message     = jsonValue(body, "message");
    rs.downloadUrl = jsonValue(body, "downloadUrl");

    DiagLog::add("[remote] status ok: enabled=" + std::string(rs.enabled ? "1" : "0") +
                 " latest=" + rs.latest + " min=" + rs.minVersion);

    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_rs = rs;
    }
    g_done.store(true);
}

void RemoteConfig::start()
{
    if (g_thread.joinable()) return;

    g_stop.store(false);

    g_thread = std::thread([] {
        pollOnce();

        while (!g_stop.load()) {
            for (int i = 0; i < kPollSeconds * 2 && !g_stop.load(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(500));

            if (g_stop.load()) break;
            pollOnce();
        }
    });
}

void RemoteConfig::stop()
{
    g_stop.store(true);
    if (g_thread.joinable()) g_thread.join();
}

bool RemoteConfig::ready() { return g_done.load(); }

RemoteStatus RemoteConfig::snapshot()
{
    std::lock_guard<std::mutex> lk(g_mtx);
    return g_rs;
}

int RemoteConfig::compareVersion(const std::string& a, const std::string& b)
{
    int pa[4] = { 0, 0, 0, 0 }, pb[4] = { 0, 0, 0, 0 };
    sscanf(a.c_str(), "%d.%d.%d.%d", &pa[0], &pa[1], &pa[2], &pa[3]);
    sscanf(b.c_str(), "%d.%d.%d.%d", &pb[0], &pb[1], &pb[2], &pb[3]);

    for (int i = 0; i < 4; ++i) {
        if (pa[i] != pb[i]) return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}

bool RemoteConfig::updateAvailable()
{
    RemoteStatus s = snapshot();
    if (!s.fetched || s.latest.empty()) return false;
    return compareVersion(s.latest, APP_VERSION) > 0;
}

bool RemoteConfig::mustUpdate()
{
    RemoteStatus s = snapshot();
    if (!s.fetched) return false;
    if (s.enabled == false) return true;
    if (s.minVersion.empty()) return false;
    return compareVersion(s.minVersion, APP_VERSION) > 0;
}