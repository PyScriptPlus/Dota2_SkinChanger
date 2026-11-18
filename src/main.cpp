#include <kvpp/kvpp.h>
#include <kvpp/KV1.h>
#include <iostream>
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <d3d11.h>
#include <windowsx.h>
#include "steam_detector.h"
#include "vpk_manager.h"
#include <string>
#include <thread>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cfloat>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include "item_database.h"
#include "spell_database.h"
#include "icon_manager.h"
#include "vtex_decoder.h"
#include "mod_generator.h"
#include "diag_log.h"
#include "remote_config.h"
#include <shellapi.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

#pragma comment(linker, "/SUBSYSTEM:WINDOWS")
#pragma comment(linker, "/ENTRY:mainCRTStartup")

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

static ItemDatabase g_itemDb;

struct LoadPayload {
    ItemDatabase db;
    SpellDatabase spellDb;
    std::string status;
    std::wstring vpkPath;
    bool success = false;
};
static std::atomic<LoadPayload*> g_loadPayload{ nullptr };
static std::atomic<bool> g_dbLoaded{ false };
static std::atomic<bool> g_dbLoadingDone{ false };
static std::string g_dbStatus = "Loading...";

static int   g_selectedHeroIdx = -1;
static int   g_selectedItemIdx = -1;
static char  g_heroSearch[64] = "";
static char  g_itemSearch[64] = "";
static int   g_itemFilter = 0;

static AppliedMap g_appliedSkins;

static std::unordered_map<std::string, int> g_stylePrefs;
static std::string g_modStatus = "";
static ImU32       g_modStatusColor = IM_COL32(140, 140, 160, 255);

static SpellDatabase g_spellDb;
static std::unordered_map<std::string, std::string> g_spellOverrides;
static std::string g_spellHero;
static int         g_spellSel = -1;
static std::string g_spellPickFor;
static char        g_spellSearch[96] = "";
static bool        g_spellTried = false;

static std::string ToLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::tolower);
    return s;
}

bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND, UINT, WPARAM, LPARAM);

static int   g_currentTab = 0;
static bool  g_autoUpdate = true;
static bool  g_anonymousMode = false;
static bool  g_showPortraits = true;
static bool  g_verifyBeforeWrite = true;

static float g_uiScale = 1.f;
static float g_uiZoom = 1.20f;
static const float UI_ZOOM_DEF = 1.20f;
static const float UI_ZOOM_MIN = 0.70f;
static const float UI_ZOOM_MAX = 2.50f;

static ImFont* g_fBody = nullptr;
static ImFont* g_fSmall = nullptr;
static ImFont* g_fSemi = nullptr;
static ImFont* g_fBold = nullptr;
static ImFont* g_fHuge = nullptr;

namespace C {
    const ImU32 bg0 = IM_COL32(7, 7, 11, 255);
    const ImU32 bg1 = IM_COL32(11, 11, 18, 255);
    const ImU32 panel = IM_COL32(16, 16, 24, 255);
    const ImU32 panel2 = IM_COL32(22, 22, 33, 255);
    const ImU32 panel3 = IM_COL32(29, 29, 43, 255);
    const ImU32 line = IM_COL32(38, 38, 55, 255);
    const ImU32 lineSoft = IM_COL32(28, 28, 40, 255);
    const ImU32 text = IM_COL32(236, 236, 244, 255);
    const ImU32 text2 = IM_COL32(150, 150, 172, 255);
    const ImU32 dim = IM_COL32(98, 98, 120, 255);
    const ImU32 accent = IM_COL32(124, 92, 255, 255);
    const ImU32 accent2 = IM_COL32(58, 200, 255, 255);
    const ImU32 good = IM_COL32(74, 222, 128, 255);
    const ImU32 bad = IM_COL32(255, 92, 114, 255);
    const ImU32 warn = IM_COL32(255, 186, 76, 255);
    const ImU32 gold = IM_COL32(255, 199, 51, 255);
}

static inline ImU32 SetA(ImU32 c, int a) {
    if (a < 0) a = 0;
    if (a > 255) a = 255;
    return (c & 0x00FFFFFF) | ((ImU32)a << 24);
}

static const float PI_F = 3.14159265358979f;

static inline float Clamp01(float v) { return v < 0.f ? 0.f : (v > 1.f ? 1.f : v); }
static inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }

static std::unordered_map<ImGuiID, float> g_animMap;
static float AnimStep(ImGuiID id, bool active, float speed = 16.f) {
    float& v = g_animMap[id];
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    float k = 1.f - expf(-speed * dt);
    v += ((active ? 1.f : 0.f) - v) * k;
    return v;
}

static void Txt(ImDrawList* dl, ImFont* f, float sz, ImVec2 p, ImU32 col, const char* s) {
    dl->AddText(f, sz, p, col, s);
}

static ImVec2 TxtSz(ImFont* f, float sz, const char* s) {
    return f->CalcTextSizeA(sz, FLT_MAX, 0.f, s);
}

static std::string Ellipsize(ImFont* f, float sz, const std::string& s, float maxw) {
    if (maxw <= 4.f) return "";
    if (f->CalcTextSizeA(sz, FLT_MAX, 0.f, s.c_str()).x <= maxw) return s;
    std::string out = s;
    while (out.size() > 3 &&
        f->CalcTextSizeA(sz, FLT_MAX, 0.f, (out + "...").c_str()).x > maxw)
        out.pop_back();
    return out + "...";
}

static void SoftGlow(ImDrawList* dl, ImVec2 c, float r, ImU32 col, int steps = 12, int a = 6) {
    for (int i = 0; i < steps; ++i) {
        float t = 1.f - (float)i / (float)steps;
        dl->AddCircleFilled(c, r * t, SetA(col, a), 48);
    }
}

static void Chip(ImDrawList* dl, ImVec2 p, const char* label, ImU32 col, float fs = 12.f) {
    ImVec2 ts = TxtSz(g_fSmall, fs, label);
    ImVec2 a = p, b = ImVec2(p.x + ts.x + 18.f, p.y + 22.f);
    dl->AddRectFilled(a, b, SetA(col, 30), 6.f);
    dl->AddRect(a, b, SetA(col, 90), 6.f, 0, 1.f);
    Txt(dl, g_fSmall, fs, ImVec2(a.x + 9.f, a.y + (22.f - ts.y) * 0.5f), col, label);
}

static void IcoSkins(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float th = 1.7f;
    dl->AddCircleFilled(ImVec2(c.x, c.y - s * 0.42f), s * 0.30f, col, 24);
    dl->PathArcTo(ImVec2(c.x, c.y + s * 0.95f), s * 0.62f, PI_F * 1.18f, PI_F * 1.82f, 28);
    dl->PathStroke(col, 0, th);
    dl->AddLine(ImVec2(c.x - s * 0.62f, c.y + s * 0.35f),
        ImVec2(c.x + s * 0.62f, c.y + s * 0.35f), col, th);
}

static void IcoWorld(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float th = 1.6f, r = s * 0.82f;
    dl->AddCircle(c, r, col, 32, th);
    dl->AddLine(ImVec2(c.x - r * 0.92f, c.y), ImVec2(c.x + r * 0.92f, c.y), col, th);
    dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), col, th);
    float d = r * 0.40f;
    dl->AddLine(ImVec2(c.x - r * 0.80f, c.y - d), ImVec2(c.x + r * 0.80f, c.y - d), col, th * 0.8f);
    dl->AddLine(ImVec2(c.x - r * 0.80f, c.y + d), ImVec2(c.x + r * 0.80f, c.y + d), col, th * 0.8f);
}

static void IcoCamera(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    const float th = 1.6f;
    ImVec2 a(c.x - s, c.y - s * 0.60f), b(c.x + s, c.y + s * 0.62f);
    dl->AddRect(a, b, col, s * 0.22f, 0, th);
    dl->AddRectFilled(ImVec2(c.x - s * 0.44f, a.y - s * 0.26f),
                      ImVec2(c.x - s * 0.08f, a.y + 1.f), col, 1.5f);
    dl->AddCircle(ImVec2(c.x, c.y + s * 0.02f), s * 0.34f, col, 20, th);
    dl->AddCircleFilled(ImVec2(c.x, c.y + s * 0.02f), s * 0.11f, col, 12);
}

static void IcoLoadout(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    const float th = 1.6f;
    for (int i = -1; i <= 1; ++i) {
        float y = c.y + (float)i * s * 0.52f;
        float x0 = c.x - s * 0.62f;
        float x1 = c.x + s * (i == 0 ? 0.95f : 0.74f);
        dl->AddCircleFilled(ImVec2(x0, y), s * 0.15f, col, 10);
        dl->AddLine(ImVec2(x0 + s * 0.28f, y), ImVec2(x1, y), col, th);
    }
}

static void IcoSpell(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    const float t = s * 0.34f;
    ImVec2 v[4] = { ImVec2(c.x, c.y - s), ImVec2(c.x + t, c.y),
                    ImVec2(c.x, c.y + s), ImVec2(c.x - t, c.y) };
    ImVec2 h[4] = { ImVec2(c.x - s, c.y), ImVec2(c.x, c.y - t),
                    ImVec2(c.x + s, c.y), ImVec2(c.x, c.y + t) };
    dl->AddConvexPolyFilled(v, 4, col);
    dl->AddConvexPolyFilled(h, 4, col);
}

static void IcoGear(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float th = 1.6f, r = s * 0.34f;
    dl->AddCircle(c, r, col, 24, th);
    for (int i = 0; i < 8; ++i) {
        float a = (float)i * PI_F * 0.25f;
        ImVec2 p1(c.x + cosf(a) * (r + s * 0.14f), c.y + sinf(a) * (r + s * 0.14f));
        ImVec2 p2(c.x + cosf(a) * (r + s * 0.48f), c.y + sinf(a) * (r + s * 0.48f));
        dl->AddLine(p1, p2, col, th);
    }
}

static void IcoInfo(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    float th = 1.6f, r = s * 0.82f;
    dl->AddCircle(c, r, col, 32, th);
    dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), s * 0.11f, col, 12);
    dl->AddLine(ImVec2(c.x, c.y - r * 0.05f), ImVec2(c.x, c.y + r * 0.50f), col, th);
}

static void IcoSearch(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    dl->AddCircle(ImVec2(c.x - s * 0.12f, c.y - s * 0.12f), s * 0.52f, col, 24, 1.6f);
    dl->AddLine(ImVec2(c.x + s * 0.28f, c.y + s * 0.28f),
        ImVec2(c.x + s * 0.72f, c.y + s * 0.72f), col, 1.6f);
}

static void IcoCheck(ImDrawList* dl, ImVec2 c, float s, ImU32 col, float th = 2.f) {
    dl->AddLine(ImVec2(c.x - s * 0.42f, c.y + s * 0.02f),
        ImVec2(c.x - s * 0.10f, c.y + s * 0.34f), col, th);
    dl->AddLine(ImVec2(c.x - s * 0.10f, c.y + s * 0.34f),
        ImVec2(c.x + s * 0.44f, c.y - s * 0.34f), col, th);
}

static void IcoDiamond(ImDrawList* dl, ImVec2 c, float s, ImU32 col) {
    ImVec2 p[4] = {
        ImVec2(c.x, c.y - s),
        ImVec2(c.x + s * 0.72f, c.y),
        ImVec2(c.x, c.y + s),
        ImVec2(c.x - s * 0.72f, c.y)
    };
    dl->AddQuadFilled(p[0], p[1], p[2], p[3], col);
}

static void DrawLogo(ImDrawList* dl, ImVec2 c, float s, float t) {
    float pulse = 0.5f + 0.5f * sinf(t * 1.6f);
    SoftGlow(dl, c, s * 2.2f, C::accent, 10, 5 + (int)(pulse * 3.f));

    ImVec2 o[4] = {
        ImVec2(c.x, c.y - s),
        ImVec2(c.x + s * 0.80f, c.y),
        ImVec2(c.x, c.y + s),
        ImVec2(c.x - s * 0.80f, c.y)
    };
    dl->AddQuadFilled(o[0], o[1], o[2], o[3], SetA(C::accent, 40));

    float i = s * 0.66f;
    ImVec2 n[4] = {
        ImVec2(c.x, c.y - i),
        ImVec2(c.x + i * 0.80f, c.y),
        ImVec2(c.x, c.y + i),
        ImVec2(c.x - i * 0.80f, c.y)
    };
    dl->AddQuadFilled(n[0], n[1], n[2], n[3], C::accent);

    float k = s * 0.30f;
    ImVec2 m[4] = {
        ImVec2(c.x, c.y - k),
        ImVec2(c.x + k * 0.80f, c.y),
        ImVec2(c.x, c.y + k),
        ImVec2(c.x - k * 0.80f, c.y)
    };
    dl->AddQuadFilled(m[0], m[1], m[2], m[3], SetA(C::bg0, 220));
    dl->AddCircleFilled(c, s * 0.14f, SetA(C::accent2, 230), 16);
}

struct ToastState { std::string msg; ImU32 color = C::text2; float life = 0.f; };
static ToastState g_toast;

static void Toast(const std::string& m, ImU32 col) {
    g_toast.msg = m;
    g_toast.color = col;
    g_toast.life = 4.0f;
}

static std::string UiZoomFile()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return std::string();
    std::string dir = std::string(appdata) + "\\dota2PyScriptPlus";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\ui.txt";
}

static void UiZoomLoad()
{
    std::string p = UiZoomFile();
    if (p.empty()) return;
    FILE* f = fopen(p.c_str(), "r");
    if (!f) return;
    float z = 0.f;
    if (fscanf(f, "%f", &z) == 1 && z >= UI_ZOOM_MIN && z <= UI_ZOOM_MAX) g_uiZoom = z;
    fclose(f);
}

static void UiZoomSave()
{
    std::string p = UiZoomFile();
    if (p.empty()) return;
    FILE* f = fopen(p.c_str(), "w");
    if (!f) return;
    fprintf(f, "%.4f\n", g_uiZoom);
    fclose(f);
}

// dir: +1 bigger, -1 smaller, 0 reset
static void UiZoomStep(int dir)
{
    if (dir == 0) g_uiZoom = UI_ZOOM_DEF;
    else {
        g_uiZoom += dir * 0.05f;
        if (g_uiZoom < UI_ZOOM_MIN) g_uiZoom = UI_ZOOM_MIN;
        if (g_uiZoom > UI_ZOOM_MAX) g_uiZoom = UI_ZOOM_MAX;
    }
    UiZoomSave();

    char buf[64];
    snprintf(buf, sizeof(buf), "UI scale  %d%%", (int)(g_uiZoom * 100.f + 0.5f));
    Toast(buf, C::accent2);
}

static bool GradientButton(const char* id, const char* label, ImVec2 size, bool primary) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiID uid = ImGui::GetID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = p, b = ImVec2(p.x + size.x, p.y + size.y);

    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##btn", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    float t = AnimStep(uid, hov, 18.f);
    float grow = t * 1.5f;
    a.x -= grow; a.y -= grow * 0.5f;
    b.x += grow; b.y += grow * 0.5f;

    if (primary) {
        if (t > 0.02f) SoftGlow(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
            size.x * 0.55f, C::accent, 8, (int)(3 * t));
        ImU32 c1 = SetA(C::accent, (int)(230 + 25 * t));
        ImU32 c2 = SetA(C::accent2, (int)(170 + 60 * t));
        dl->AddRectFilledMultiColor(a, b, c1, c2, c2, c1);
        dl->AddRect(a, b, SetA(C::accent2, (int)(40 + 90 * t)), 9.f, 0, 1.f);
    }
    else {
        dl->AddRectFilled(a, b, SetA(C::panel3, (int)(170 + 60 * t)), 9.f);
        dl->AddRect(a, b, SetA(C::line, (int)(200 + 55 * t)), 9.f, 0, 1.f);
    }

    ImVec2 ts = TxtSz(g_fSemi, 14.f, label);
    Txt(dl, g_fSemi, 14.f,
        ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
        primary ? IM_COL32(255, 255, 255, 255) : C::text, label);

    return clicked;
}

static bool GhostButton(const char* id, const char* label, ImVec2 size, ImU32 tint = C::bad) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiID uid = ImGui::GetID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = p, b = ImVec2(p.x + size.x, p.y + size.y);

    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##gbtn", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    float t = AnimStep(uid, hov, 18.f);
    dl->AddRectFilled(a, b, SetA(tint, (int)(16 + 30 * t)), 8.f);
    dl->AddRect(a, b, SetA(tint, (int)(70 + 120 * t)), 8.f, 0, 1.f);

    ImVec2 ts = TxtSz(g_fSmall, 13.f, label);
    Txt(dl, g_fSmall, 13.f,
        ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f),
        SetA(tint, (int)(200 + 55 * t)), label);

    return clicked;
}

static void Pill(const char* label, int* value, int idx) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiID uid = ImGui::GetID(label);
    ImVec2 ts = TxtSz(g_fSmall, 12.f, label);
    ImVec2 size(ts.x + 22.f, 28.f);
    ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::PushID(label);
    bool clicked = ImGui::InvisibleButton("##pill", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    bool sel = (*value == idx);
    float t = AnimStep(uid, sel || hov, 18.f);
    ImVec2 a = p, b = ImVec2(p.x + size.x, p.y + size.y);

    if (sel) {
        dl->AddRectFilled(a, b, SetA(C::accent, 55), 7.f);
        dl->AddRect(a, b, SetA(C::accent, 180), 7.f, 0, 1.f);
    }
    else {
        dl->AddRectFilled(a, b, SetA(C::panel2, (int)(120 + 90 * t)), 7.f);
        dl->AddRect(a, b, SetA(C::line, 160), 7.f, 0, 1.f);
    }

    Txt(dl, g_fSmall, 12.f,
        ImVec2(a.x + 11.f, a.y + (size.y - ts.y) * 0.5f),
        sel ? C::text : SetA(C::text2, 200), label);

    if (clicked) *value = idx;
    ImGui::SameLine(0, 7.f);
}

static bool SearchBox(const char* id, const char* hint, char* buf, size_t n, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = 38.f;
    ImVec2 a = p, b = ImVec2(p.x + width, p.y + h);

    bool focused = ImGui::GetIO().WantTextInput;
    dl->AddRectFilled(a, b, C::panel2, 9.f);
    dl->AddRect(a, b, SetA(focused ? C::accent : C::line, 220), 9.f, 0, focused ? 1.6f : 1.f);
    IcoSearch(dl, ImVec2(b.x - 20.f, p.y + h * 0.5f), 13.f, SetA(C::dim, 230));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(12.f, (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, C::text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, SetA(C::dim, 200));

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width - 34.f);
    bool changed = ImGui::InputTextWithHint("##search", hint, buf, n);
    ImGui::PopID();

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);
    return changed;
}

static bool NameBox(const char* id, const char* hint, char* buf, size_t n, float width) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float h = 38.f;
    ImVec2 a = p, b = ImVec2(p.x + width, p.y + h);

    dl->AddRectFilled(a, b, C::panel2, 9.f);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
        ImVec2(12.f, (h - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, C::text);
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, SetA(C::dim, 200));

    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    bool changed = ImGui::InputTextWithHint("##name", hint, buf, n);
    bool active = ImGui::IsItemActive();
    ImGui::PopID();

    ImGui::PopStyleColor(6);
    ImGui::PopStyleVar(2);

    dl->AddRect(a, b, SetA(active ? C::accent : C::line, 220), 9.f, 0, active ? 1.6f : 1.f);
    return changed;
}

static bool Toggle(const char* id, const char* label, const char* desc, bool* v) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiID uid = ImGui::GetID(id);
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    float h = desc ? 60.f : 46.f;

    ImGui::PushID(id);
    bool clicked = ImGui::InvisibleButton("##row", ImVec2(w, h));
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    if (clicked) *v = !*v;

    float t = AnimStep(uid, *v, 14.f);
    float hovA = AnimStep(uid + 1, hov, 14.f);

    ImVec2 a = p, b = ImVec2(p.x + w, p.y + h);
    dl->AddRectFilled(a, b, SetA(C::panel2, (int)(90 + 60 * hovA)), 9.f);

    Txt(dl, g_fSemi, 14.f, ImVec2(p.x + 16.f, p.y + (desc ? 12.f : (h - 18.f) * 0.5f)),
        C::text, label);
    if (desc)
        Txt(dl, g_fSmall, 12.f, ImVec2(p.x + 16.f, p.y + 32.f), SetA(C::dim, 230), desc);

    ImVec2 sw(b.x - 62.f, p.y + (h - 22.f) * 0.5f);
    ImVec2 sw2(sw.x + 42.f, sw.y + 22.f);
    dl->AddRectFilled(sw, sw2, SetA(C::line, 255), 11.f);
    if (t > 0.01f) {
        dl->AddRectFilled(sw, sw2, SetA(C::accent, (int)(200 * t)), 11.f);
        SoftGlow(dl, ImVec2((sw.x + sw2.x) * 0.5f, (sw.y + sw2.y) * 0.5f), 20.f, C::accent, 6, (int)(5 * t));
    }
    dl->AddCircleFilled(ImVec2(Lerp(sw.x + 11.f, sw2.x - 11.f, t), sw.y + 11.f),
        8.f, IM_COL32(255, 255, 255, 255), 20);

    return clicked;
}

static ImU32 RarityColor(const ItemInfo& item) {
    if (item.isArcana)   return IM_COL32(255, 199, 51, 255);
    if (item.isImmortal) return IM_COL32(242, 71, 71, 255);
    if (item.isPersona)  return IM_COL32(217, 115, 255, 255);
    std::string r = ToLower(item.rarity);
    if (r.find("legendary") != std::string::npos) return IM_COL32(204, 77, 217, 255);
    if (r.find("mythical") != std::string::npos) return IM_COL32(217, 89, 140, 255);
    if (r.find("rare") != std::string::npos) return IM_COL32(77, 153, 242, 255);
    if (r.find("uncommon") != std::string::npos) return IM_COL32(115, 217, 128, 255);
    if (r.find("common") != std::string::npos) return IM_COL32(166, 166, 184, 255);
    return IM_COL32(140, 140, 165, 255);
}

static const char* RarityTag(const ItemInfo& item) {
    if (item.isArcana)   return "ARCANA";
    if (item.isImmortal) return "IMMORTAL";
    if (item.isPersona)  return "PERSONA";
    if (!item.rarity.empty()) return item.rarity.c_str();
    return "ITEM";
}

static ImU32 HexCol(const std::string& hex, ImU32 fallback) {
    if (hex.size() != 7 || hex[0] != '#') return fallback;
    auto hx = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return 0;
        };
    return IM_COL32(hx(hex[1]) * 16 + hx(hex[2]),
        hx(hex[3]) * 16 + hx(hex[4]),
        hx(hex[5]) * 16 + hx(hex[6]), 255);
}

static std::unordered_map<int, float> g_cardAnim;

static bool DrawItemCard(const ItemInfo& item, bool selected, bool applied, bool slotBusy, int idx,
    ImVec2 pos, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImU32 rar = RarityColor(item);

    ImGui::PushID(idx);
    ImGui::SetCursorScreenPos(pos);
    bool clicked = ImGui::InvisibleButton("##card", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    float& hv = g_cardAnim[idx];
    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    hv += ((hov ? 1.f : 0.f) - hv) * (1.f - expf(-18.f * dt));

    float lift = hv * 3.f;
    ImVec2 a(pos.x - lift * 0.5f, pos.y - lift);
    ImVec2 b(pos.x + size.x + lift * 0.5f, pos.y + size.y - lift * 0.5f);
    float rnd = 12.f;

    if (selected || hv > 0.02f) {
        ImU32 glowCol = selected ? C::accent : rar;
        SoftGlow(dl, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f),
            size.x * 0.62f, glowCol, 8, (int)(3 + 5 * hv));
    }

    ImU32 top = selected ? SetA(C::accent, 46) : (applied ? SetA(C::good, 30) : SetA(C::panel2, 255));
    ImU32 bot = selected ? SetA(C::accent, 20) : (applied ? SetA(C::good, 12) : SetA(C::panel, 255));
    dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);

    float pad = 7.f;
    ImVec2 ia(a.x + pad, a.y + pad);
    ImVec2 ib(b.x - pad, a.y + size.y - 46.f);
    dl->AddRectFilled(ia, ib, IM_COL32(6, 6, 10, 235), 8.f);

    ID3D11ShaderResourceView* srv = IconManager::peek(item.imageInventory);
    if (!srv && IconManager::ready()) IconManager::request(item.imageInventory);
    if (srv) {
        dl->AddImage((ImTextureID)(intptr_t)srv, ia, ib);
    }
    else {
        std::string initial = item.name.empty() ? "?" : std::string(1, (char)toupper(item.name[0]));
        ImVec2 ts = TxtSz(g_fHuge, 34.f, initial.c_str());
        Txt(dl, g_fHuge, 34.f,
            ImVec2((ia.x + ib.x - ts.x) * 0.5f, (ia.y + ib.y - ts.y) * 0.5f),
            SetA(C::dim, 160), initial.c_str());
    }

    dl->AddRectFilledMultiColor(
        ImVec2(a.x + 1, b.y - 52.f), ImVec2(b.x - 1, b.y - 1),
        IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
        IM_COL32(0, 0, 0, 210), IM_COL32(0, 0, 0, 210));

    // border
    ImU32 border = selected ? C::accent : SetA(rar, (int)(110 + 110 * hv));
    dl->AddRect(a, b, border, rnd, 0, selected ? 2.2f : 1.5f);

    // top accent
    dl->AddRectFilled(ImVec2(a.x + 12.f, a.y + 1.5f), ImVec2(b.x - 12.f, a.y + 3.5f),
        SetA(rar, selected ? 255 : (int)(140 + 100 * hv)), 1.f);

    {
        const char* sl = item.slot.empty() ? "visual" : item.slot.c_str();
        ImVec2 ts = TxtSz(g_fSmall, 10.f, sl);
        ImVec2 ca(a.x + 7.f, a.y + 7.f);
        ImVec2 cb(ca.x + ts.x + 13.f, ca.y + 17.f);
        ImU32 col = applied ? C::good : (slotBusy ? C::warn : SetA(C::text2, 225));
        dl->AddRectFilled(ca, cb, IM_COL32(6, 6, 10, 205), 8.5f);
        dl->AddRect(ca, cb, SetA(col, 135), 8.5f, 0, 1.f);
        Txt(dl, g_fSmall, 10.f, ImVec2(ca.x + 6.5f, ca.y + (17.f - ts.y) * 0.5f), col, sl);
    }

    // text
    std::string nm = Ellipsize(g_fSemi, 13.f, item.name, size.x - 24.f);
    Txt(dl, g_fSemi, 13.f, ImVec2(a.x + 12.f, b.y - 40.f), C::text, nm.c_str());
    Txt(dl, g_fSmall, 10.5f, ImVec2(a.x + 12.f, b.y - 22.f), SetA(rar, 235), RarityTag(item));

    if (item.hasStyleChoice) {
        ImVec2 ts = TxtSz(g_fSmall, 9.5f, "STYLE");
        ImVec2 ca(b.x - 12.f - ts.x - 12.f, b.y - 26.f);
        ImVec2 cb(b.x - 12.f, b.y - 12.f);
        dl->AddRectFilled(ca, cb, SetA(C::gold, 36), 6.f);
        dl->AddRect(ca, cb, SetA(C::gold, 130), 6.f, 0, 1.f);
        Txt(dl, g_fSmall, 9.5f, ImVec2(ca.x + 6.f, ca.y + (14.f - ts.y) * 0.5f), C::gold, "STYLE");
    }

    // applied badge
    if (applied) {
        ImVec2 c(b.x - 17.f, a.y + 17.f);
        dl->AddCircleFilled(c, 11.f, IM_COL32(6, 10, 8, 235), 24);
        dl->AddCircleFilled(c, 9.f, C::good, 24);
        IcoCheck(dl, c, 9.f, IM_COL32(8, 20, 12, 255), 2.f);
    }

    // selected ring
    if (selected) {
        dl->AddRect(ImVec2(a.x - 3.f, a.y - 3.f), ImVec2(b.x + 3.f, b.y + 3.f),
            SetA(C::accent, 90), 15.f, 0, 2.f);
    }

    if (hov) {
        ImGui::SetTooltip("%s\n%s  |  slot: %s%s", item.name.c_str(), RarityTag(item),
            item.slot.empty() ? "visual" : item.slot.c_str(),
            applied ? "\napplied to this hero"
            : (slotBusy ? "\nthis slot is already taken - applying replaces it" : ""));
    }

    return clicked;
}

static std::string SlotKeyOf(const ItemInfo& it) {
    return it.slot.empty() ? std::string("visual") : it.slot;
}

static const std::vector<AppliedPick>* PicksOf(const std::string& heroInternal) {
    auto it = g_appliedSkins.find(heroInternal);
    return it == g_appliedSkins.end() ? nullptr : &it->second;
}

static bool IsItemApplied(const std::string& heroInternal, int itemIdx) {
    const auto* v = PicksOf(heroInternal);
    if (!v) return false;
    for (const auto& p : *v) if (p.itemIdx == itemIdx) return true;
    return false;
}

static int AppliedInSlot(const std::string& heroInternal, const std::string& slot) {
    const auto* v = PicksOf(heroInternal);
    if (!v) return -1;
    const auto& items = g_itemDb.getItems();
    int found = -1;
    for (const auto& p : *v) {
        if (p.itemIdx < 0 || p.itemIdx >= (int)items.size()) continue;
        if (SlotKeyOf(items[p.itemIdx]) == slot) found = p.itemIdx;
    }
    return found;
}

static int SlotPickCount(const std::string& heroInternal, const std::string& slot) {
    const auto* v = PicksOf(heroInternal);
    if (!v) return 0;
    const auto& items = g_itemDb.getItems();
    int n = 0;
    for (const auto& p : *v) {
        if (p.itemIdx < 0 || p.itemIdx >= (int)items.size()) continue;
        if (SlotKeyOf(items[p.itemIdx]) == slot) ++n;
    }
    return n;
}

static int CountAppliedForHero(const std::string& heroInternal) {
    const auto* v = PicksOf(heroInternal);
    return v ? (int)v->size() : 0;
}

static void CountAllApplied(int& skins, int& world) {
    skins = world = 0;
    for (const auto& hp : g_appliedSkins) {
        if (hp.first == "__world__") world += (int)hp.second.size();
        else                         skins += (int)hp.second.size();
    }
}

static void AppliedSave();

static int StyleOf(const std::string& heroInternal, int itemIdx) {
    if (itemIdx < 0 || itemIdx >= (int)g_itemDb.getItems().size()) return -1;
    const auto& id = g_itemDb.getItems()[itemIdx].id;

    const auto* v = PicksOf(heroInternal);
    if (v) for (const auto& p : *v) if (p.itemIdx == itemIdx && p.style >= 0) return p.style;

    auto it = g_stylePrefs.find(heroInternal + "|" + id);
    return it == g_stylePrefs.end() ? -1 : it->second;
}

static void SetStyleFor(const std::string& heroInternal, int itemIdx, int style) {
    if (itemIdx < 0 || itemIdx >= (int)g_itemDb.getItems().size()) return;
    g_stylePrefs[heroInternal + "|" + g_itemDb.getItems()[itemIdx].id] = style;

    auto& v = g_appliedSkins[heroInternal];
    for (auto& p : v) if (p.itemIdx == itemIdx) { p.style = style; break; }
    AppliedSave();
}

static std::string AppliedFile()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return std::string();
    std::string dir = std::string(appdata) + "\\dota2PyScriptPlus";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\applied.txt";
}

static std::string StateSerialize()
{
    std::string out;
    const auto& items = g_itemDb.getItems();
    char line[1024];

    for (const auto& heroPair : g_appliedSkins)
        for (const auto& pick : heroPair.second) {
            if (pick.itemIdx < 0 || pick.itemIdx >= (int)items.size()) continue;
            snprintf(line, sizeof(line), "%s\t%s\t%d\n", heroPair.first.c_str(),
                items[pick.itemIdx].id.c_str(), pick.style);
            out += line;
        }

    for (const auto& sp : g_stylePrefs) {
        snprintf(line, sizeof(line), "~%s\t%d\n", sp.first.c_str(), sp.second);
        out += line;
    }

    for (const auto& sp : g_spellOverrides) {
        snprintf(line, sizeof(line), "@%s\t%s\n", sp.first.c_str(), sp.second.c_str());
        out += line;
    }
    return out;
}

static void StateDeserialize(const std::string& text)
{
    g_appliedSkins.clear();
    g_stylePrefs.clear();
    g_spellOverrides.clear();

    std::unordered_map<std::string, int> byId;
    const auto& items = g_itemDb.getItems();
    byId.reserve(items.size() * 2 + 16);
    for (size_t i = 0; i < items.size(); ++i) byId.emplace(items[i].id, (int)i);

    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string s = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        if (s.empty()) continue;

        if (s[0] == '~') {
            size_t t = s.find('\t');
            if (t != std::string::npos)
                g_stylePrefs[s.substr(1, t - 1)] = atoi(s.c_str() + t + 1);
            continue;
        }

        if (s[0] == '@') {
            size_t t = s.find('\t');
            if (t != std::string::npos)
                g_spellOverrides[s.substr(1, t - 1)] = s.substr(t + 1);
            continue;
        }

        size_t t1 = s.find('\t'); if (t1 == std::string::npos) continue;
        size_t t2 = s.find('\t', t1 + 1); if (t2 == std::string::npos) continue;

        std::string hero = s.substr(0, t1);
        std::string id = s.substr(t1 + 1, t2 - t1 - 1);
        int style = atoi(s.c_str() + t2 + 1);

        auto it = byId.find(id);
        if (it == byId.end()) continue;

        AppliedPick p;
        p.itemIdx = it->second;
        p.style = style;
        g_appliedSkins[hero].push_back(p);
    }
}

static bool ReadWholeFile(const std::string& path, std::string* out)
{
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    out->clear();
    char buf[8192];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out->append(buf, n);
    fclose(f);
    return true;
}

static bool WriteWholeFile(const std::string& path, const std::string& data)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    if (!data.empty()) fwrite(data.data(), 1, data.size(), f);
    fclose(f);
    return true;
}

static void AppliedSave()
{
    std::string p = AppliedFile();
    if (p.empty()) return;
    WriteWholeFile(p, StateSerialize());
}

static void AppliedLoad()
{
    std::string p = AppliedFile();
    if (p.empty()) return;
    std::string text;
    if (!ReadWholeFile(p, &text)) return;
    StateDeserialize(text);
}

static char g_presetName[64] = "";
static bool g_presetsDirty = true;

static const int BIND_SLOTS   = 9;
static const int BIND_ID_BASE = 8100;
static std::unordered_map<int, std::string> g_binds;
static std::atomic<int> g_hotkeySlot{ -1 };
static bool g_bindAutoGenerate = true;

struct PresetEntry {
    std::string name;
    int skins = 0;
    int world = 0;
    int spells = 0;
};

static std::string PresetDir()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return std::string();
    std::string base = std::string(appdata) + "\\dota2PyScriptPlus";
    CreateDirectoryA(base.c_str(), nullptr);
    std::string dir = base + "\\loadouts";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir;
}

static std::string PresetSafeName(const std::string& raw)
{
    std::string s;
    for (char c : raw) {
        unsigned char u = (unsigned char)c;
        if (u < 32) continue;
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|') continue;
        s.push_back(c);
        if (s.size() >= 40) break;
    }
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}

static std::string PresetPath(const std::string& name)
{
    std::string dir = PresetDir();
    if (dir.empty()) return std::string();
    std::string n = PresetSafeName(name);
    if (n.empty()) return std::string();
    return dir + "\\" + n + ".txt";
}

static std::vector<PresetEntry> PresetScan()
{
    std::vector<PresetEntry> out;
    std::string dir = PresetDir();
    if (dir.empty()) return out;

    WIN32_FIND_DATAA fd;
    std::string pattern = dir + "\\*.txt";
    HANDLE h = FindFirstFileA(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;

    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::string fn = fd.cFileName;
        if (fn.size() < 5) continue;
        if (fn.compare(fn.size() - 4, 4, ".txt") != 0) continue;

        PresetEntry e;
        e.name = fn.substr(0, fn.size() - 4);

        std::string text;
        if (ReadWholeFile(dir + "\\" + fn, &text)) {
            size_t pos = 0;
            while (pos < text.size()) {
                size_t eol = text.find('\n', pos);
                if (eol == std::string::npos) eol = text.size();
                std::string s = text.substr(pos, eol - pos);
                pos = eol + 1;
                if (s.empty()) continue;

                if (s[0] == '@') { ++e.spells; continue; }
                if (s[0] == '~') continue;

                size_t t = s.find('\t');
                if (t == std::string::npos) continue;
                if (s.compare(0, t, "__world__") == 0) ++e.world;
                else ++e.skins;
            }
        }
        out.push_back(e);
    } while (FindNextFileA(h, &fd));
    FindClose(h);

    std::sort(out.begin(), out.end(),
        [](const PresetEntry& a, const PresetEntry& b) { return a.name < b.name; });
    return out;
}

static bool PresetSave(const std::string& name)
{
    std::string p = PresetPath(name);
    if (p.empty()) return false;
    if (!WriteWholeFile(p, StateSerialize())) return false;
    g_presetsDirty = true;
    return true;
}

static bool PresetLoad(const std::string& name)
{
    std::string p = PresetPath(name);
    if (p.empty()) return false;
    std::string text;
    if (!ReadWholeFile(p, &text)) return false;
    StateDeserialize(text);
    AppliedSave();
    return true;
}

static void PresetDelete(const std::string& name)
{
    std::string p = PresetPath(name);
    if (p.empty()) return;
    DeleteFileA(p.c_str());
    g_presetsDirty = true;
}

static std::string BindsFile()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return std::string();
    std::string dir = std::string(appdata) + "\\dota2PyScriptPlus";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\binds.txt";
}

static void BindsSave()
{
    std::string p = BindsFile();
    if (p.empty()) return;

    std::string out;
    char line[512];
    for (const auto& b : g_binds) {
        snprintf(line, sizeof(line), "%d\t%s\n", b.first, b.second.c_str());
        out += line;
    }
    WriteWholeFile(p, out);
}

static void BindsLoad()
{
    std::string p = BindsFile();
    if (p.empty()) return;
    std::string text;
    if (!ReadWholeFile(p, &text)) return;

    g_binds.clear();
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string s = text.substr(pos, eol - pos);
        pos = eol + 1;
        while (!s.empty() && s.back() == '\r') s.pop_back();
        if (s.empty()) continue;

        size_t t = s.find('\t');
        if (t == std::string::npos) continue;
        int slot = atoi(s.c_str());
        if (slot < 0 || slot >= BIND_SLOTS) continue;
        g_binds[slot] = s.substr(t + 1);
    }
}

static int BindSlotOf(const std::string& preset)
{
    for (const auto& b : g_binds)
        if (b.second == preset) return b.first;
    return -1;
}

static int  g_camDistance  = 1134;
static bool g_camWheelZoom = true;
static bool g_camNoShake   = false;
static bool g_camBinds     = false;
static std::string g_camPath;

static std::string CameraFile()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return std::string();
    std::string dir = std::string(appdata) + "\\dota2PyScriptPlus";
    CreateDirectoryA(dir.c_str(), nullptr);
    return dir + "\\camera.txt";
}

static void CameraSave()
{
    std::string p = CameraFile();
    if (p.empty()) return;
    char line[64];
    snprintf(line, sizeof(line), "%d\t%d\t%d\t%d\n", g_camDistance,
        g_camWheelZoom ? 1 : 0, g_camNoShake ? 1 : 0, g_camBinds ? 1 : 0);
    WriteWholeFile(p, line);
}

static void CameraLoad()
{
    std::string p = CameraFile();
    if (p.empty()) return;
    std::string t;
    if (!ReadWholeFile(p, &t)) return;

    int d = 1134, w = 1, s = 0, b = 0;
    sscanf(t.c_str(), "%d\t%d\t%d\t%d", &d, &w, &s, &b);
    if (d < 700)  d = 700;
    if (d > 1400) d = 1400;

    g_camDistance  = d;
    g_camWheelZoom = (w != 0);
    g_camNoShake   = (s != 0);
    g_camBinds     = (b != 0);
}

static std::string SetBaseName(const std::string& name) {
    size_t p = name.find(" - ");
    if (p == std::string::npos) return {};
    return name.substr(0, p);
}

static int FindSetBody(int itemIdx) {
    const auto& items = g_itemDb.getItems();
    if (itemIdx < 0 || itemIdx >= (int)items.size()) return -1;
    const std::string base = SetBaseName(items[itemIdx].name);
    if (base.empty()) return -1;

    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].name != base) continue;
        if (!items[i].isArcana && items[i].slot != "hero_base") continue;
        for (const auto& h : items[i].heroes)
            for (const auto& h2 : items[itemIdx].heroes)
                if (h == h2) return (int)i;
    }
    return -1;
}

static bool ToggleItemApplied(const std::string& heroInternal, int itemIdx) {
    auto& v = g_appliedSkins[heroInternal];
    for (size_t i = 0; i < v.size(); ++i) {
        if (v[i].itemIdx != itemIdx) continue;
        v.erase(v.begin() + (long)i);
        AppliedSave();
        return true;
    }
    AppliedPick p;
    p.itemIdx = itemIdx;
    auto pref = g_stylePrefs.find(heroInternal + "|" + g_itemDb.getItems()[itemIdx].id);
    p.style = (pref == g_stylePrefs.end()) ? -1 : pref->second;
    v.push_back(p);

    if (int body = FindSetBody(itemIdx); body >= 0) {
        bool have = false;
        for (const auto& q : v) if (q.itemIdx == body) { have = true; break; }
        if (!have) {
            AppliedPick b;
            b.itemIdx = body;
            auto bp = g_stylePrefs.find(heroInternal + "|" + g_itemDb.getItems()[body].id);
            b.style = (bp == g_stylePrefs.end()) ? -1 : bp->second;
            v.push_back(b);
        }
    }

    AppliedSave();
    return false;
}

static int HeroSlotCapacity(const std::string& heroInternal)
{
    static std::unordered_map<std::string, int> cache;
    auto c = cache.find(heroInternal);
    if (c != cache.end()) return c->second;

    std::unordered_set<std::string> uniq;
    for (const auto& h : g_itemDb.getHeroes()) {
        if (h.internalName != heroInternal) continue;
        uniq.reserve(h.itemIndices.size() + 8);
        for (size_t idx : h.itemIndices) {
            const std::string& s = g_itemDb.getItems()[idx].slot;
            if (!s.empty()) uniq.insert(s);
        }
        break;
    }
    int n = (int)uniq.size();
    if (n < 1) n = 1;
    cache[heroInternal] = n;
    return n;
}

static void RunGenerate() {
    int nSkins = 0, nWorld = 0;
    CountAllApplied(nSkins, nWorld);
    if (nSkins == 0 && nWorld == 0 && g_spellOverrides.empty()) {
        g_modStatus = "Nothing selected";
        g_modStatusColor = C::warn;
        Toast("Select at least one skin, world item or spell icon first", C::warn);
        return;
    }

    auto steamPath = SteamDetector::detectSteam();
    if (!steamPath) {
        g_modStatus = "Steam not found";
        g_modStatusColor = C::bad;
        Toast("Steam installation not found", C::bad);
        return;
    }
    auto dotaPath = SteamDetector::detectDota(*steamPath);
    if (!dotaPath) {
        g_modStatus = "Dota 2 not found";
        g_modStatusColor = C::bad;
        Toast("Dota 2 installation not found", C::bad);
        return;
    }

    std::string err;

    if (!ModGenerator::patchGameInfo(*dotaPath, "PyScriptPlus", &err)) {
        g_modStatus = "Patch failed: " + err;
        g_modStatusColor = C::bad;
        Toast("gameinfo patch failed", C::bad);
        return;
    }

    BuildReport rep;
    std::string modified;
    if (nSkins > 0 || nWorld > 0) {
        modified = ModGenerator::buildModifiedItemsGame(
            g_itemDb.getRawContent(), g_appliedSkins, g_itemDb, &rep);

        if (modified.empty()) {
            g_modStatus = "Build failed: " + (rep.firstError.empty() ? "parse error" : rep.firstError);
            g_modStatusColor = C::bad;
            Toast("items_game build failed", C::bad);
            return;
        }
    }

    std::vector<ModGenerator::ExtraEntry> extra;
    int spellWritten = 0;
    for (const auto& sp : g_spellOverrides) {
        auto bytes = VPKManager::readFileBytes(sp.second);
        if (!bytes || bytes->empty()) continue;
        extra.push_back({ sp.first, std::move(*bytes) });
        ++spellWritten;
    }

    if (!ModGenerator::writeModFile(*dotaPath, modified, extra, &err)) {
        g_modStatus = "Write failed: " + err;
        g_modStatusColor = C::bad;
        Toast("Could not write VPK", C::bad);
        return;
    }

    if (!ModGenerator::updateSignatures(*dotaPath, &err)) {
        g_modStatus = "Signature update failed: " + err;
        g_modStatusColor = C::bad;
        Toast("Signature update failed", C::bad);
        return;
    }

    bool missingBody = false;
    for (const auto& hp : g_appliedSkins) {
        for (const auto& pick : hp.second) {
            int body = FindSetBody(pick.itemIdx);
            if (body < 0) continue;
            bool have = false;
            for (const auto& q : hp.second) if (q.itemIdx == body) { have = true; break; }
            if (!have) { missingBody = true; break; }
        }
        if (missingBody) break;
    }

    char buf[240];
    char spellTxt[48] = "";
    if (spellWritten > 0)
        snprintf(spellTxt, sizeof(spellTxt), " + %d spell icons", spellWritten);

    if (missingBody)
        snprintf(buf, sizeof(buf),
            "%d skins + %d world%s written - WARNING: a set piece has no arcana body",
            rep.heroSlots, rep.worldSlots, spellTxt);
    else if (rep.shadowed > 0)
        snprintf(buf, sizeof(buf), "%d skins + %d world%s written (%d overridden by a newer pick)",
            rep.heroSlots, rep.worldSlots, spellTxt, rep.shadowed);
    else
        snprintf(buf, sizeof(buf), "%d skins + %d world%s written - restart Dota 2",
            rep.heroSlots, rep.worldSlots, spellTxt);

    g_modStatus = buf;
    g_modStatusColor = missingBody ? C::warn : C::good;
    Toast(buf, missingBody ? C::warn : C::good);
}

static void ApplyBoundSlot(int slot)
{
    if (!g_dbLoaded.load()) {
        Toast("Still loading the item database", C::warn);
        return;
    }

    auto it = g_binds.find(slot);
    if (it == g_binds.end()) {
        Toast("Ctrl+Alt+" + std::to_string(slot + 1) + " is not bound", C::warn);
        return;
    }

    if (!PresetLoad(it->second)) {
        Toast("Could not load \"" + it->second + "\"", C::bad);
        return;
    }

    DiagLog::add("[bind] applied loadout: " + it->second);
    Toast("Hotkey: loaded \"" + it->second + "\"", C::good);

    if (g_bindAutoGenerate) RunGenerate();
}

static float CreditBadgeW()
{
    ImVec2 s1 = TxtSz(g_fSemi, 13.f, "coded by");
    ImVec2 s2 = TxtSz(g_fBold, 15.f, "Mamad");
    return 26.f + s1.x + 6.f + s2.x + 14.f;
}

static void DrawCreditBadge(ImDrawList* dl, ImVec2 center, float alpha = 1.f)
{
    const float w = CreditBadgeW();
    const float hgt = 28.f;
    ImVec2 pa(center.x - w * 0.5f, center.y - hgt * 0.5f);
    ImVec2 pb(center.x + w * 0.5f, center.y + hgt * 0.5f);

    dl->AddRectFilled(pa, pb, SetA(C::accent, (int)(24 * alpha)), hgt * 0.5f);
    dl->AddRect(pa, pb, SetA(C::accent, (int)(78 * alpha)), hgt * 0.5f, 0, 1.f);

    IcoDiamond(dl, ImVec2(pa.x + 14.f, center.y), 4.5f, SetA(C::accent2, (int)(235 * alpha)));

    ImVec2 s1 = TxtSz(g_fSemi, 13.f, "coded by");
    ImVec2 s2 = TxtSz(g_fBold, 15.f, "Mamad");

    Txt(dl, g_fSemi, 13.f, ImVec2(pa.x + 25.f, center.y - s1.y * 0.5f),
        SetA(C::text2, (int)(228 * alpha)), "coded by");
    Txt(dl, g_fBold, 15.f, ImVec2(pa.x + 25.f + s1.x + 6.f, center.y - s2.y * 0.5f),
        SetA(C::accent2, (int)(255 * alpha)), "Mamad");
}

static void DrawTitleBar(HWND hwnd, float width, float t)
{
    const float H = 58.f;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = p, b = ImVec2(p.x + width, p.y + H);

    dl->AddRectFilledMultiColor(a, b, IM_COL32(13, 13, 20, 255), IM_COL32(13, 13, 20, 255),
        IM_COL32(9, 9, 14, 255), IM_COL32(9, 9, 14, 255));
    dl->AddLine(ImVec2(a.x, b.y - 0.5f), ImVec2(b.x, b.y - 0.5f), C::lineSoft, 1.f);

    DrawLogo(dl, ImVec2(p.x + 34.f, p.y + H * 0.5f), 15.f, t);

    Txt(dl, g_fBold, 20.f, ImVec2(p.x + 62.f, p.y + 10.f), C::text, "Dota Cheat");
    Txt(dl, g_fSmall, 10.5f, ImVec2(p.x + 63.f, p.y + 34.f), SetA(C::dim, 220),
        "D O T A   2   C L I E N T   S K I N   M A N A G E R");

    DrawCreditBadge(dl, ImVec2(b.x - 128.f - CreditBadgeW() * 0.5f, p.y + H * 0.5f));

    float bw = 34.f, bh = 26.f;
    float bx = b.x - 14.f - bw * 2.f - 8.f;
    float by = p.y + (H - bh) * 0.5f;

    {
        ImGui::PushID("##win_min");
        ImGui::SetCursorScreenPos(ImVec2(bx, by));
        bool clicked = ImGui::InvisibleButton("##m", ImVec2(bw, bh));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        float hv = AnimStep(ImGui::GetID("##win_min"), hov, 18.f);
        if (hv > 0.01f)
            dl->AddRectFilled(ImVec2(bx, by), ImVec2(bx + bw, by + bh),
                SetA(C::panel3, (int)(220 * hv)), 7.f);
        dl->AddLine(ImVec2(bx + 12.f, by + bh * 0.5f), ImVec2(bx + 22.f, by + bh * 0.5f),
            SetA(C::text2, (int)(170 + 85 * hv)), 1.6f);
        if (clicked) ShowWindow(hwnd, SW_MINIMIZE);
    }

    {
        ImGui::PushID("##win_close");
        ImGui::SetCursorScreenPos(ImVec2(bx + bw + 8.f, by));
        bool clicked = ImGui::InvisibleButton("##c", ImVec2(bw, bh));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        float hv = AnimStep(ImGui::GetID("##win_close"), hov, 18.f);
        ImVec2 q0(bx + bw + 8.f, by), q1(q0.x + bw, by + bh);
        if (hv > 0.01f) {
            dl->AddRectFilled(q0, q1, SetA(C::bad, (int)(70 + 150 * hv)), 7.f);
            SoftGlow(dl, ImVec2((q0.x + q1.x) * 0.5f, (q0.y + q1.y) * 0.5f), 24.f, C::bad, 6, (int)(6 * hv));
        }
        ImU32 cc = SetA(hv > 0.5f ? IM_COL32(255, 255, 255, 255) : C::text2, 255);
        dl->AddLine(ImVec2(q0.x + 12.f, q0.y + 8.f), ImVec2(q0.x + 22.f, q0.y + bh - 8.f), cc, 1.7f);
        dl->AddLine(ImVec2(q0.x + 22.f, q0.y + 8.f), ImVec2(q0.x + 12.f, q0.y + bh - 8.f), cc, 1.7f);
        if (clicked) PostMessage(hwnd, WM_CLOSE, 0, 0);
    }

    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + H));
    ImGui::Dummy(ImVec2(1.f, 1.f));
}

static float g_navSel[7] = { 0, 0, 0, 0, 0, 0, 0 };
static float g_navHov[7] = { 0, 0, 0, 0, 0, 0, 0 };

static void DrawSidebar(float width, float height, float t)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImVec2 a = p, b = ImVec2(p.x + width, p.y + height);

    dl->AddRectFilled(a, b, IM_COL32(11, 11, 17, 255));
    dl->AddLine(ImVec2(b.x - 0.5f, a.y), ImVec2(b.x - 0.5f, b.y), C::lineSoft, 1.f);

    float y = p.y + 18.f;
    Txt(dl, g_fSmall, 10.5f, ImVec2(p.x + 24.f, y), SetA(C::dim, 200), "N A V I G A T I O N");
    y += 24.f;

    struct NavItem { const char* label; const char* sub; };
    NavItem nav[7] = {
        { "Skins",    "hero loadouts" },
        { "Spells",   "icons & effects" },
        { "World",    "couriers, wards" },
        { "Camera",   "zoom & view" },
        { "Loadouts", "saved presets" },
        { "Settings", "preferences" },
        { "About",    "credits" }
    };

    float dt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
    float k = 1.f - expf(-16.f * dt);

    for (int i = 0; i < 7; ++i) {
        ImVec2 ra(p.x + 12.f, y);
        ImVec2 rb(p.x + width - 12.f, y + 46.f);

        ImGui::PushID(i);
        ImGui::SetCursorScreenPos(ra);
        bool clicked = ImGui::InvisibleButton("##nav", ImVec2(rb.x - ra.x, rb.y - ra.y));
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();

        bool sel = (g_currentTab == i);
        g_navSel[i] += ((sel ? 1.f : 0.f) - g_navSel[i]) * k;
        g_navHov[i] += ((hov ? 1.f : 0.f) - g_navHov[i]) * k;

        float s = g_navSel[i], h = g_navHov[i];

        if (s > 0.01f) {
            dl->AddRectFilled(ra, rb, SetA(C::accent, (int)(38 * s)), 10.f);
            dl->AddRect(ra, rb, SetA(C::accent, (int)(110 * s)), 10.f, 0, 1.f);
            dl->AddRectFilled(ImVec2(ra.x, ra.y + 12.f), ImVec2(ra.x + 3.f, rb.y - 12.f),
                SetA(C::accent2, (int)(255 * s)), 2.f);
            SoftGlow(dl, ImVec2(ra.x + 26.f, (ra.y + rb.y) * 0.5f), 30.f, C::accent, 6, (int)(6 * s));
        }
        else if (h > 0.01f) {
            dl->AddRectFilled(ra, rb, SetA(C::panel2, (int)(200 * h)), 10.f);
        }

        ImU32 icol = sel ? C::accent2 : SetA(C::text2, (int)(170 + 85 * h));
        ImVec2 ic(ra.x + 28.f, (ra.y + rb.y) * 0.5f);
        switch (i) {
        case 0: IcoSkins(dl, ic, 11.f, icol); break;
        case 1: IcoSpell(dl, ic, 11.f, icol); break;
        case 2: IcoWorld(dl, ic, 11.f, icol); break;
        case 3: IcoCamera(dl, ic, 11.f, icol); break;
        case 4: IcoLoadout(dl, ic, 11.f, icol); break;
        case 5: IcoGear(dl, ic, 11.f, icol);  break;
        default: IcoInfo(dl, ic, 11.f, icol); break;
        }

        Txt(dl, g_fSemi, 14.f, ImVec2(ra.x + 52.f, ra.y + 11.f),
            sel ? C::text : SetA(C::text, (int)(190 + 65 * h)), nav[i].label);
        Txt(dl, g_fSmall, 11.f, ImVec2(ra.x + 52.f, ra.y + 30.f), SetA(C::dim, 210), nav[i].sub);

        if (clicked) {
            g_currentTab = i;
            g_selectedItemIdx = -1;
        }

        y += 52.f;
    }

    float cardY = b.y - 172.f;
    ImVec2 ca(p.x + 12.f, cardY), cb(p.x + width - 12.f, cardY + 96.f);
    dl->AddRectFilled(ca, cb, SetA(C::panel, 255), 10.f);
    dl->AddRect(ca, cb, SetA(C::line, 200), 10.f, 0, 1.f);

    bool ok = g_dbLoaded.load();
    bool done = g_dbLoadingDone.load();
    ImU32 dot = !done ? C::warn : (ok ? C::good : C::bad);
    float pulse = 0.5f + 0.5f * sinf(t * 3.f);
    dl->AddCircleFilled(ImVec2(ca.x + 16.f, ca.y + 20.f), 4.f, dot, 12);
    if (!done) SoftGlow(dl, ImVec2(ca.x + 16.f, ca.y + 20.f), 14.f, dot, 5, (int)(6 * pulse));

    Txt(dl, g_fSmall, 10.5f, ImVec2(ca.x + 28.f, ca.y + 14.f), SetA(C::dim, 220), "DATABASE");
    Txt(dl, g_fSemi, 12.5f, ImVec2(ca.x + 28.f, ca.y + 28.f),
        done ? (ok ? C::text : C::bad) : C::warn,
        done ? (ok ? "Ready" : "Failed") : "Loading...");

    if (done && ok) {
        char buf[96];
        snprintf(buf, sizeof(buf), "%d items", (int)g_itemDb.getItems().size());
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 16.f, ca.y + 50.f), SetA(C::text2, 220), buf);
        snprintf(buf, sizeof(buf), "%d heroes", (int)g_itemDb.getHeroes().size());
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 100.f, ca.y + 50.f), SetA(C::text2, 220), buf);

        int nSkins = 0, nWorld = 0;
        CountAllApplied(nSkins, nWorld);

        dl->AddLine(ImVec2(ca.x + 14.f, ca.y + 62.f), ImVec2(cb.x - 14.f, ca.y + 62.f),
            SetA(C::line, 150), 1.f);

        char s1[32], s2[32];
        snprintf(s1, sizeof(s1), "%d skins", nSkins);
        snprintf(s2, sizeof(s2), "%d world", nWorld);
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 16.f, ca.y + 70.f),
            nSkins > 0 ? SetA(C::good, 235) : SetA(C::dim, 190), s1);
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 86.f, ca.y + 70.f),
            nWorld > 0 ? SetA(C::accent2, 235) : SetA(C::dim, 190), s2);
        Txt(dl, g_fSmall, 10.f, ImVec2(ca.x + 156.f, ca.y + 70.f), SetA(C::dim, 170),
            (nSkins + nWorld) > 0 ? "both included in one build" : "nothing selected");
    }
    else if (!done) {
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 16.f, ca.y + 50.f), SetA(C::dim, 220),
            "reading items_game.txt");
    }
    else {
        Txt(dl, g_fSmall, 11.f, ImVec2(ca.x + 16.f, ca.y + 50.f), SetA(C::bad, 200),
            Ellipsize(g_fSmall, 11.f, g_dbStatus, width - 46.f).c_str());
    }

    DrawCreditBadge(dl, ImVec2(p.x + width * 0.5f, b.y - 26.f), 0.92f);
}

static int DrawItemGrid(const std::vector<size_t>& indices, const std::string& heroKey,
    float viewW, float listH, const std::function<bool(const ItemInfo&)>& filter)
{
    (void)listH;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float CW = 172.f, CH = 202.f, GAP = 12.f;

    float gridW = viewW - ImGui::GetStyle().ScrollbarSize - 2.f;
    if (gridW < 40.f) gridW = 40.f;

    int perRow = (int)((gridW + GAP) / (CW + GAP));
    if (perRow < 1) perRow = 1;

    float cw = CW;
    if (cw > gridW) cw = gridW;

    static std::vector<size_t> vis;
    vis.clear();
    vis.reserve(indices.size());
    for (size_t idx : indices) {
        const auto& item = g_itemDb.getItems()[idx];
        if (filter && !filter(item)) continue;
        vis.push_back(idx);
    }

    ImVec2 origin = ImGui::GetCursorScreenPos();
    const int shown = (int)vis.size();

    if (shown == 0) {
        Txt(dl, g_fSemi, 14.f, ImVec2(origin.x + 4.f, origin.y + 8.f), SetA(C::dim, 220),
            "Nothing matches the current filter.");
        ImGui::SetCursorScreenPos(origin);
        ImGui::Dummy(ImVec2(gridW, 36.f));
        return 0;
    }

    const int rows = (shown + perRow - 1) / perRow;
    const float contentH = rows * CH + (rows - 1) * GAP;

    const ImVec2 clipMin = dl->GetClipRectMin();
    const ImVec2 clipMax = dl->GetClipRectMax();

    for (int i = 0; i < shown; ++i) {
        const int r = i / perRow;
        const int c = i % perRow;
        const float x = origin.x + c * (cw + GAP);
        const float y = origin.y + r * (CH + GAP);

        if (y > clipMax.y + 8.f) break;
        if (y + CH < clipMin.y - 8.f) continue;

        const size_t idx = vis[i];
        const auto& item = g_itemDb.getItems()[idx];
        const bool isSel = ((int)idx == g_selectedItemIdx);
        const bool isApp = IsItemApplied(heroKey, (int)idx);

        const int holder = AppliedInSlot(heroKey, SlotKeyOf(item));
        const bool slotBusy = (holder >= 0 && holder != (int)idx);

        ImGui::SetCursorScreenPos(ImVec2(x, y));
        if (DrawItemCard(item, isSel, isApp, slotBusy, (int)idx, ImVec2(x, y), ImVec2(cw, CH)))
            g_selectedItemIdx = isSel ? -1 : (int)idx;
    }

    ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + contentH));
    ImGui::Dummy(ImVec2(std::max(gridW, 1.f), 1.f));

    return shown;
}

static void DrawSkinsTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!g_dbLoadingDone.load() || !g_dbLoaded.load()) {
        const char* msg = !g_dbLoadingDone.load() ? "Loading item database..."
            : "Database failed to load.";
        ImVec2 c = ImGui::GetCursorScreenPos();
        ImVec2 ts = TxtSz(g_fBold, 16.f, msg);
        Txt(dl, g_fBold, 16.f, ImVec2(c.x + (w - ts.x) * 0.5f, c.y + h * 0.5f - 20.f),
            SetA(C::text2, 220), msg);
        if (g_dbLoadingDone.load()) {
            std::string s = Ellipsize(g_fSmall, 12.f, g_dbStatus, w - 80.f);
            ImVec2 s2 = TxtSz(g_fSmall, 12.f, s.c_str());
            Txt(dl, g_fSmall, 12.f, ImVec2(c.x + (w - s2.x) * 0.5f, c.y + h * 0.5f + 6.f),
                SetA(C::bad, 200), s.c_str());
        }
        return;
    }

    const float LEFT_W = 276.f, GAP = 14.f;
    float rightW = w - LEFT_W - GAP;
    if (rightW < 260.f) rightW = 260.f;

    ImVec2 start = ImGui::GetCursorScreenPos();

    // Hero list
    {
        ImVec2 a = start, b = ImVec2(start.x + LEFT_W, start.y + h);
        dl->AddRectFilled(a, b, C::panel, 12.f);
        dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

        ImGui::SetCursorScreenPos(ImVec2(a.x + 16.f, a.y + 16.f));
        Txt(dl, g_fSemi, 12.f, ImVec2(a.x + 16.f, a.y + 16.f), SetA(C::text2, 230), "HEROES");
        char cnt[32];
        snprintf(cnt, sizeof(cnt), "%d", (int)g_itemDb.getHeroes().size());
        ImVec2 cs = TxtSz(g_fSmall, 11.f, cnt);
        Txt(dl, g_fSmall, 11.f, ImVec2(b.x - 16.f - cs.x, a.y + 17.f), SetA(C::dim, 220), cnt);

        ImGui::SetCursorScreenPos(ImVec2(a.x + 16.f, a.y + 42.f));
        SearchBox("##herosearch", "Search heroes...", g_heroSearch, sizeof(g_heroSearch),
            LEFT_W - 32.f);

        float listTop = a.y + 92.f;
        ImGui::SetCursorScreenPos(ImVec2(a.x + 8.f, listTop));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##heroScroll", ImVec2(LEFT_W - 16.f, h - 108.f), false,
            ImGuiWindowFlags_NoBackground);

        ImDrawList* cdl = ImGui::GetWindowDrawList();

        std::string hf = ToLower(g_heroSearch);
        const float ROW_H = 46.f;
        float rowW = LEFT_W - 16.f - 8.f;
        static std::unordered_map<int, float> heroAnim;

        for (size_t i = 0; i < g_itemDb.getHeroes().size(); ++i) {
            const auto& hero = g_itemDb.getHeroes()[i];
            if (!hf.empty() && ToLower(hero.displayName).find(hf) == std::string::npos) continue;

            ImVec2 rp = ImGui::GetCursorScreenPos();

            ImGui::PushID((int)i);
            bool clicked = ImGui::InvisibleButton("##hrow", ImVec2(rowW, ROW_H));
            bool hov = ImGui::IsItemHovered();
            ImGui::PopID();

            bool sel = ((int)i == g_selectedHeroIdx);
            float& av = heroAnim[(int)i];
            float dtt = std::min(ImGui::GetIO().DeltaTime, 0.05f);
            av += ((sel ? 1.f : (hov ? 0.45f : 0.f)) - av) * (1.f - expf(-16.f * dtt));

            ImVec2 ra = rp, rb(rp.x + rowW, rp.y + ROW_H - 2.f);
            if (av > 0.01f)
                cdl->AddRectFilled(ra, rb, SetA(sel ? C::accent : C::panel3, (int)(46 * av)), 8.f);
            if (sel)
                cdl->AddRectFilled(ImVec2(ra.x, ra.y + 10.f), ImVec2(ra.x + 2.5f, rb.y - 10.f),
                    C::accent2, 2.f);

            ImVec2 ac(rp.x + 26.f, rp.y + ROW_H * 0.5f - 1.f);
            ID3D11ShaderResourceView* psrv = nullptr;
            if (g_showPortraits) {
                const std::string path = "heroes/" + hero.internalName;
                psrv = IconManager::peek(path);
                if (!psrv && IconManager::ready()) IconManager::request(path);
            }
            if (psrv) {
                cdl->AddCircleFilled(ac, 17.f, IM_COL32(8, 8, 12, 255), 28);
                cdl->AddImageRounded((ImTextureID)(intptr_t)psrv,
                    ImVec2(ac.x - 16.f, ac.y - 16.f), ImVec2(ac.x + 16.f, ac.y + 16.f),
                    ImVec2(0, 0), ImVec2(1, 1), IM_COL32(255, 255, 255, 255), 16.f);
                cdl->AddCircle(ac, 17.f, SetA(sel ? C::accent2 : C::line, 220), 28, 1.2f);
            }
            else {
                cdl->AddCircleFilled(ac, 16.f, SetA(sel ? C::accent : C::panel3, 255), 28);
                char ini[2] = { (char)toupper(hero.displayName.empty() ? '?' : hero.displayName[0]), 0 };
                ImVec2 ts = TxtSz(g_fSemi, 13.f, ini);
                Txt(cdl, g_fSemi, 13.f, ImVec2(ac.x - ts.x * 0.5f, ac.y - ts.y * 0.5f),
                    sel ? C::text : SetA(C::text2, 220), ini);
            }

            int applied = CountAppliedForHero(hero.internalName);
            if (applied > 0) {
                cdl->AddCircleFilled(ImVec2(ac.x + 12.f, ac.y + 12.f), 7.f, IM_COL32(10, 14, 11, 255), 16);
                cdl->AddCircleFilled(ImVec2(ac.x + 12.f, ac.y + 12.f), 5.5f, C::good, 16);
            }

            std::string nm = Ellipsize(g_fSemi, 13.f, hero.displayName, rowW - 84.f);
            Txt(cdl, g_fSemi, 13.f, ImVec2(rp.x + 50.f, rp.y + 8.f),
                sel ? C::text : SetA(C::text, (int)(200 + 55 * av)), nm.c_str());

            char sub[64];
            if (applied > 0) {
                int cap = HeroSlotCapacity(hero.internalName);
                if (applied > cap)
                    snprintf(sub, sizeof(sub), "%d picks  -  %d slots", applied, cap);
                else
                    snprintf(sub, sizeof(sub), "%d / %d slots  -  %d items", applied, cap,
                        (int)hero.itemIndices.size());
            }
            else snprintf(sub, sizeof(sub), "%d items", (int)hero.itemIndices.size());
            Txt(cdl, g_fSmall, 11.f, ImVec2(rp.x + 50.f, rp.y + 26.f),
                (applied > HeroSlotCapacity(hero.internalName) ? SetA(C::warn, 225) : SetA(C::dim, 215)),
                sub);

            if (hero.arcanaCount > 0) {
                ImVec2 chipP(rp.x + rowW - 26.f, rp.y + 14.f);
                cdl->AddRectFilled(chipP, ImVec2(chipP.x + 20.f, chipP.y + 17.f), SetA(C::gold, 30), 4.f);
                Txt(cdl, g_fSmall, 10.f, ImVec2(chipP.x + 5.f, chipP.y + 2.f), SetA(C::gold, 230), "A");
            }

            if (clicked) { g_selectedHeroIdx = (int)i; g_selectedItemIdx = -1; }
        }

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    ImVec2 rp(start.x + LEFT_W + GAP, start.y);
    ImVec2 ra = rp, rb(rp.x + rightW, rp.y + h);
    dl->AddRectFilled(ra, rb, C::panel, 12.f);
    dl->AddRect(ra, rb, SetA(C::line, 180), 12.f, 0, 1.f);

    if (g_selectedHeroIdx < 0) {
        const char* m = "Select a hero to browse skins";
        ImVec2 ts = TxtSz(g_fSemi, 14.f, m);
        Txt(dl, g_fSemi, 14.f, ImVec2(ra.x + (rightW - ts.x) * 0.5f, ra.y + h * 0.5f - 10.f),
            SetA(C::dim, 200), m);
        const char* m2 = "Every skin is client-side only";
        ImVec2 ts2 = TxtSz(g_fSmall, 11.5f, m2);
        Txt(dl, g_fSmall, 11.5f, ImVec2(ra.x + (rightW - ts2.x) * 0.5f, ra.y + h * 0.5f + 12.f),
            SetA(C::dim, 140), m2);
        return;
    }

    const auto& hero = g_itemDb.getHeroes()[g_selectedHeroIdx];

    // header
    Txt(dl, g_fBold, 22.f, ImVec2(ra.x + 22.f, ra.y + 16.f), C::text, hero.displayName.c_str());
    {
        char b1[32], b2[32], b3[32];
        snprintf(b1, sizeof(b1), "%d items", (int)hero.itemIndices.size());
        snprintf(b2, sizeof(b2), "%d arcana", (int)hero.arcanaCount);
        int ap = CountAppliedForHero(hero.internalName);
        int cap = HeroSlotCapacity(hero.internalName);
        if (cap < 1) cap = 1;
        if (ap > cap) snprintf(b3, sizeof(b3), "%d picks / %d slots", ap, cap);
        else          snprintf(b3, sizeof(b3), "%d / %d slots", ap, cap);

        ImVec2 hsz = TxtSz(g_fBold, 22.f, hero.displayName.c_str());
        float cx = ra.x + 22.f + hsz.x + 16.f;
        float cy = ra.y + 22.f;
        Chip(dl, ImVec2(cx, cy), b1, C::accent2);
        cx += TxtSz(g_fSmall, 12.f, b1).x + 26.f;
        if (hero.arcanaCount > 0) {
            Chip(dl, ImVec2(cx, cy), b2, C::gold);
            cx += TxtSz(g_fSmall, 12.f, b2).x + 26.f;
        }
        if (ap >= cap)      Chip(dl, ImVec2(cx, cy), b3, C::warn);
        else if (ap > 0)    Chip(dl, ImVec2(cx, cy), b3, C::good);
        else                Chip(dl, ImVec2(cx, cy), b3, SetA(C::text2, 235));
    }

    // toolbar
    float toolbarY = ra.y + 58.f;
    const float PILL_ZONE = 340.f;
    float searchW = rightW - 44.f - PILL_ZONE - 12.f;
    if (searchW < 150.f) searchW = 150.f;

    ImGui::SetCursorScreenPos(ImVec2(ra.x + 22.f, toolbarY));
    SearchBox("##itemsearch", "Search skins...", g_itemSearch, sizeof(g_itemSearch), searchW);

    {
        ImVec2 pp(ra.x + 22.f + searchW + 12.f, toolbarY + 5.f);
        ImGui::SetCursorScreenPos(pp);
        Pill("All", &g_itemFilter, 0);
        Pill("Arcana", &g_itemFilter, 1);
        Pill("Immortal", &g_itemFilter, 2);
        Pill("Persona", &g_itemFilter, 3);
        Pill("Other", &g_itemFilter, 4);
    }

    dl->AddLine(ImVec2(ra.x + 22.f, toolbarY + 52.f), ImVec2(rb.x - 22.f, toolbarY + 52.f),
        C::lineSoft, 1.f);

    // action bar
    const float ACTION_H = 134.f;
    float gridTop = toolbarY + 64.f;
    float gridH = (ra.y + h - ACTION_H - 12.f) - gridTop;
    if (gridH < 120.f) gridH = 120.f;

    ImGui::SetCursorScreenPos(ImVec2(ra.x + 22.f, gridTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##itemScroll", ImVec2(rightW - 44.f, gridH), false,
        ImGuiWindowFlags_NoBackground);
    {
        std::string f = ToLower(g_itemSearch);
        DrawItemGrid(hero.itemIndices, hero.internalName, rightW - 44.f, gridH,
            [&](const ItemInfo& it) {
                if (g_itemFilter == 1 && !it.isArcana) return false;
                if (g_itemFilter == 2 && !it.isImmortal) return false;
                if (g_itemFilter == 3 && !it.isPersona) return false;
                if (g_itemFilter == 4 && (it.isArcana || it.isImmortal || it.isPersona)) return false;
                if (!f.empty()) {
                    std::string hay = ToLower(it.name + " " + it.prefab + " " + it.slot);
                    if (hay.find(f) == std::string::npos) return false;
                }
                return true;
            });
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // action bar panel
    float ay = ra.y + h - ACTION_H;
    ImVec2 aa(ra.x + 16.f, ay), ab(rb.x - 16.f, rb.y - 14.f);
    dl->AddRectFilled(aa, ab, C::panel2, 10.f);
    dl->AddRect(aa, ab, SetA(C::line, 190), 10.f, 0, 1.f);

    // selected preview
    ImVec2 sp(aa.x + 14.f, aa.y + 14.f);
    if (g_selectedItemIdx >= 0 && g_selectedItemIdx < (int)g_itemDb.getItems().size()) {
        const auto& sel = g_itemDb.getItems()[g_selectedItemIdx];
        ImVec2 ib(sp.x + 64.f, sp.y + 64.f);
        dl->AddRectFilled(sp, ib, IM_COL32(6, 6, 10, 235), 8.f);
        ID3D11ShaderResourceView* srv = IconManager::peek(sel.imageInventory);
        if (!srv && IconManager::ready()) IconManager::request(sel.imageInventory);
        if (srv) dl->AddImage((ImTextureID)(intptr_t)srv, sp, ib);
        dl->AddRect(sp, ib, SetA(RarityColor(sel), 180), 8.f, 0, 1.f);

        std::string nm = Ellipsize(g_fSemi, 14.f, sel.name, rightW - 520.f);
        Txt(dl, g_fSemi, 14.f, ImVec2(ib.x + 14.f, sp.y + 4.f), C::text, nm.c_str());
        Txt(dl, g_fSmall, 11.5f, ImVec2(ib.x + 14.f, sp.y + 24.f), SetA(RarityColor(sel), 230),
            RarityTag(sel));

        const std::string selSlot = SlotKeyOf(sel);
        const int         holder = AppliedInSlot(hero.internalName, selSlot);
        const int         stacked = SlotPickCount(hero.internalName, selSlot);
        const bool        isApp = IsItemApplied(hero.internalName, g_selectedItemIdx);
        const bool        replace = (!isApp && holder >= 0 && holder != g_selectedItemIdx);

        {
            const std::string sl = "slot: " + selSlot;
            Txt(dl, g_fSmall, 11.5f, ImVec2(ib.x + 14.f, sp.y + 42.f), SetA(C::dim, 220), sl.c_str());
            float tx = ib.x + 14.f + TxtSz(g_fSmall, 11.5f, sl.c_str()).x + 12.f;

            if (replace) {
                const std::string who = Ellipsize(g_fSmall, 11.f,
                    "in use by \"" + g_itemDb.getItems()[holder].name + "\"", 240.f);
                Txt(dl, g_fSmall, 11.f, ImVec2(tx, sp.y + 42.f), SetA(C::warn, 235), who.c_str());
            }
            else if (stacked > 1) {
                char sb[64];
                snprintf(sb, sizeof(sb), "%d picks share this slot", stacked);
                Txt(dl, g_fSmall, 11.f, ImVec2(tx, sp.y + 42.f), SetA(C::text2, 220), sb);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(ib.x + 14.f, aa.y + 84.f));
        if (isApp) {
            if (GhostButton("##remove", "REMOVE SKIN", ImVec2(140.f, 30.f), C::bad)) {
                ToggleItemApplied(hero.internalName, g_selectedItemIdx);
                Toast("Removed from " + selSlot, C::text2);
            }
        }
        else if (replace) {
            if (GhostButton("##apply", "REPLACE SLOT", ImVec2(140.f, 30.f), C::warn)) {
                const std::string oldName = Ellipsize(g_fSemi, 13.f,
                    g_itemDb.getItems()[holder].name, 150.f);
                ToggleItemApplied(hero.internalName, g_selectedItemIdx);
                Toast("Replaced \"" + oldName + "\" in slot " + selSlot, C::warn);
            }
        }
        else {
            if (GradientButton("##apply", "APPLY SKIN", ImVec2(140.f, 30.f), true)) {
                ToggleItemApplied(hero.internalName, g_selectedItemIdx);
                Toast("Applied to slot " + selSlot, C::good);
            }
        }

        int styleOwner = -1;
        if (sel.hasStyleChoice) styleOwner = g_selectedItemIdx;
        else {
            int bodyIdx = FindSetBody(g_selectedItemIdx);
            if (bodyIdx >= 0 && g_itemDb.getItems()[bodyIdx].hasStyleChoice) styleOwner = bodyIdx;
        }

        if (styleOwner >= 0) {
            const ItemInfo& stItem = g_itemDb.getItems()[styleOwner];
            const int cur = StyleOf(hero.internalName, styleOwner);
            const int active = (cur >= 0) ? cur : stItem.styles.front().id;

            const float sx = ib.x + 14.f + 152.f;
            Txt(dl, g_fSmall, 10.f, ImVec2(sx, aa.y + 76.f), SetA(C::dim, 215),
                styleOwner == g_selectedItemIdx ? "STYLE" : "BODY STYLE");

            float bx2 = sx + (styleOwner == g_selectedItemIdx ? 42.f : 70.f);
            for (size_t si = 0; si < stItem.styles.size() && si < 8; ++si) {
                const StyleInfo& st = stItem.styles[si];
                ImVec2 c(bx2 + 11.f, aa.y + 96.f);

                ImGui::PushID((int)(9000 + si));
                ImGui::SetCursorScreenPos(ImVec2(bx2, aa.y + 85.f));
                bool hit = ImGui::InvisibleButton("##st", ImVec2(22.f, 22.f));
                bool hovS = ImGui::IsItemHovered();
                ImGui::PopID();

                ImU32 col = HexCol(st.swatch, SetA(C::accent, 235));
                if (st.id == active) dl->AddCircle(c, 11.5f, C::text, 28, 1.8f);
                dl->AddCircleFilled(c, 8.f, col, 24);

                if (hovS) {
                    dl->AddCircle(c, 12.5f, SetA(C::accent2, 200), 28, 1.4f);
                    ImGui::SetTooltip("%s", st.name.c_str());
                }
                if (hit) {
                    SetStyleFor(hero.internalName, styleOwner, st.id);
                    Toast("Style: " + st.name, C::accent2);
                }
                bx2 += 26.f;
            }
        }
    }
    else {
        Txt(dl, g_fSmall, 12.f, ImVec2(sp.x, sp.y + 24.f), SetA(C::dim, 200),
            "Click a skin card to select it");
        Txt(dl, g_fSmall, 11.f, ImVec2(sp.x, sp.y + 44.f), SetA(C::dim, 140),
            "hover a card for details");
    }

    {
        float bx = ab.x - 14.f - 190.f;
        ImGui::SetCursorScreenPos(ImVec2(bx, aa.y + 22.f));
        if (GradientButton("##gen", "GENERATE MOD", ImVec2(190.f, 40.f), true))
            RunGenerate();

        ImGui::SetCursorScreenPos(ImVec2(bx - 132.f, aa.y + 24.f));
        if (GhostButton("##restore", "RESTORE ALL", ImVec2(120.f, 34.f), C::warn)) {
            g_appliedSkins[hero.internalName].clear();
            AppliedSave();
            Toast("All skins restored for this hero", C::warn);
        }

        if (!g_modStatus.empty()) {
            std::string st = Ellipsize(g_fSmall, 11.f, g_modStatus, 200.f);
            ImVec2 ss = TxtSz(g_fSmall, 11.f, st.c_str());
            Txt(dl, g_fSmall, 11.f, ImVec2(ab.x - 14.f - ss.x, aa.y + 70.f), g_modStatusColor,
                st.c_str());
        }
    }
}

static void DrawWorldTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!g_dbLoadingDone.load() || !g_dbLoaded.load()) {
        const char* msg = "Loading item database...";
        ImVec2 c = ImGui::GetCursorScreenPos();
        ImVec2 ts = TxtSz(g_fBold, 16.f, msg);
        Txt(dl, g_fBold, 16.f, ImVec2(c.x + (w - ts.x) * 0.5f, c.y + h * 0.5f), SetA(C::text2, 220), msg);
        return;
    }

    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    Txt(dl, g_fBold, 22.f, ImVec2(a.x + 22.f, a.y + 16.f), C::text, "World Catalog");
    {
        char c1[32];
        snprintf(c1, sizeof(c1), "%d items", (int)g_itemDb.getWorldItems().size());
        Chip(dl, ImVec2(a.x + 22.f + TxtSz(g_fBold, 22.f, "World Catalog").x + 16.f, a.y + 22.f),
            c1, C::accent2);
    }
    Txt(dl, g_fSmall, 11.5f, ImVec2(a.x + 22.f, a.y + 46.f), SetA(C::dim, 210),
        "Couriers, wards, terrain, weather, music, announcers, effects");

    float toolbarY = a.y + 72.f;
    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, toolbarY));
    SearchBox("##worldsearch", "Search world items...", g_itemSearch, sizeof(g_itemSearch), 360.f);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 398.f, toolbarY + 6.f));
    if (GhostButton("##wgen", "GENERATE MOD", ImVec2(160.f, 30.f), C::accent)) RunGenerate();

    dl->AddLine(ImVec2(a.x + 22.f, toolbarY + 52.f), ImVec2(b.x - 22.f, toolbarY + 52.f),
        C::lineSoft, 1.f);

    const float ACTION_H = 92.f;
    float gridTop = toolbarY + 64.f;
    float gridH = (a.y + h - ACTION_H - 12.f) - gridTop;
    if (gridH < 120.f) gridH = 120.f;

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, gridTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##worldScroll", ImVec2(w - 44.f, gridH), false, ImGuiWindowFlags_NoBackground);
    {
        std::string f = ToLower(g_itemSearch);
        DrawItemGrid(g_itemDb.getWorldItems(), "__world__", w - 44.f, gridH,
            [&](const ItemInfo& it) {
                if (f.empty()) return true;
                std::string hay = ToLower(it.name + " " + it.prefab);
                return hay.find(f) != std::string::npos;
            });
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    float ay = a.y + h - ACTION_H;
    ImVec2 aa(a.x + 16.f, ay), ab(b.x - 16.f, b.y - 14.f);
    dl->AddRectFilled(aa, ab, C::panel2, 10.f);
    dl->AddRect(aa, ab, SetA(C::line, 190), 10.f, 0, 1.f);

    int ap = CountAppliedForHero("__world__");
    char info[64];
    snprintf(info, sizeof(info), "%d applied", ap);
    Txt(dl, g_fSemi, 13.f, ImVec2(aa.x + 16.f, aa.y + 20.f), SetA(C::text2, 230), info);

    if (g_selectedItemIdx >= 0 && g_selectedItemIdx < (int)g_itemDb.getItems().size()) {
        const auto& sel = g_itemDb.getItems()[g_selectedItemIdx];
        const std::string wslot = SlotKeyOf(sel);
        const int  wholder = AppliedInSlot("__world__", wslot);
        const bool wrepl = (wholder >= 0 && wholder != g_selectedItemIdx);

        std::string nm = Ellipsize(g_fSemi, 13.f, sel.name, 320.f);
        Txt(dl, g_fSemi, 13.f, ImVec2(aa.x + 110.f, aa.y + 20.f), C::text, nm.c_str());
        Txt(dl, g_fSmall, 11.f, ImVec2(aa.x + 16.f, aa.y + 46.f), SetA(C::dim, 210),
            (std::string("slot: ") + wslot).c_str());

        ImGui::SetCursorScreenPos(ImVec2(aa.x + 110.f, aa.y + 42.f));
        if (IsItemApplied("__world__", g_selectedItemIdx)) {
            if (GhostButton("##wrem", "REMOVE", ImVec2(110.f, 28.f), C::bad))
                ToggleItemApplied("__world__", g_selectedItemIdx);
        }
        else if (wrepl) {
            if (GhostButton("##wapp", "REPLACE", ImVec2(110.f, 28.f), C::warn)) {
                const std::string oldName = Ellipsize(g_fSemi, 13.f,
                    g_itemDb.getItems()[wholder].name, 140.f);
                ToggleItemApplied("__world__", g_selectedItemIdx);
                Toast("Replaced \"" + oldName + "\" in slot " + wslot, C::warn);
            }
        }
        else {
            if (GradientButton("##wapp", "APPLY", ImVec2(110.f, 28.f), true)) {
                ToggleItemApplied("__world__", g_selectedItemIdx);
                Toast("Applied to slot " + wslot, C::good);
            }
        }
    }
    else {
        Txt(dl, g_fSmall, 11.5f, ImVec2(aa.x + 110.f, aa.y + 24.f), SetA(C::dim, 190),
            "Select an item to apply");
    }

    if (!g_modStatus.empty()) {
        std::string st = Ellipsize(g_fSmall, 11.f, g_modStatus, 260.f);
        ImVec2 ss = TxtSz(g_fSmall, 11.f, st.c_str());
        Txt(dl, g_fSmall, 11.f, ImVec2(ab.x - 16.f - ss.x, aa.y + 22.f), g_modStatusColor, st.c_str());
    }
}

static void EnsureSpellDb()
{
    if (g_spellDb.loaded() || g_spellTried) return;
    g_spellTried = true;
    if (!VPKManager::isOpen()) return;

    std::vector<std::string> names;
    const auto& heroes = g_itemDb.getHeroes();
    names.reserve(heroes.size());
    for (const auto& h : heroes) names.push_back(h.internalName);

    g_spellDb.build(names);

    if (g_spellDb.loaded() && g_spellHero.empty()) {
        auto groups = g_spellDb.heroGroups();
        if (!groups.empty()) g_spellHero = groups.front();
    }
}

static bool DrawSpellCard(const SpellIcon& ic, int idx, bool selected, bool changed,
    ImVec2 pos, ImVec2 size)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImGui::PushID(idx);
    ImGui::SetCursorScreenPos(pos);
    bool clicked = ImGui::InvisibleButton("##sc", size);
    bool hov = ImGui::IsItemHovered();
    ImGui::PopID();

    ImVec2 a = pos;
    ImVec2 b(pos.x + size.x, pos.y + size.y);

    ImU32 top = selected ? SetA(C::accent, 46) : (changed ? SetA(C::good, 28) : SetA(C::panel2, 255));
    ImU32 bot = selected ? SetA(C::accent, 18) : (changed ? SetA(C::good, 10) : SetA(C::panel, 255));
    dl->AddRectFilledMultiColor(a, b, top, top, bot, bot);

    ImVec2 ia(a.x + 8.f, a.y + 8.f);
    ImVec2 ib(b.x - 8.f, b.y - 28.f);
    dl->AddRectFilled(ia, ib, IM_COL32(6, 6, 10, 235), 7.f);

    ID3D11ShaderResourceView* srv = IconManager::peek(ic.path);
    if (!srv && IconManager::ready()) IconManager::request(ic.path);
    if (srv) {
        dl->AddImage((ImTextureID)(intptr_t)srv, ia, ib);
    }
    else {
        std::string initial = ic.label.empty() ? "?" : std::string(1, (char)toupper(ic.label[0]));
        ImVec2 ts = TxtSz(g_fHuge, 26.f, initial.c_str());
        Txt(dl, g_fHuge, 26.f,
            ImVec2((ia.x + ib.x - ts.x) * 0.5f, (ia.y + ib.y - ts.y) * 0.5f),
            SetA(C::dim, 150), initial.c_str());
    }

    dl->AddRect(a, b, selected ? C::accent : SetA(C::line, 150), 10.f, 0, selected ? 2.f : 1.2f);

    std::string nm = Ellipsize(g_fSemi, 11.5f, ic.label, size.x - 16.f);
    Txt(dl, g_fSemi, 11.5f, ImVec2(a.x + 8.f, b.y - 21.f), C::text, nm.c_str());

    if (changed) {
        ImVec2 ts = TxtSz(g_fSmall, 9.f, "MOD");
        ImVec2 ca(b.x - 8.f - ts.x - 10.f, a.y + 6.f);
        ImVec2 cb(b.x - 8.f, ca.y + 14.f);
        dl->AddRectFilled(ca, cb, SetA(C::good, 40), 5.f);
        dl->AddRect(ca, cb, SetA(C::good, 160), 5.f, 0, 1.f);
        Txt(dl, g_fSmall, 9.f, ImVec2(ca.x + 5.f, ca.y + (14.f - ts.y) * 0.5f), C::good, "MOD");
    }

    if (hov) {
        ImGui::SetTooltip("%s\n%s", ic.label.c_str(), ic.base.c_str());
    }
    return clicked;
}

static void DrawSpellTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (!g_dbLoadingDone.load() || !g_dbLoaded.load()) {
        const char* msg = "Loading item database...";
        ImVec2 c = ImGui::GetCursorScreenPos();
        ImVec2 ts = TxtSz(g_fBold, 16.f, msg);
        Txt(dl, g_fBold, 16.f, ImVec2(c.x + (w - ts.x) * 0.5f, c.y + h * 0.5f),
            SetA(C::text2, 220), msg);
        return;
    }

    EnsureSpellDb();

    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    if (!g_spellDb.loaded()) {
        const char* msg = "Could not index spell icons from the game VPK.";
        ImVec2 ts = TxtSz(g_fBold, 15.f, msg);
        Txt(dl, g_fBold, 15.f, ImVec2(a.x + (w - ts.x) * 0.5f, a.y + h * 0.5f),
            SetA(C::bad, 220), msg);
        return;
    }

    const bool pickMode = !g_spellPickFor.empty();
    const int  swapped  = (int)g_spellOverrides.size();

    // ---- header ----
    const char* title = pickMode ? "Pick Source Icon" : "Spell Workshop";
    Txt(dl, g_fBold, 22.f, ImVec2(a.x + 22.f, a.y + 16.f), C::text, title);
    {
        char c1[64];
        if (pickMode) snprintf(c1, sizeof(c1), "%d icons", g_spellDb.count());
        else          snprintf(c1, sizeof(c1), "%d abilities", g_spellDb.targetCount());
        Chip(dl, ImVec2(a.x + 22.f + TxtSz(g_fBold, 22.f, title).x + 16.f, a.y + 22.f),
            c1, pickMode ? C::accent2 : C::accent);
    }
    Txt(dl, g_fSmall, 11.5f, ImVec2(a.x + 22.f, a.y + 46.f), SetA(C::dim, 210),
        pickMode ? "Click any icon to assign it to the selected ability"
                 : "Swap ability icons - default, arcana, immortal and TI variants");

    // ---- toolbar ----
    float toolbarY = a.y + 72.f;
    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, toolbarY));
    SearchBox("##spellsearch", "Search abilities...", g_spellSearch, sizeof(g_spellSearch), 320.f);

    if (pickMode) {
        ImGui::SetCursorScreenPos(ImVec2(a.x + 358.f, toolbarY + 6.f));
        if (GhostButton("##scancel", "CANCEL", ImVec2(100.f, 30.f), C::bad))
            g_spellPickFor.clear();
    }
    else {
        ImGui::SetCursorScreenPos(ImVec2(a.x + 358.f, toolbarY));
        ImGui::SetNextItemWidth(210.f);
        if (ImGui::BeginCombo("##spellhero", g_spellHero.empty() ? "All heroes" : g_spellHero.c_str())) {
            for (const auto& g : g_spellDb.heroGroups()) {
                bool sel = (g == g_spellHero);
                if (ImGui::Selectable(g.c_str(), sel)) {
                    g_spellHero = g;
                    g_spellSel = -1;
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SetCursorScreenPos(ImVec2(a.x + 584.f, toolbarY + 6.f));
        if (GhostButton("##sgen", "GENERATE MOD", ImVec2(160.f, 30.f), C::accent)) RunGenerate();
    }

    dl->AddLine(ImVec2(a.x + 22.f, toolbarY + 52.f), ImVec2(b.x - 22.f, toolbarY + 52.f),
        C::lineSoft, 1.f);

    // ---- selection ----
    std::vector<int> vis;
    const std::string f = ToLower(g_spellSearch);
    const bool global = pickMode || !f.empty();

    if (global) {
        for (int i = 0; i < g_spellDb.count(); ++i) {
            const SpellIcon* ic = g_spellDb.at(i);
            if (!ic) continue;
            if (!pickMode && !ic->target) continue;
            if (!f.empty()) {
                std::string hay = ToLower(ic->base + " " + ic->label + " " + ic->hero);
                if (hay.find(f) == std::string::npos) continue;
            }
            vis.push_back(i);
        }
    }
    else {
        vis = g_spellDb.forHero(g_spellHero, true);
    }

    const float ACTION_H = 92.f;
    float gridTop = toolbarY + 64.f;
    float gridH = (a.y + h - ACTION_H - 12.f) - gridTop;
    if (gridH < 120.f) gridH = 120.f;

    const float CW = 128.f, CH = 126.f, GAP = 12.f;
    const float gw = w - 44.f;
    const int perRow = std::max(1, (int)((gw + GAP) / (CW + GAP)));

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, gridTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##spellScroll", ImVec2(gw, gridH), false, ImGuiWindowFlags_NoBackground);
    {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        if (vis.empty()) {
            Txt(dl, g_fSemi, 14.f, ImVec2(origin.x + 4.f, origin.y + 8.f), SetA(C::dim, 220),
                "No ability icons match.");
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(gw, 36.f));
        }
        else {
            const int rows = ((int)vis.size() + perRow - 1) / perRow;
            const float contentH = rows * CH + (rows - 1) * GAP;

            const ImVec2 clipMin = dl->GetClipRectMin();
            const ImVec2 clipMax = dl->GetClipRectMax();

            for (int i = 0; i < (int)vis.size(); ++i) {
                const int r = i / perRow;
                const int c = i % perRow;
                const float x = origin.x + c * (CW + GAP);
                const float y = origin.y + r * (CH + GAP);

                if (y > clipMax.y + 8.f) break;
                if (y + CH < clipMin.y - 8.f) continue;

                const int idx = vis[(size_t)i];
                const SpellIcon* ic = g_spellDb.at(idx);
                if (!ic) continue;

                const bool changed = g_spellOverrides.count(ic->path) > 0;
                const bool sel     = (!pickMode && idx == g_spellSel);

                if (DrawSpellCard(*ic, idx, sel, changed, ImVec2(x, y), ImVec2(CW, CH))) {
                    if (pickMode) {
                        const std::string target = g_spellPickFor;
                        if (ic->path == target) g_spellOverrides.erase(target);
                        else                    g_spellOverrides[target] = ic->path;
                        g_spellPickFor.clear();
                        AppliedSave();
                        Toast("Icon updated", C::good);
                    }
                    else {
                        g_spellSel = sel ? -1 : idx;
                    }
                }
            }

            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + contentH));
            ImGui::Dummy(ImVec2(std::max(gw, 1.f), 1.f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    float ay = a.y + h - ACTION_H;
    ImVec2 aa(a.x + 16.f, ay), ab(b.x - 16.f, b.y - 14.f);
    dl->AddRectFilled(aa, ab, C::panel2, 10.f);
    dl->AddRect(aa, ab, SetA(C::line, 190), 10.f, 0, 1.f);

    char info[64];
    snprintf(info, sizeof(info), "%d swapped", swapped);
    Txt(dl, g_fSemi, 13.f, ImVec2(aa.x + 16.f, aa.y + 20.f), SetA(C::text2, 230), info);

    const SpellIcon* selIc = g_spellDb.at(g_spellSel);
    if (selIc) {
        std::string nm = Ellipsize(g_fSemi, 13.f, selIc->label, 300.f);
        Txt(dl, g_fSemi, 13.f, ImVec2(aa.x + 110.f, aa.y + 20.f), C::text, nm.c_str());

        auto ov = g_spellOverrides.find(selIc->path);
        std::string sub = "original icon";
        if (ov != g_spellOverrides.end()) {
            const int si = g_spellDb.indexOf(ov->second);
            const SpellIcon* src = g_spellDb.at(si);
            sub = "using: " + (src ? src->label : std::string("custom"));
        }
        Txt(dl, g_fSmall, 11.f, ImVec2(aa.x + 110.f, aa.y + 46.f),
            ov != g_spellOverrides.end() ? C::good : SetA(C::dim, 210), sub.c_str());

        ImGui::SetCursorScreenPos(ImVec2(aa.x + 16.f, aa.y + 42.f));
        if (GradientButton("##spick", "PICK ICON", ImVec2(120.f, 28.f), true))
            g_spellPickFor = selIc->path;

        if (ov != g_spellOverrides.end()) {
            ImGui::SetCursorScreenPos(ImVec2(aa.x + 146.f, aa.y + 42.f));
            if (GhostButton("##sreset", "RESET", ImVec2(90.f, 28.f), C::bad)) {
                g_spellOverrides.erase(selIc->path);
                AppliedSave();
                Toast("Icon restored", C::warn);
            }
        }
    }
    else {
        Txt(dl, g_fSmall, 11.5f, ImVec2(aa.x + 110.f, aa.y + 24.f), SetA(C::dim, 190),
            "Select an ability to swap its icon");
    }

    if (!g_modStatus.empty()) {
        std::string st = Ellipsize(g_fSmall, 11.f, g_modStatus, 260.f);
        ImVec2 ss = TxtSz(g_fSmall, 11.f, st.c_str());
        Txt(dl, g_fSmall, 11.f, ImVec2(ab.x - 16.f - ss.x, aa.y + 22.f), g_modStatusColor, st.c_str());
    }
}


static void DrawCameraTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    ImVec2 a = ImGui::GetCursorScreenPos();
    float panelH = h < 560.f ? 560.f : h;
    ImVec2 b(a.x + w, a.y + panelH);
    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    Txt(dl, g_fBold, 22.f, ImVec2(a.x + 22.f, a.y + 14.f), C::text, "Camera");
    {
        char c1[48];
        snprintf(c1, sizeof(c1), "%d", g_camDistance);
        Chip(dl, ImVec2(a.x + 22.f + TxtSz(g_fBold, 22.f, "Camera").x + 16.f, a.y + 20.f),
            c1, C::accent);
    }
    Txt(dl, g_fSmall, 11.f, ImVec2(a.x + 22.f, a.y + 44.f), SetA(C::dim, 205),
        "Written to game/dota/cfg/autoexec.cfg - restart Dota 2 to apply");

    ImVec2 wa(a.x + 22.f, a.y + 66.f), wb(b.x - 22.f, a.y + 122.f);
    dl->AddRectFilled(wa, wb, SetA(C::warn, 18), 9.f);
    dl->AddRect(wa, wb, SetA(C::warn, 75), 9.f, 0, 1.f);
    Txt(dl, g_fSemi, 12.f, ImVec2(wa.x + 14.f, wa.y + 8.f), C::warn,
        "Why only some keys work");
    Txt(dl, g_fSmall, 11.f, ImVec2(wa.x + 14.f, wa.y + 28.f), SetA(C::text2, 225),
        "The game server caps how far the camera may zoom OUT - anything above the cap is silently ignored.");
    Txt(dl, g_fSmall, 11.f, ImVec2(wa.x + 14.f, wa.y + 44.f), SetA(C::dim, 205),
        "Zooming IN always applies. The ladder below finds the highest value your game actually accepts.");

    float sy = a.y + 138.f;
    Txt(dl, g_fSemi, 13.f, ImVec2(a.x + 22.f, sy + 6.f), C::text, "Camera distance");

    ImGui::SetCursorScreenPos(ImVec2(a.x + 190.f, sy));
    ImGui::SetNextItemWidth(340.f);
    ImGui::SliderInt("##camdist", &g_camDistance, 700, 1400, "%d");

    Txt(dl, g_fSmall, 10.5f, ImVec2(a.x + 190.f, sy + 28.f), SetA(C::dim, 190),
        "700 = closest    1134 = default at 16:9    1400 = widest (Demo Hero only)");

    float ky = a.y + 194.f;
    Txt(dl, g_fSemi, 12.f, ImVec2(a.x + 22.f, ky), C::accent2,
        "ZOOM-OUT LADDER - find the highest value your game accepts");

    {
        static const struct { const char* key; int dist; const char* tag; } kKeys[5] = {
            { "F5", 1200, "" },
            { "F6", 1250, "" },
            { "F7", 1300, "" },
            { "F8", 1400, "max" },
            { "F9", 1134, "reset" },
        };
        float kx = a.x + 22.f;
        float kyy = ky + 26.f;
        for (int i = 0; i < 5; ++i) {
            char lbl[56];
            if (kKeys[i].tag[0])
                snprintf(lbl, sizeof(lbl), "%s  %d  %s", kKeys[i].key, kKeys[i].dist, kKeys[i].tag);
            else
                snprintf(lbl, sizeof(lbl), "%s  %d", kKeys[i].key, kKeys[i].dist);
            Chip(dl, ImVec2(kx, kyy), lbl, (i == 4) ? C::text2 : C::accent2, 11.5f);
            kx += TxtSz(g_fSmall, 11.5f, lbl).x + 28.f;
        }
        Txt(dl, g_fSmall, 10.5f, ImVec2(a.x + 22.f, kyy + 30.f), SetA(C::dim, 195),
            "Press F9 first, then F5 to F8. The last one that moves the camera is your real maximum.");
    }

    float py = a.y + 274.f;

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, py));
    if (GhostButton("##cwheel", g_camWheelZoom ? "WHEEL ZOOM: ON" : "WHEEL ZOOM: OFF",
            ImVec2(190.f, 30.f), g_camWheelZoom ? C::good : C::text2)) {
        g_camWheelZoom = !g_camWheelZoom;
        CameraSave();
    }

    ImGui::SetCursorScreenPos(ImVec2(a.x + 222.f, py));
    if (GhostButton("##cshake", g_camNoShake ? "NO SHAKE: ON" : "NO SHAKE: OFF",
            ImVec2(170.f, 30.f), g_camNoShake ? C::good : C::text2)) {
        g_camNoShake = !g_camNoShake;
        CameraSave();
    }

    ImGui::SetCursorScreenPos(ImVec2(a.x + 402.f, py));
    if (GhostButton("##cbinds", g_camBinds ? "HOTKEYS: ON" : "HOTKEYS: OFF",
            ImVec2(170.f, 30.f), g_camBinds ? C::good : C::text2)) {
        g_camBinds = !g_camBinds;
        CameraSave();
    }

    float ay = a.y + 314.f;

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, ay));
    if (GradientButton("##capply", "APPLY TO GAME", ImVec2(180.f, 32.f), true)) {
        auto steamPath = SteamDetector::detectSteam();
        std::optional<fs::path> dotaPath;
        if (steamPath) dotaPath = SteamDetector::detectDota(*steamPath);

        if (!dotaPath) {
            Toast("Dota 2 installation not found", C::bad);
        }
        else {
            std::vector<std::string> lines;
            char l1[64];
            snprintf(l1, sizeof(l1), "dota_camera_distance %d", g_camDistance);
            lines.push_back(l1);
            lines.push_back(std::string("dota_camera_disable_zoom ") + (g_camWheelZoom ? "0" : "1"));
            if (g_camNoShake) lines.push_back("dota_screen_shake 0");
            if (g_camBinds) {
                lines.push_back("bind F5 \"dota_camera_distance 1200\"");
                lines.push_back("bind F6 \"dota_camera_distance 1250\"");
                lines.push_back("bind F7 \"dota_camera_distance 1300\"");
                lines.push_back("bind F8 \"dota_camera_distance 1400\"");
                lines.push_back("bind F9 \"dota_camera_distance 1134\"");
            }

            std::string err;
            fs::path out;
            if (ModGenerator::writeAutoexec(*dotaPath, lines, &out, &err)) {
                g_camPath = out.string();
                CameraSave();
                g_modStatus = "Camera settings written - restart Dota 2";
                g_modStatusColor = C::good;
                Toast("Camera settings written - restart Dota 2", C::good);
            }
            else {
                Toast("Could not write autoexec.cfg: " + err, C::bad);
            }
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(a.x + 214.f, ay));
    if (GhostButton("##cremove", "REMOVE", ImVec2(110.f, 32.f), C::bad)) {
        auto steamPath = SteamDetector::detectSteam();
        std::optional<fs::path> dotaPath;
        if (steamPath) dotaPath = SteamDetector::detectDota(*steamPath);

        if (!dotaPath) {
            Toast("Dota 2 installation not found", C::bad);
        }
        else {
            std::string err;
            if (ModGenerator::clearAutoexec(*dotaPath, &err)) {
                g_camPath.clear();
                Toast("Camera block removed from autoexec.cfg", C::warn);
            }
            else {
                Toast("Could not clean autoexec.cfg: " + err, C::bad);
            }
        }
    }

    if (!g_camPath.empty()) {
        std::string p = Ellipsize(g_fSmall, 10.5f, g_camPath, w - 400.f);
        Txt(dl, g_fSmall, 10.5f, ImVec2(a.x + 340.f, ay + 10.f), SetA(C::good, 215), p.c_str());
    }

    float ly = a.y + 360.f;
    ImVec2 ka(a.x + 22.f, ly), kb(b.x - 22.f, ly + 118.f);
    dl->AddRectFilled(ka, kb, SetA(C::good, 12), 9.f);
    dl->AddRect(ka, kb, SetA(C::good, 60), 9.f, 0, 1.f);

    Txt(dl, g_fSemi, 12.f, ImVec2(ka.x + 14.f, ka.y + 8.f), C::good,
        "LIVE - can be changed while Dota is running");
    Txt(dl, g_fSmall, 11.f, ImVec2(ka.x + 14.f, ka.y + 28.f), SetA(C::text2, 220),
        "autoexec.cfg is only read at startup. To reload it without restarting, run   exec autoexec.cfg");
    Txt(dl, g_fSmall, 11.f, ImVec2(ka.x + 14.f, ka.y + 44.f), SetA(C::dim, 200),
        "For the console and exec, add this once in Steam > Dota 2 > Properties > Launch Options:   -console +exec autoexec.cfg");

    ImGui::SetCursorScreenPos(ImVec2(ka.x + 14.f, ka.y + 70.f));
    if (GhostButton("##cl1", "COPY  exec autoexec.cfg", ImVec2(224.f, 28.f), C::good)) {
        ImGui::SetClipboardText("exec autoexec.cfg");
        Toast("Copied - paste into the console", C::good);
    }

    ImGui::SetCursorScreenPos(ImVec2(ka.x + 248.f, ka.y + 70.f));
    if (GhostButton("##cl2", "COPY  1400 (wide)", ImVec2(160.f, 28.f), C::accent2)) {
        ImGui::SetClipboardText("dota_camera_distance 1400");
        Toast("Copied - wide view", C::good);
    }

    ImGui::SetCursorScreenPos(ImVec2(ka.x + 418.f, ka.y + 70.f));
    if (GhostButton("##cl3", "COPY  1134 (default)", ImVec2(176.f, 28.f), C::text2)) {
        ImGui::SetClipboardText("dota_camera_distance 1134");
        Toast("Copied - default view", C::good);
    }

    float ny = kb.y + 14.f;
    ImVec2 na(a.x + 22.f, ny), nb(b.x - 22.f, ny + 46.f);
    dl->AddRectFilled(na, nb, SetA(C::panel3, 170), 9.f);
    dl->AddRect(na, nb, SetA(C::warn, 55), 9.f, 0, 1.f);
    Txt(dl, g_fSemi, 12.f, ImVec2(na.x + 14.f, na.y + 7.f), C::warn,
        "This section is not finished yet - an update is coming soon.");
    Txt(dl, g_fSmall, 10.5f, ImVec2(na.x + 14.f, na.y + 24.f), SetA(C::dim, 200),
        "Zoom-out beyond your default is still capped by the game server. Work on a proper fix is in progress.");
}

static void DrawLoadoutsTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();

    static std::vector<PresetEntry> s_list;
    if (g_presetsDirty) {
        s_list = PresetScan();
        g_presetsDirty = false;
    }

    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + w, a.y + h);
    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    Txt(dl, g_fBold, 22.f, ImVec2(a.x + 22.f, a.y + 16.f), C::text, "Loadouts");
    {
        char c1[48];
        snprintf(c1, sizeof(c1), "%d saved", (int)s_list.size());
        Chip(dl, ImVec2(a.x + 22.f + TxtSz(g_fBold, 22.f, "Loadouts").x + 16.f, a.y + 22.f),
            c1, C::accent2);
    }
    Txt(dl, g_fSmall, 11.5f, ImVec2(a.x + 22.f, a.y + 46.f), SetA(C::dim, 210),
        "Save the current look (skins + world + spells), then switch with Ctrl+Alt+1..9");

    float y = a.y + 78.f;
    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, y));
    NameBox("##pname", "Loadout name...", g_presetName, sizeof(g_presetName), 340.f);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 374.f, y + 4.f));
    if (GradientButton("##psave", "SAVE CURRENT", ImVec2(170.f, 30.f), true)) {
        std::string nm = PresetSafeName(g_presetName);
        if (nm.empty()) {
            Toast("Type a name first", C::warn);
        }
        else if (PresetSave(nm)) {
            Toast("Saved loadout \"" + nm + "\"", C::good);
            g_presetName[0] = 0;
        }
        else {
            Toast("Could not save loadout", C::bad);
        }
    }

    ImGui::SetCursorScreenPos(ImVec2(a.x + 560.f, y + 4.f));
    if (GhostButton("##pauto", g_bindAutoGenerate ? "AUTO BUILD: ON" : "AUTO BUILD: OFF",
            ImVec2(170.f, 30.f), g_bindAutoGenerate ? C::good : C::text2)) {
        g_bindAutoGenerate = !g_bindAutoGenerate;
        Toast(g_bindAutoGenerate ? "Hotkeys will rebuild the mod" : "Hotkeys will only load",
            C::accent2);
    }

    dl->AddLine(ImVec2(a.x + 22.f, y + 54.f), ImVec2(b.x - 22.f, y + 54.f), C::lineSoft, 1.f);

    float listTop = y + 66.f;
    float listH = (b.y - 22.f) - listTop;
    if (listH < 120.f) listH = 120.f;

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, listTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##loadouts", ImVec2(w - 44.f, listH), false, ImGuiWindowFlags_NoBackground);
    {
        ImVec2 origin = ImGui::GetCursorScreenPos();
        const float RH = 62.f, GAP = 10.f;
        const float rowW = std::max(w - 44.f, 1.f);

        if (s_list.empty()) {
            Txt(dl, g_fSemi, 14.f, ImVec2(origin.x + 4.f, origin.y + 8.f), SetA(C::dim, 220),
                "No loadouts yet - type a name above and hit SAVE CURRENT.");
            ImGui::SetCursorScreenPos(origin);
            ImGui::Dummy(ImVec2(rowW, 36.f));
        }
        else {
            const ImVec2 clipMin = dl->GetClipRectMin();
            const ImVec2 clipMax = dl->GetClipRectMax();

            for (int i = 0; i < (int)s_list.size(); ++i) {
                const float ry = origin.y + i * (RH + GAP);
                if (ry > clipMax.y + 8.f) break;
                if (ry + RH < clipMin.y - 8.f) continue;

                const PresetEntry& e = s_list[(size_t)i];
                ImVec2 ra(origin.x, ry), rb(origin.x + rowW, ry + RH);

                dl->AddRectFilled(ra, rb, C::panel2, 10.f);
                dl->AddRect(ra, rb, SetA(C::line, 180), 10.f, 0, 1.f);

                std::string nm = Ellipsize(g_fSemi, 14.f, e.name, rowW - 360.f);
                Txt(dl, g_fSemi, 14.f, ImVec2(ra.x + 16.f, ra.y + 12.f), C::text, nm.c_str());

                char sum[96];
                snprintf(sum, sizeof(sum), "%d skins   %d world   %d spells",
                    e.skins, e.world, e.spells);
                Txt(dl, g_fSmall, 11.f, ImVec2(ra.x + 16.f, ra.y + 34.f), SetA(C::dim, 215), sum);

                ImGui::PushID(i);

                const int bslot = BindSlotOf(e.name);
                char bindLabel[32];
                if (bslot >= 0) snprintf(bindLabel, sizeof(bindLabel), "Ctrl+Alt+%d", bslot + 1);
                else            snprintf(bindLabel, sizeof(bindLabel), "BIND");

                ImGui::SetCursorScreenPos(ImVec2(rb.x - 300.f, ra.y + 16.f));
                if (GhostButton("##pb", bindLabel, ImVec2(100.f, 30.f),
                        bslot >= 0 ? C::accent2 : C::text2)) {
                    if (bslot >= 0) {
                        g_binds.erase(bslot);
                        Toast("Unbound \"" + e.name + "\"", C::warn);
                    }
                    else {
                        int slot = -1;
                        for (int s = 0; s < BIND_SLOTS; ++s)
                            if (!g_binds.count(s)) { slot = s; break; }

                        if (slot < 0) Toast("All 9 hotkeys are already used", C::warn);
                        else {
                            g_binds[slot] = e.name;
                            Toast("Bound to Ctrl+Alt+" + std::to_string(slot + 1), C::good);
                        }
                    }
                    BindsSave();
                }

                ImGui::SetCursorScreenPos(ImVec2(rb.x - 196.f, ra.y + 16.f));
                if (GradientButton("##pl", "LOAD", ImVec2(96.f, 30.f), true)) {
                    if (PresetLoad(e.name)) {
                        Toast("Loaded \"" + e.name + "\" - now hit GENERATE", C::good);
                        g_presetsDirty = true;
                    }
                    else {
                        Toast("Could not load loadout", C::bad);
                    }
                }

                ImGui::SetCursorScreenPos(ImVec2(rb.x - 92.f, ra.y + 16.f));
                if (GhostButton("##pd", "DELETE", ImVec2(76.f, 30.f), C::bad)) {
                    PresetDelete(e.name);
                    Toast("Deleted \"" + e.name + "\"", C::warn);
                }

                ImGui::PopID();
            }

            ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + s_list.size() * (RH + GAP)));
            ImGui::Dummy(ImVec2(rowW, 1.f));
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawSettingsTab(float w, float h)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + w, a.y + h);

    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    Txt(dl, g_fBold, 22.f, ImVec2(a.x + 22.f, a.y + 16.f), C::text, "Settings");
    Txt(dl, g_fSmall, 11.5f, ImVec2(a.x + 22.f, a.y + 46.f), SetA(C::dim, 210),
        "Preferences are stored for this session only");

    float y = a.y + 78.f;
    float colW = (w - 60.f) * 0.5f;

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, y));
    Toggle("##auto", "Auto-update after patches", "Rebuild the mod when Dota 2 updates", &g_autoUpdate);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f, y + 68.f));
    Toggle("##anon", "Anonymous mode", "Hide applied skins from your profile", &g_anonymousMode);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f + colW + 16.f, y));
    Toggle("##port", "Hero portraits", "Decode hero images (slower first load)", &g_showPortraits);

    ImGui::SetCursorScreenPos(ImVec2(a.x + 22.f + colW + 16.f, y + 68.f));
    Toggle("##safemode", "Verify before write", "Validate the KV tree before packing", &g_verifyBeforeWrite);

    float sy = y + 148.f;
    ImVec2 sa(a.x + 22.f, sy), sb(b.x - 22.f, sy + 76.f);
    dl->AddRectFilled(sa, sb, C::panel2, 10.f);
    dl->AddRect(sa, sb, SetA(C::line, 190), 10.f, 0, 1.f);
    Txt(dl, g_fSemi, 13.f, ImVec2(sa.x + 16.f, sa.y + 14.f), C::text, "UI scale");
    Txt(dl, g_fSmall, 11.f, ImVec2(sa.x + 16.f, sa.y + 36.f), SetA(C::dim, 210),
        "Bigger text and icons on any monitor   -   Ctrl +   /   Ctrl -");

    {
        char pct[32];
        snprintf(pct, sizeof(pct), "%d%%", (int)(g_uiZoom * 100.f + 0.5f));
        ImVec2 ps = TxtSz(g_fBold, 20.f, pct);
        Txt(dl, g_fBold, 20.f, ImVec2(sb.x - 148.f - ps.x, sa.y + 27.f), C::accent2, pct);

        ImGui::SetCursorScreenPos(ImVec2(sb.x - 138.f, sa.y + 24.f));
        if (GhostButton("##zout", "-", ImVec2(38.f, 30.f), C::text2)) UiZoomStep(-1);

        ImGui::SetCursorScreenPos(ImVec2(sb.x - 96.f, sa.y + 24.f));
        if (GhostButton("##zin", "+", ImVec2(38.f, 30.f), C::accent)) UiZoomStep(+1);

        ImGui::SetCursorScreenPos(ImVec2(sb.x - 54.f, sa.y + 24.f));
        if (GhostButton("##zrst", "R", ImVec2(38.f, 30.f), C::dim)) UiZoomStep(0);
    }

    float dy = sy + 96.f;
    ImVec2 da(a.x + 22.f, dy), db(b.x - 22.f, dy + 112.f);
    dl->AddRectFilled(da, db, SetA(C::bad, 14), 10.f);
    dl->AddRect(da, db, SetA(C::bad, 70), 10.f, 0, 1.f);
    Txt(dl, g_fSemi, 13.f, ImVec2(da.x + 16.f, da.y + 14.f), SetA(C::bad, 240), "Danger zone");
    Txt(dl, g_fSmall, 11.f, ImVec2(da.x + 16.f, da.y + 38.f), SetA(C::dim, 210),
        "CLEAR ALL forgets every applied skin and spell icon");
    Txt(dl, g_fSmall, 11.f, ImVec2(da.x + 16.f, da.y + 58.f), SetA(C::dim, 210),
        "RESTORE undoes gameinfo, deletes the mod VPK and re-signs the game");

    ImGui::SetCursorScreenPos(ImVec2(db.x - 296.f, da.y + 22.f));
    if (GhostButton("##clearall", "CLEAR ALL", ImVec2(130.f, 30.f), C::bad)) {
        g_appliedSkins.clear();
        g_spellOverrides.clear();
        AppliedSave();
        Toast("All applied skins and spell icons cleared", C::warn);
    }

    static float s_restoreConfirm = 0.f;
    if (s_restoreConfirm > 0.f && (float)ImGui::GetTime() > s_restoreConfirm)
        s_restoreConfirm = 0.f;

    ImGui::SetCursorScreenPos(ImVec2(db.x - 156.f, da.y + 22.f));
    const char* rLabel = (s_restoreConfirm > 0.f) ? "CLICK AGAIN" : "RESTORE";
    if (GhostButton("##restore", rLabel, ImVec2(140.f, 30.f), C::bad)) {
        if (s_restoreConfirm <= 0.f) {
            s_restoreConfirm = (float)ImGui::GetTime() + 4.f;
            Toast("This touches real game files - click again to confirm", C::warn);
        }
        else {
            s_restoreConfirm = 0.f;

            auto steamPath = SteamDetector::detectSteam();
            std::optional<fs::path> dotaPath;
            if (steamPath) dotaPath = SteamDetector::detectDota(*steamPath);

            if (!dotaPath) {
                Toast("Dota 2 installation not found", C::bad);
            }
            else {
                std::string err;
                if (ModGenerator::restoreGameInfo(*dotaPath, &err)) {
                    g_modStatus = "Game restored - mod removed";
                    g_modStatusColor = C::good;
                    Toast("Game restored. Restart Dota 2.", C::good);
                }
                else {
                    g_modStatus = "Restore failed: " + err;
                    g_modStatusColor = C::bad;
                    Toast("Restore failed: " + err, C::bad);
                }
            }
        }
    }

    float gy = dy + 130.f;
    float gh = (b.y - 22.f) - gy;
    if (gh < 90.f) gh = 90.f;

    ImVec2 ga(a.x + 22.f, gy), gb(b.x - 22.f, gy + gh);
    dl->AddRectFilled(ga, gb, C::panel2, 10.f);
    dl->AddRect(ga, gb, SetA(C::line, 190), 10.f, 0, 1.f);

    Txt(dl, g_fSemi, 13.f, ImVec2(ga.x + 16.f, ga.y + 12.f), C::text, "Diagnostics");
    {
        char c1[48];
        snprintf(c1, sizeof(c1), "%d lines", (int)DiagLog::count());
        Txt(dl, g_fSmall, 11.f, ImVec2(ga.x + 104.f, ga.y + 14.f), SetA(C::dim, 210), c1);
    }

    ImGui::SetCursorScreenPos(ImVec2(gb.x - 96.f, ga.y + 8.f));
    if (GhostButton("##logclear", "CLEAR", ImVec2(80.f, 24.f), C::text2)) DiagLog::clear();

    const float logTop = ga.y + 34.f;
    ImGui::SetCursorScreenPos(ImVec2(ga.x + 12.f, logTop));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
    ImGui::BeginChild("##diag", ImVec2((gb.x - ga.x) - 24.f, (gb.y - 12.f) - logTop), false,
        ImGuiWindowFlags_NoBackground);
    {
        auto lines = DiagLog::snapshot(400);
        if (lines.empty()) {
            ImVec2 o = ImGui::GetCursorScreenPos();
            Txt(dl, g_fSmall, 11.f, ImVec2(o.x + 2.f, o.y + 2.f), SetA(C::dim, 200),
                "No activity yet - generate a mod and the log fills up.");
            ImGui::SetCursorScreenPos(o);
            ImGui::Dummy(ImVec2(10.f, 22.f));
        }
        else {
            ImGui::PushFont(g_fSmall, 10.5f);
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.f, 2.f));
            for (const auto& ln : lines) {
                ImGui::PushStyleColor(ImGuiCol_Text, SetA(C::text2, 225));
                ImGui::TextUnformatted(ln.c_str());
                ImGui::PopStyleColor();
            }
            ImGui::PopStyleVar();
            ImGui::PopFont();
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
}

static void DrawAboutTab(float w, float h, float t)
{
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 a = ImGui::GetCursorScreenPos();
    ImVec2 b(a.x + w, a.y + h);

    dl->AddRectFilled(a, b, C::panel, 12.f);
    dl->AddRect(a, b, SetA(C::line, 180), 12.f, 0, 1.f);

    SoftGlow(dl, ImVec2(a.x + w * 0.5f, a.y + 90.f), 150.f, C::accent, 12, 5);

    ImVec2 lc(a.x + w * 0.5f, a.y + 86.f);
    DrawLogo(dl, lc, 34.f, t);

    const char* title = "Dota Cheat";
    ImVec2 ts = TxtSz(g_fBold, 30.f, title);
    Txt(dl, g_fBold, 30.f, ImVec2(a.x + (w - ts.x) * 0.5f, a.y + 138.f), C::text, title);

    const char* sub = "Dota 2 client-side skin manager";
    ImVec2 ss = TxtSz(g_fSmall, 12.f, sub);
    Txt(dl, g_fSmall, 12.f, ImVec2(a.x + (w - ss.x) * 0.5f, a.y + 176.f), SetA(C::text2, 220), sub);

    ImVec2 ca(a.x + w * 0.5f - 150.f, a.y + 214.f);
    ImVec2 cb(a.x + w * 0.5f + 150.f, a.y + 292.f);
    dl->AddRectFilled(ca, cb, SetA(C::accent, 20), 12.f);
    dl->AddRect(ca, cb, SetA(C::accent, 90), 12.f, 0, 1.f);
    SoftGlow(dl, ImVec2(a.x + w * 0.5f, (ca.y + cb.y) * 0.5f), 110.f, C::accent, 8, 4);

    {
        const char* c1 = "c o d e d   b y";
        ImVec2 s1 = TxtSz(g_fSemi, 12.f, c1);
        float cy1 = ca.y + 15.f;
        Txt(dl, g_fSemi, 12.f, ImVec2(a.x + w * 0.5f - s1.x * 0.5f, cy1),
            SetA(C::text2, 215), c1);
        IcoDiamond(dl, ImVec2(a.x + w * 0.5f - s1.x * 0.5f - 13.f, cy1 + 7.f), 3.5f,
            SetA(C::accent, 170));
        IcoDiamond(dl, ImVec2(a.x + w * 0.5f + s1.x * 0.5f + 13.f, cy1 + 7.f), 3.5f,
            SetA(C::accent, 170));

        const char* c2 = "Mamad";
        ImVec2 s2 = TxtSz(g_fBold, 32.f, c2);
        SoftGlow(dl, ImVec2(a.x + w * 0.5f, ca.y + 52.f), 52.f, C::accent2, 8, 3);
        Txt(dl, g_fBold, 32.f, ImVec2(a.x + w * 0.5f - s2.x * 0.5f, ca.y + 34.f), C::accent2, c2);
    }

    // features
    float fy = cb.y + 22.f;
    const char* feats[] = {
        "My GitHub: PyScriptPlus",
        "My Telegram: @Mamadreal",
        "Written by Mohammad in C++ language"
    };
    for (int i = 0; i < 3; ++i) {
        float fx = a.x + 40.f;
        dl->AddCircleFilled(ImVec2(fx + 3.f, fy + 8.f), 2.5f, SetA(C::accent2, 230), 12);
        Txt(dl, g_fSmall, 12.f, ImVec2(fx + 16.f, fy), SetA(C::text2, 225), feats[i]);
        fy += 26.f;
    }

    // version chips
    {
        char v[64];
        snprintf(v, sizeof(v), "v0.1.0  -  build %s", __DATE__);
        Chip(dl, ImVec2(a.x + 40.f, b.y - 52.f), v, C::accent2, 11.f);
        Chip(dl, ImVec2(a.x + 40.f + TxtSz(g_fSmall, 11.f, v).x + 26.f, b.y - 52.f),
            "ImGui + DX11", C::accent, 11.f);
    }

    const char* disc = "Client-side only. Use at your own risk.";
    ImVec2 ds = TxtSz(g_fSmall, 11.f, disc);
    Txt(dl, g_fSmall, 11.f, ImVec2(b.x - 26.f - ds.x, b.y - 40.f), SetA(C::dim, 170), disc);
}

void ApplyModernTheme()
{
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    style.WindowRounding = 0.f;
    style.ChildRounding = 10.f;
    style.FrameRounding = 8.f;
    style.GrabRounding = 8.f;
    style.TabRounding = 8.f;
    style.ScrollbarRounding = 10.f;
    style.WindowPadding = ImVec2(0, 0);
    style.FramePadding = ImVec2(12, 8);
    style.ItemSpacing = ImVec2(12, 10);
    style.ItemInnerSpacing = ImVec2(8, 6);
    style.WindowBorderSize = 0.f;
    style.ChildBorderSize = 0.f;
    style.ScrollbarSize = 8.f;

    auto S = [](ImU32 u) {
        return ImVec4(((u >> 0) & 0xFF) / 255.f, ((u >> 8) & 0xFF) / 255.f,
            ((u >> 16) & 0xFF) / 255.f, ((u >> 24) & 0xFF) / 255.f);
        };

    c[ImGuiCol_Text] = S(C::text);
    c[ImGuiCol_TextDisabled] = S(C::dim);
    c[ImGuiCol_WindowBg] = S(C::bg0);
    c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_PopupBg] = S(C::panel2);
    c[ImGuiCol_Border] = S(C::line);
    c[ImGuiCol_FrameBg] = S(C::panel2);
    c[ImGuiCol_FrameBgHovered] = S(C::panel3);
    c[ImGuiCol_FrameBgActive] = S(C::panel3);
    c[ImGuiCol_TitleBg] = S(C::bg0);
    c[ImGuiCol_TitleBgActive] = S(C::bg0);
    c[ImGuiCol_MenuBarBg] = S(C::panel);
    c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab] = S(SetA(C::accent, 90));
    c[ImGuiCol_ScrollbarGrabHovered] = S(SetA(C::accent, 150));
    c[ImGuiCol_ScrollbarGrabActive] = S(C::accent);
    c[ImGuiCol_CheckMark] = S(C::accent2);
    c[ImGuiCol_SliderGrab] = S(C::accent);
    c[ImGuiCol_SliderGrabActive] = S(C::accent2);
    c[ImGuiCol_Button] = S(C::panel3);
    c[ImGuiCol_ButtonHovered] = S(C::accent);
    c[ImGuiCol_ButtonActive] = S(C::accent2);
    c[ImGuiCol_Header] = S(SetA(C::accent, 90));
    c[ImGuiCol_HeaderHovered] = S(SetA(C::accent, 140));
    c[ImGuiCol_HeaderActive] = S(C::accent);
    c[ImGuiCol_Separator] = S(C::lineSoft);
    c[ImGuiCol_Tab] = S(C::panel2);
    c[ImGuiCol_TabHovered] = S(SetA(C::accent, 160));
    c[ImGuiCol_PlotLines] = S(C::accent2);
    c[ImGuiCol_PlotHistogram] = S(C::accent);
    c[ImGuiCol_TextSelectedBg] = S(SetA(C::accent, 110));
}


static ImFont* LoadFont(ImGuiIO& io, const char* path, float size, bool bold)
{
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH = true;
    // No RasterizerDensity here on purpose: with a 1.92+ backend that supports
    // ImGuiBackendFlags_RendererHasTextures the atlas is re-rasterized at the
    // current FramebufferScale (= our g_uiScale), so glyphs are always baked at
    // exactly the number of pixels they occupy on screen.
    (void)bold;
    ImFont* f = io.Fonts->AddFontFromFileTTF(path, size, &cfg);
    return f;
}

static void LoadFonts(ImGuiIO& io)
{
    g_fBody = LoadFont(io, "C:\\Windows\\Fonts\\segoeui.ttf", 16.0f, false);
    g_fSmall = LoadFont(io, "C:\\Windows\\Fonts\\segoeui.ttf", 12.0f, false);
    g_fSemi = LoadFont(io, "C:\\Windows\\Fonts\\seguisb.ttf", 14.0f, false);
    g_fBold = LoadFont(io, "C:\\Windows\\Fonts\\segoeuib.ttf", 22.0f, true);
    g_fHuge = LoadFont(io, "C:\\Windows\\Fonts\\segoeuib.ttf", 34.0f, true);

    if (!g_fBody) {
        ImFontConfig d; d.SizePixels = 16.f;
        g_fBody = io.Fonts->AddFontDefault(&d);
    }
    if (!g_fSmall) g_fSmall = g_fBody;
    if (!g_fSemi)  g_fSemi = g_fBody;
    if (!g_fBold)  g_fBold = g_fBody;
    if (!g_fHuge)  g_fHuge = g_fBold;
}


static void MigrateOldDataDir()
{
    const char* appdata = getenv("APPDATA");
    if (!appdata || !*appdata) return;

    std::string oldDir = std::string(appdata) + "\\Dota2Chameleon";
    std::string newDir = std::string(appdata) + "\\dota2PyScriptPlus";

    if (GetFileAttributesA(newDir.c_str()) != INVALID_FILE_ATTRIBUTES) return;
    if (GetFileAttributesA(oldDir.c_str()) == INVALID_FILE_ATTRIBUTES) return;

    if (MoveFileA(oldDir.c_str(), newDir.c_str()))
        DiagLog::add("[migrate] settings moved: Dota2Chameleon -> dota2PyScriptPlus");
    else
        DiagLog::add("[migrate] could not move old settings folder");
}

int main(int, char**)
{
    MigrateOldDataDir();

    // Per-monitor DPI awareness. Without this Windows thinks the app only
    // understands 96 DPI and bitmap-stretches the whole window on a high-DPI
    // screen -- that stretch is exactly what made the text look blurry.
    ImGui_ImplWin32_EnableDpiAwareness();

    UiZoomLoad();   // remembered ui scale from a previous run

    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
                       GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
                       L"ChameleonCPP", nullptr };
    ::RegisterClassExW(&wc);

    // ---- size the window from the monitor work area, in real pixels ----
    RECT work{};
    ::SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    const int workW = (work.right - work.left) > 0 ? (work.right - work.left) : 1920;
    const int workH = (work.bottom - work.top) > 0 ? (work.bottom - work.top) : 1080;

    POINT center{ work.left + workW / 2, work.top + workH / 2 };
    float dpi0 = ImGui_ImplWin32_GetDpiScaleForMonitor(
        ::MonitorFromPoint(center, MONITOR_DEFAULTTOPRIMARY));
    if (dpi0 <= 0.f) dpi0 = 1.f;
    g_uiScale = dpi0 * g_uiZoom;

    int winW = (int)(1500.f * g_uiScale);
    int winH = (int)(1000.f * g_uiScale);
    if (winW > workW - 40) winW = workW - 40;
    if (winH > workH - 40) winH = workH - 40;
    if (winW < 900) winW = 900;
    if (winH < 700) winH = 700;
    int winX = work.left + (workW - winW) / 2;
    int winY = work.top + (workH - winH) / 2;

    HWND hwnd = ::CreateWindowExW(
        0, wc.lpszClassName, L"Dota Cheat",
        WS_POPUP | WS_MINIMIZEBOX,
        winX, winY, winW, winH,
        nullptr, nullptr, wc.hInstance, nullptr);

    int hotkeyFails = 0;
    for (int i = 0; i < BIND_SLOTS; ++i)
        if (!::RegisterHotKey(hwnd, BIND_ID_BASE + i,
                MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, (UINT)('1' + i)))
            ++hotkeyFails;

    DiagLog::add("[bind] hotkeys registered " + std::to_string(BIND_SLOTS - hotkeyFails) +
                 "/" + std::to_string(BIND_SLOTS) +
                 (hotkeyFails ? " (some are taken by another app)" : ""));


    RemoteConfig::start();

    if (!CreateDeviceD3D(hwnd)) {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }



    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigInputTrickleEventQueue = false;
    io.IniFilename = nullptr;

    LoadFonts(io);
    ApplyModernTheme();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    std::thread([]() {
        auto payload = new LoadPayload();
        auto steamPath = SteamDetector::detectSteam();
        if (!steamPath) payload->status = "Steam not found";
        else {
            auto dotaPath = SteamDetector::detectDota(*steamPath);
            if (!dotaPath) payload->status = "Dota 2 not found";
            else {
                auto vpkPath = VPKManager::findGameVPK(*dotaPath);
                if (!vpkPath) payload->status = "VPK not found";
                else {
                    payload->vpkPath = vpkPath->wstring();

                    VPKManager::openGameVPK(*vpkPath);

                    if (payload->db.loadFromVPK(*vpkPath, &payload->status)) {
                        payload->success = true;

                        std::vector<std::string> heroNames;
                        const auto& hs = payload->db.getHeroes();
                        heroNames.reserve(hs.size());
                        for (const auto& h : hs) heroNames.push_back(h.internalName);
                        payload->spellDb.build(heroNames);
                    }
                    else
                        payload->status = "Failed to load items_game.txt";
                }
            }
        }
        g_loadPayload.store(payload);
        }).detach();

    const float TITLE_H = 58.f;
    const float SIDE_W = 244.f;
    float t = 0.f;

    bool done = false;
    bool shown = false;
    while (!done)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) done = true;
        }
        if (done) break;

        if (auto* payload = g_loadPayload.exchange(nullptr)) {
            g_itemDb = std::move(payload->db);
            g_spellDb = std::move(payload->spellDb);
            g_dbStatus = payload->status;
            g_dbLoaded = payload->success;
            g_dbLoadingDone = true;

            if (g_spellDb.loaded()) {
                g_spellTried = true;
                auto groups = g_spellDb.heroGroups();
                if (!groups.empty() && g_spellHero.empty()) g_spellHero = groups.front();
            }

            if (g_dbLoaded) AppliedLoad();
            BindsLoad();
            CameraLoad();

            if (!payload->vpkPath.empty())
                IconManager::init(g_pd3dDevice, payload->vpkPath);

            delete payload;
        }

        if (int hk = g_hotkeySlot.exchange(-1); hk >= 0 && hk < BIND_SLOTS) {
            if (!RemoteConfig::mustUpdate())
                ApplyBoundSlot(hk);
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();

        {
            float dpi = ImGui_ImplWin32_GetDpiScaleForHwnd(hwnd);
            if (dpi <= 0.f) dpi = 1.f;
            g_uiScale = dpi * g_uiZoom;

            RECT cr{};
            ::GetClientRect(hwnd, &cr);
            const float cwpx = (float)(cr.right - cr.left);
            const float chpx = (float)(cr.bottom - cr.top);
            if (cwpx > 1.f && chpx > 1.f) {
                io.DisplaySize = ImVec2(cwpx / g_uiScale, chpx / g_uiScale);
                io.DisplayFramebufferScale = ImVec2(g_uiScale, g_uiScale);
            }

            HWND fg = ::GetForegroundWindow();
            POINT mp{};
            if (fg && (fg == hwnd || ::IsChild(fg, hwnd)) &&
                ::GetCursorPos(&mp) && ::ScreenToClient(hwnd, &mp))
                io.AddMousePosEvent((float)mp.x / g_uiScale, (float)mp.y / g_uiScale);
            else
                io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
        }

        ImGui::NewFrame();

        if (io.KeyCtrl && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))      UiZoomStep(+1);
            if (ImGui::IsKeyPressed(ImGuiKey_Minus, false) ||
                ImGui::IsKeyPressed(ImGuiKey_KeypadSubtract, false)) UiZoomStep(-1);
            if (ImGui::IsKeyPressed(ImGuiKey_0, false))              UiZoomStep(0);
        }

        t += std::min(io.DeltaTime, 0.05f);

        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->Pos);
        ImGui::SetNextWindowSize(vp->Size);

        ImGuiWindowFlags rootFlags =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("##root", nullptr, rootFlags);
        ImGui::PopStyleVar(3);

        ImDrawList* bg = ImGui::GetWindowDrawList();
        ImVec2 wp = ImGui::GetWindowPos();
        ImVec2 ws = ImGui::GetWindowSize();

        bg->AddRectFilledMultiColor(wp, ImVec2(wp.x + ws.x, wp.y + ws.y),
            C::bg1, C::bg0, C::bg0, C::bg1);
        SoftGlow(bg, ImVec2(wp.x + ws.x * 0.18f, wp.y + ws.y * 0.08f), 420.f, C::accent, 14, 4);
        SoftGlow(bg, ImVec2(wp.x + ws.x * 0.92f, wp.y + ws.y * 0.95f), 460.f, C::accent2, 14, 3);

        float fullW = ws.x, fullH = ws.y;

        ImGui::SetCursorScreenPos(wp);
        DrawTitleBar(hwnd, fullW, t);

        ImGui::SetCursorScreenPos(ImVec2(wp.x, wp.y + TITLE_H));
        DrawSidebar(SIDE_W, fullH - TITLE_H, t);

        float cx = wp.x + SIDE_W + 18.f;
        float cy = wp.y + TITLE_H + 18.f;
        float cw = fullW - SIDE_W - 36.f;
        float ch = fullH - TITLE_H - 36.f;

        ImGui::SetCursorScreenPos(ImVec2(cx, cy));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##content", ImVec2(cw, ch), false, ImGuiWindowFlags_NoBackground);
        {
            ImVec2 cp = ImGui::GetCursorScreenPos();
            switch (g_currentTab) {
            case 0: DrawSkinsTab(cw, ch);    break;
            case 1: DrawSpellTab(cw, ch);    break;
            case 2: DrawWorldTab(cw, ch);    break;
            case 3: DrawCameraTab(cw, ch);   break;
            case 4: DrawLoadoutsTab(cw, ch); break;
            case 5: DrawSettingsTab(cw, ch); break;
            case 6: DrawAboutTab(cw, ch, t); break;
            }
            ImGui::SetCursorScreenPos(cp);
            ImGui::Dummy(ImVec2(1.f, 1.f));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        if (g_toast.life > 0.f) {
            g_toast.life -= std::min(io.DeltaTime, 0.05f);
            float a = g_toast.life > 3.4f ? (4.0f - g_toast.life) / 0.6f : std::min(g_toast.life / 0.6f, 1.f);
            a = Clamp01(a);

            ImVec2 ts = TxtSz(g_fSemi, 13.f, g_toast.msg.c_str());
            float tw = ts.x + 56.f;
            ImVec2 ta(wp.x + fullW - tw - 26.f, wp.y + fullH - 62.f);
            ImVec2 tb(ta.x + tw, ta.y + 40.f);

            bg->AddRectFilled(ta, tb, SetA(C::panel3, (int)(240 * a)), 10.f);
            bg->AddRect(ta, tb, SetA(g_toast.color, (int)(140 * a)), 10.f, 0, 1.f);
            bg->AddRectFilled(ImVec2(ta.x, ta.y + 8.f), ImVec2(ta.x + 3.f, tb.y - 8.f),
                SetA(g_toast.color, (int)(255 * a)), 2.f);
            bg->AddCircleFilled(ImVec2(ta.x + 22.f, ta.y + 20.f), 5.f,
                SetA(g_toast.color, (int)(255 * a)), 16);
            Txt(bg, g_fSemi, 13.f, ImVec2(ta.x + 38.f, ta.y + 11.f),
                SetA(C::text, (int)(255 * a)), g_toast.msg.c_str());
        }

        ImGui::End();

        if (RemoteConfig::mustUpdate()) {
            RemoteStatus rs = RemoteConfig::snapshot();
            ImGuiIO& io2 = ImGui::GetIO();

            ImGui::SetNextWindowPos(ImVec2(0.f, 0.f));
            ImGui::SetNextWindowSize(io2.DisplaySize);
            ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(5, 5, 10, 252));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.f, 0.f));

            ImGui::Begin("##blocked", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoSavedSettings);

            ImGui::Dummy(ImVec2(0.f, io2.DisplaySize.y * 0.28f));
            ImGui::Indent(70.f);

            ImGui::PushFont(g_fBold, 28.f);
            ImGui::PushStyleColor(ImGuiCol_Text, C::warn);
            ImGui::TextUnformatted(rs.enabled ? "UPDATE REQUIRED" : "THIS VERSION IS DISABLED");
            ImGui::PopStyleColor();
            ImGui::PopFont();

            ImGui::Dummy(ImVec2(0.f, 14.f));

            if (!rs.message.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, C::text2);
                ImGui::PushTextWrapPos(io2.DisplaySize.x - 140.f);
                ImGui::TextUnformatted(rs.message.c_str());
                ImGui::PopTextWrapPos();
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0.f, 12.f));
            }

            ImGui::PushStyleColor(ImGuiCol_Text, C::dim);
            ImGui::Text("Your version: %s", APP_VERSION);
            if (!rs.latest.empty()) ImGui::Text("Latest version: %s", rs.latest.c_str());
            ImGui::PopStyleColor();

            ImGui::Dummy(ImVec2(0.f, 22.f));

            if (ImGui::Button("   OPEN DOWNLOAD PAGE   ", ImVec2(320.f, 46.f))) {
                std::string u = rs.downloadUrl.empty() ? "https://cppcore.ir" : rs.downloadUrl;
                std::wstring wu(u.begin(), u.end());
                ShellExecuteW(nullptr, L"open", wu.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }

            ImGui::Unindent(70.f);
            ImGui::End();
            ImGui::PopStyleVar();
            ImGui::PopStyleColor();
        }
        else if (RemoteConfig::updateAvailable()) {
            RemoteStatus rs = RemoteConfig::snapshot();
            ImGuiIO& io2 = ImGui::GetIO();

            ImGui::SetNextWindowPos(ImVec2(io2.DisplaySize.x - 330.f, 18.f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(312.f, 0.f));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, SetA(C::panel2, 245));
            ImGui::Begin("##updavail", nullptr,
                ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_AlwaysAutoResize);

            ImGui::PushStyleColor(ImGuiCol_Text, C::good);
            ImGui::Text("Update available: %s", rs.latest.c_str());
            ImGui::PopStyleColor();

            if (ImGui::Button("GET IT", ImVec2(120.f, 30.f))) {
                std::string u = rs.downloadUrl.empty() ? "https://cppcore.ir" : rs.downloadUrl;
                std::wstring wu(u.begin(), u.end());
                ShellExecuteW(nullptr, L"open", wu.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            }
            ImGui::End();
            ImGui::PopStyleColor();
        }

        ImGui::Render();
        const float clear[4] = { 0.027f, 0.027f, 0.043f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0);

        if (!shown) {
            ::ShowWindow(hwnd, SW_SHOWDEFAULT);
            ::UpdateWindow(hwnd);
            shown = true;
        }

        IconManager::pump(4);
    }

    RemoteConfig::stop();
    IconManager::shutdown();
    for (int i = 0; i < BIND_SLOTS; ++i) ::UnregisterHotKey(hwnd, BIND_ID_BASE + i);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
    D3D_FEATURE_LEVEL level;
    const D3D_FEATURE_LEVEL levels[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT res = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
        levels, 2, D3D11_SDK_VERSION, &sd,
        &g_pSwapChain, &g_pd3dDevice, &level, &g_pd3dDeviceContext);

    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(
            nullptr, D3D_DRIVER_TYPE_WARP, nullptr, flags,
            levels, 2, D3D11_SDK_VERSION, &sd,
            &g_pSwapChain, &g_pd3dDevice, &level, &g_pd3dDeviceContext);

    if (res != S_OK) return false;
    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBack = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBack));
    g_pd3dDevice->CreateRenderTargetView(pBack, nullptr, &g_mainRenderTargetView);
    pBack->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_HOTKEY:
        g_hotkeySlot.store((int)wParam - BIND_ID_BASE);
        return 0;

    case WM_NCHITTEST:
    {
        POINT pt = { GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam) };
        ScreenToClient(hWnd, &pt);
        RECT rc; GetClientRect(hWnd, &rc);

        if (pt.y < (int)(58.f * g_uiScale) &&
            pt.x < rc.right - (int)(300.f * g_uiScale)) return HTCAPTION;

        return HTCLIENT;
    }
    case WM_NCCALCSIZE:
        if (wParam == TRUE) return 0;
        break;
    case WM_SIZE:
        if (g_pd3dDevice && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam),
                DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
