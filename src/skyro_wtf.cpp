#ifndef UNICODE
#define UNICODE
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <gdiplus.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dwmapi.lib")
#endif

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace {

constexpr int kWindowWidth = 1000;
constexpr int kWindowHeight = 640;
constexpr int kTopBarHeight = 64;
constexpr int kRailWidth = 190;
constexpr int kTabCount = 5;

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
ID3D11ShaderResourceView* g_background = nullptr;
ULONG_PTR g_gdiplusToken = 0;
HWND g_window = nullptr;
UINT g_resizeWidth = 0;
UINT g_resizeHeight = 0;
int g_windowWidth = kWindowWidth;
int g_windowHeight = kWindowHeight;
int g_backgroundWidth = 1;
int g_backgroundHeight = 1;
std::string g_backgroundPath;

ImFont* g_font = nullptr;
ImFont* g_fontBold = nullptr;
ImFont* g_fontSmall = nullptr;

struct TabInfo {
    const char* title;
    const char* description;
};

constexpr std::array<TabInfo, kTabCount> kTabs{{
    {"01  Overview", "status + actions"},
    {"02  Deep Link", "target preview"},
    {"03  Appearance", "accent + glass"},
    {"04  Assets", "background file"},
    {"05  About", "build details"},
}};

int g_activeTab = 0;
std::array<float, kTabCount> g_tabGlow{};
int g_accentIndex = 0;
float g_glassAlpha = 0.32f;
float g_cornerRadius = 14.0f;
bool g_blurEnabled = true;
bool g_coverFit = true;

char g_placeBuffer[64] = "109983668079237";
char g_portBuffer[16] = "6969";
char g_retriesBuffer[16] = "10";
char g_delayBuffer[16] = "1500";
char g_jobBuffer[64] = "00000000-0000-0000-0000-000000000000";
char g_uriBuffer[512] = "";

std::string g_toast;
double g_toastUntil = 0.0;

std::string WideToUtf8(const std::wstring& value) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string result(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), &result[0], needed, nullptr, nullptr);
    return result;
}

std::wstring ModuleDirectory() {
    wchar_t path[MAX_PATH] = {};
    DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) return L"";
    std::wstring full(path, length);
    size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return L"";
    return full.substr(0, slash + 1);
}

ImVec4 AccentColor() {
    switch (g_accentIndex) {
        case 1: return ImVec4(1.00f, 0.35f, 0.70f, 1.0f);
        case 2: return ImVec4(0.25f, 0.76f, 1.00f, 1.0f);
        case 3: return ImVec4(0.92f, 0.92f, 1.00f, 1.0f);
        default: return ImVec4(0.69f, 0.31f, 1.00f, 1.0f);
    }
}

ImU32 AccentU32(float alpha) {
    ImVec4 accent = AccentColor();
    return ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha));
}

void SetToast(const char* message) {
    g_toast = message ? message : "";
    g_toastUntil = ImGui::GetTime() + 2.6;
}

void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowRounding = g_cornerRadius;
    style.ChildRounding = g_cornerRadius;
    style.FrameRounding = std::max(5.0f, g_cornerRadius - 6.0f);
    style.PopupRounding = 10.0f;
    style.GrabRounding = 7.0f;
    style.ScrollbarRounding = 7.0f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(9.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 4.0f);
    style.ScrollbarSize = 9.0f;
    style.Alpha = 1.0f;

    ImVec4 accent = AccentColor();
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.035f, 0.018f, 0.070f, g_glassAlpha);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.045f, 0.022f, 0.085f, 0.94f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.07f, 0.19f, 0.62f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(accent.x * 0.42f, accent.y * 0.42f, accent.z * 0.42f, 0.70f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.78f);
    style.Colors[ImGuiCol_Button] = ImVec4(accent.x * 0.52f, accent.y * 0.52f, accent.z * 0.52f, 0.72f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x * 0.78f, accent.y * 0.78f, accent.z * 0.78f, 0.88f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(accent.x, accent.y, accent.z, 0.20f);
    style.Colors[ImGuiCol_Separator] = ImVec4(accent.x, accent.y, accent.z, 0.28f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(accent.x, accent.y, accent.z, 0.92f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.97f, 0.96f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.66f, 0.60f, 0.76f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(accent.x, accent.y, accent.z, 0.55f);
}

void LoadFonts() {
    wchar_t windowsDirectory[MAX_PATH] = {};
    GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
    std::wstring base(windowsDirectory);
    base += L"\\Fonts\\";

    ImGuiIO& io = ImGui::GetIO();
    g_font = io.Fonts->AddFontFromFileTTF(WideToUtf8(base + L"segoeui.ttf").c_str(), 15.0f);
    g_fontBold = io.Fonts->AddFontFromFileTTF(WideToUtf8(base + L"seguisb.ttf").c_str(), 19.0f);
    g_fontSmall = io.Fonts->AddFontFromFileTTF(WideToUtf8(base + L"segoeui.ttf").c_str(), 12.0f);
    if (!g_font) g_font = io.Fonts->AddFontDefault();
    if (!g_fontBold) g_fontBold = g_font;
    if (!g_fontSmall) g_fontSmall = g_font;
    io.Fonts->Build();
}

void LoadBackgroundTexture() {
    if (!g_device) return;

    std::wstring directory = ModuleDirectory();
    std::wstring path = directory.empty() ? L"assets\\login.jpg" : directory + L"assets\\login.jpg";
    Gdiplus::Image source(path.c_str());
    if (source.GetLastStatus() != Gdiplus::Ok) {
        g_backgroundPath = WideToUtf8(path);
        g_backgroundWidth = 1;
        g_backgroundHeight = 1;
        return;
    }

    int width = static_cast<int>(source.GetWidth());
    int height = static_cast<int>(source.GetHeight());
    if (width <= 0 || height <= 0) return;

    Gdiplus::Bitmap bitmap(width, height, PixelFormat32bppARGB);
    Gdiplus::Graphics graphics(&bitmap);
    graphics.DrawImage(&source, 0, 0, width, height);

    Gdiplus::BitmapData data = {};
    Gdiplus::Rect bounds(0, 0, width, height);
    if (bitmap.LockBits(&bounds, Gdiplus::ImageLockModeRead, PixelFormat32bppARGB, &data) != Gdiplus::Ok) return;

    D3D11_TEXTURE2D_DESC textureDesc = {};
    textureDesc.Width = static_cast<UINT>(width);
    textureDesc.Height = static_cast<UINT>(height);
    textureDesc.MipLevels = 1;
    textureDesc.ArraySize = 1;
    textureDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    textureDesc.SampleDesc.Count = 1;
    textureDesc.Usage = D3D11_USAGE_DEFAULT;
    textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA upload = {};
    upload.pSysMem = data.Scan0;
    upload.SysMemPitch = static_cast<UINT>(data.Stride);

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if (SUCCEEDED(g_device->CreateTexture2D(&textureDesc, &upload, &texture))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.Format = textureDesc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;
        if (SUCCEEDED(g_device->CreateShaderResourceView(texture, &viewDesc, &view))) {
            if (g_background) g_background->Release();
            g_background = view;
            g_backgroundWidth = width;
            g_backgroundHeight = height;
            g_backgroundPath = WideToUtf8(path);
        } else if (view) {
            view->Release();
        }
        texture->Release();
    }
    bitmap.UnlockBits(&data);
}

void CreateRenderTarget();

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0};
    HRESULT result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                    levels, 2, D3D11_SDK_VERSION, &desc,
                                                    &g_swapChain, &g_device, &level, &g_context);
    if (result == DXGI_ERROR_UNSUPPORTED) {
        result = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                                levels, 2, D3D11_SDK_VERSION, &desc,
                                                &g_swapChain, &g_device, &level, &g_context);
    }
    if (FAILED(result)) return false;

    CreateRenderTarget();
    return true;
}

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_renderTarget) {
        g_renderTarget->Release();
        g_renderTarget = nullptr;
    }
}

void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_background) {
        g_background->Release();
        g_background = nullptr;
    }
    if (g_swapChain) {
        g_swapChain->Release();
        g_swapChain = nullptr;
    }
    if (g_context) {
        g_context->Release();
        g_context = nullptr;
    }
    if (g_device) {
        g_device->Release();
        g_device = nullptr;
    }
}

void UpdateWindowRegion() {
    if (!g_window) return;
    HRGN region = CreateRoundRectRgn(0, 0, g_windowWidth + 1, g_windowHeight + 1, 24, 24);
    if (region) SetWindowRgn(g_window, region, TRUE);
}

void ApplyWindowEffects() {
    SetWindowLongPtrW(g_window, GWL_EXSTYLE, GetWindowLongPtrW(g_window, GWL_EXSTYLE) | WS_EX_LAYERED);
    SetLayeredWindowAttributes(g_window, 0, 255, LWA_ALPHA);

    MARGINS margins = {-1, -1, -1, -1};
    DwmExtendFrameIntoClientArea(g_window, &margins);

    int cornerPreference = 2;
    DwmSetWindowAttribute(g_window, DWMWA_WINDOW_CORNER_PREFERENCE, &cornerPreference, sizeof(cornerPreference));
    UpdateWindowRegion();
}

void RenderBlur() {
    struct AccentPolicy {
        int na;
        int nf;
        int nc;
        int nA;
    };
    struct WindowCompositionAttributeData {
        int attribute;
        PVOID data;
        ULONG size;
    };

    HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (!user32) return;
    using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(HWND, WindowCompositionAttributeData*);
    FARPROC address = GetProcAddress(user32, "SetWindowCompositionAttribute");
    SetWindowCompositionAttributeFn setAttribute = nullptr;
    if (address) std::memcpy(&setAttribute, &address, sizeof(setAttribute));
    if (setAttribute) {
        AccentPolicy policy = {g_blurEnabled ? 3 : 0, 0, 0, 0};
        WindowCompositionAttributeData data = {19, &policy, sizeof(policy)};
        setAttribute(g_window, &data);
    }
    FreeLibrary(user32);
}

void CalculateCoverUv(ImVec2 area, ImVec2& uv0, ImVec2& uv1) {
    if (area.x <= 0.0f || area.y <= 0.0f || g_backgroundWidth <= 0 || g_backgroundHeight <= 0) {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
        return;
    }

    if (!g_coverFit) {
        uv0 = ImVec2(0.0f, 0.0f);
        uv1 = ImVec2(1.0f, 1.0f);
        return;
    }

    const float viewAspect = area.x / area.y;
    const float imageAspect = static_cast<float>(g_backgroundWidth) / static_cast<float>(g_backgroundHeight);
    uv0 = ImVec2(0.0f, 0.0f);
    uv1 = ImVec2(1.0f, 1.0f);

    if (imageAspect > viewAspect) {
        const float visible = viewAspect / imageAspect;
        uv0.x = 0.5f - visible * 0.5f;
        uv1.x = 0.5f + visible * 0.5f;
    } else {
        const float visible = imageAspect / viewAspect;
        uv0.y = 0.5f - visible * 0.5f;
        uv1.y = 0.5f + visible * 0.5f;
    }
}

void DrawBackdrop() {
    ImDrawList* drawList = ImGui::GetBackgroundDrawList();
    ImVec2 display = ImGui::GetIO().DisplaySize;

    if (g_background) {
        ImVec2 uv0;
        ImVec2 uv1;
        CalculateCoverUv(display, uv0, uv1);
        drawList->AddImage(g_background, ImVec2(0.0f, 0.0f), display, uv0, uv1, IM_COL32(255, 255, 255, 255));
        drawList->AddRectFilled(ImVec2(0.0f, 0.0f), display, IM_COL32(18, 5, 34, 30));
    } else {
        drawList->AddRectFilledMultiColor(ImVec2(0.0f, 0.0f), display,
                                          IM_COL32(18, 7, 34, 255), IM_COL32(52, 15, 84, 255),
                                          IM_COL32(28, 10, 52, 255), IM_COL32(8, 3, 18, 255));
    }

    drawList->AddRectFilledMultiColor(ImVec2(display.x * 0.35f, 0.0f), display,
                                      IM_COL32(0, 0, 0, 0), AccentU32(0.12f),
                                      IM_COL32(0, 0, 0, 0), AccentU32(0.05f));
}

void BeginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, g_cornerRadius);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool PrimaryButton(const char* label, ImVec2 size) {
    ImVec4 accent = AccentColor();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.82f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

bool GhostButton(const char* label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.06f, 0.16f, 0.72f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.10f, 0.27f, 0.86f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.26f, 0.14f, 0.38f, 0.96f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

void DrawTopBar() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = ImGui::GetWindowWidth();

    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, kTopBarHeight), IM_COL32(12, 5, 24, 132));
    drawList->AddLine(ImVec2(0.0f, kTopBarHeight - 1.0f), ImVec2(width, kTopBarHeight - 1.0f), AccentU32(0.28f), 1.0f);

    ImVec2 logoMin(20.0f, 15.0f);
    ImVec2 logoMax = logoMin + ImVec2(34.0f, 34.0f);
    drawList->AddRectFilled(logoMin, logoMax, AccentU32(0.95f), 10.0f);
    drawList->AddText(g_fontBold, 22.0f, logoMin + ImVec2(10.0f, 4.0f), IM_COL32(255, 255, 255, 255), "S");

    drawList->AddText(g_fontBold, 22.0f, ImVec2(66.0f, 13.0f), IM_COL32(250, 248, 255, 255), "skyro.wtf");
    drawList->AddText(g_fontSmall, 12.0f, ImVec2(67.0f, 39.0f), IM_COL32(180, 166, 202, 255), "GUI SHELL  /  VIOLET EDITION");

    ImVec2 minimizePos(width - 92.0f, 18.0f);
    ImGui::SetCursorScreenPos(minimizePos);
    if (ImGui::InvisibleButton("##minimize", ImVec2(30.0f, 28.0f))) ShowWindow(g_window, SW_MINIMIZE);
    bool minimizeHovered = ImGui::IsItemHovered();
    drawList->AddRectFilled(minimizePos, minimizePos + ImVec2(30.0f, 28.0f),
                            minimizeHovered ? AccentU32(0.30f) : IM_COL32(255, 255, 255, 12), 7.0f);
    drawList->AddText(g_fontBold, 17.0f, minimizePos + ImVec2(10.0f, 2.0f), IM_COL32(230, 222, 245, 255), "-");

    ImVec2 closePos(width - 52.0f, 18.0f);
    ImGui::SetCursorScreenPos(closePos);
    if (ImGui::InvisibleButton("##close", ImVec2(30.0f, 28.0f))) PostMessageW(g_window, WM_CLOSE, 0, 0);
    bool closeHovered = ImGui::IsItemHovered();
    drawList->AddRectFilled(closePos, closePos + ImVec2(30.0f, 28.0f),
                            closeHovered ? IM_COL32(236, 68, 110, 155) : IM_COL32(255, 255, 255, 12), 7.0f);
    drawList->AddText(g_fontBold, 16.0f, closePos + ImVec2(9.0f, 3.0f), IM_COL32(255, 245, 250, 255), "x");
}

void DrawRail() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float height = ImGui::GetWindowHeight();

    drawList->AddRectFilled(ImVec2(0.0f, kTopBarHeight), ImVec2(kRailWidth, height), IM_COL32(9, 4, 19, 154));
    drawList->AddLine(ImVec2(kRailWidth - 1.0f, kTopBarHeight), ImVec2(kRailWidth - 1.0f, height), AccentU32(0.20f), 1.0f);
    drawList->AddText(g_fontSmall, 12.0f, ImVec2(18.0f, kTopBarHeight + 18.0f), AccentU32(0.95f), "NAVIGATION");

    const float animationStep = std::clamp(ImGui::GetIO().DeltaTime * 9.0f, 0.0f, 1.0f);
    for (int index = 0; index < kTabCount; ++index) {
        ImVec2 start(18.0f, static_cast<float>(kTopBarHeight + 48 + index * 66));
        ImVec2 size(kRailWidth - 36.0f, 56.0f);
        ImGui::SetCursorScreenPos(start);
        ImGui::PushID(index);
        if (ImGui::InvisibleButton("tab", size)) g_activeTab = index;
        bool hovered = ImGui::IsItemHovered();
        ImGui::PopID();

        const bool active = g_activeTab == index;
        const float target = active ? 1.0f : (hovered ? 0.55f : 0.0f);
        g_tabGlow[index] += (target - g_tabGlow[index]) * animationStep;

        ImVec2 end = start + size;
        ImU32 fill = ImGui::ColorConvertFloat4ToU32(ImVec4(0.08f, 0.04f, 0.15f, 0.22f + g_tabGlow[index] * 0.48f));
        drawList->AddRectFilled(start, end, fill, 10.0f);
        drawList->AddRect(start, end, AccentU32(0.08f + g_tabGlow[index] * 0.40f), 10.0f, 0, 1.0f);
        drawList->AddRectFilled(start + ImVec2(0.0f, 8.0f), start + ImVec2(3.0f, size.y - 8.0f),
                                AccentU32(0.25f + g_tabGlow[index] * 0.75f), 2.0f);

        ImU32 titleColor = active ? IM_COL32(255, 255, 255, 255) : IM_COL32(202, 191, 221, 255);
        drawList->AddText(g_fontBold, 15.0f, start + ImVec2(14.0f, 8.0f), titleColor, kTabs[index].title);
        drawList->AddText(g_fontSmall, 11.0f, start + ImVec2(15.0f, 32.0f), IM_COL32(164, 150, 190, 255), kTabs[index].description);
    }

    drawList->AddRectFilled(ImVec2(18.0f, height - 68.0f), ImVec2(kRailWidth - 18.0f, height - 18.0f), AccentU32(0.12f), 10.0f);
    drawList->AddText(g_fontSmall, 11.0f, ImVec2(30.0f, height - 57.0f), IM_COL32(205, 192, 225, 255), "STATUS");
    drawList->AddCircleFilled(ImVec2(35.0f, height - 33.0f), 4.0f, AccentU32(0.95f), 12);
    drawList->AddText(g_fontSmall, 12.0f, ImVec2(46.0f, height - 40.0f), IM_COL32(240, 235, 250, 255), "interface ready");
}

void DrawPageHeader(const char* kicker, const char* title, const char* subtitle) {
    ImGui::TextColored(AccentColor(), "%s", kicker);
    ImGui::PushFont(g_fontBold);
    ImGui::Text("%s", title);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", subtitle);
    ImGui::Spacing();
}

void DrawImageBox(ImVec2 size) {
    if (g_background) {
        ImVec2 uv0;
        ImVec2 uv1;
        CalculateCoverUv(size, uv0, uv1);
        ImGui::Image(g_background, size, uv0, uv1);
    } else {
        ImVec2 start = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(start, start + size, IM_COL32(28, 12, 48, 210), g_cornerRadius);
        ImGui::Dummy(size);
    }
}

void DrawStatCard(const char* label, const char* value, const char* detail, ImVec2 size) {
    BeginCard(label, size);
    ImGui::TextColored(ImVec4(0.68f, 0.61f, 0.80f, 1.0f), "%s", label);
    ImGui::PushFont(g_fontBold);
    ImGui::Text("%s", value);
    ImGui::PopFont();
    ImGui::TextDisabled("%s", detail);
    EndCard();
}

void UpdateUriBuffer() {
    std::snprintf(g_uriBuffer, sizeof(g_uriBuffer),
                  "roblox://experiences/start?placeId=%s&gameInstanceId=%s",
                  g_placeBuffer, g_jobBuffer);
}

void RenderOverview() {
    float width = ImGui::GetContentRegionAvail().x;
    DrawPageHeader("DASHBOARD", "Overview", "One clean view. No clutter, no hidden panels.");

    BeginCard("hero_card", ImVec2(-1.0f, 214.0f));
    DrawImageBox(ImVec2(width - 28.0f, 174.0f));
    ImVec2 heroStart = ImGui::GetItemRectMin();
    ImVec2 heroEnd = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        heroStart, heroEnd, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
        IM_COL32(12, 3, 24, 190), IM_COL32(12, 3, 24, 210));
    ImGui::SetCursorScreenPos(heroStart + ImVec2(18.0f, 112.0f));
    ImGui::PushFont(g_fontBold);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "skyro.wtf");
    ImGui::PopFont();
    ImGui::TextColored(ImVec4(0.88f, 0.82f, 0.96f, 1.0f), "Violet glass over the login artwork. Background stays visible.");
    EndCard();

    ImGui::Spacing();
    const float statWidth = (width - 28.0f - 3.0f * 10.0f) / 4.0f;
    DrawStatCard("SESSION", "READY", "local interface", ImVec2(statWidth, 86.0f));
    ImGui::SameLine(0.0f, 10.0f);
    DrawStatCard("RENDER", "DX11", "imgui backends", ImVec2(statWidth, 86.0f));
    ImGui::SameLine(0.0f, 10.0f);
    DrawStatCard("ARTWORK", "LOADED", "assets/login.jpg", ImVec2(statWidth, 86.0f));
    ImGui::SameLine(0.0f, 10.0f);
    DrawStatCard("BUILD", "GUI", "ui shell only", ImVec2(statWidth, 86.0f));

    ImGui::Spacing();
    const float actionWidth = (width - 28.0f - 2.0f * 10.0f) / 3.0f;
    if (PrimaryButton("DEEP LINK PREVIEW", ImVec2(actionWidth, 38.0f))) {
        g_activeTab = 1;
        SetToast("deep link tab opened");
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (GhostButton("COPY PLACE ID", ImVec2(actionWidth, 38.0f))) {
        ImGui::SetClipboardText(g_placeBuffer);
        SetToast("place id copied");
    }
    ImGui::SameLine(0.0f, 10.0f);
    if (GhostButton("RELOAD ARTWORK", ImVec2(actionWidth, 38.0f))) {
        LoadBackgroundTexture();
        SetToast(g_background ? "artwork reloaded" : "artwork missing");
    }

    ImGui::Spacing();
    BeginCard("activity_card", ImVec2(-1.0f, 88.0f));
    ImGui::TextColored(AccentColor(), "SURFACE");
    ImGui::TextWrapped("Rounded borderless frame, live blur, left tab rail, translucent cards, and a real image backdrop. Every section stays one click away.");
    EndCard();
}

void RenderDeepLink() {
    float width = ImGui::GetContentRegionAvail().x;
    DrawPageHeader("TARGET", "Deep Link", "Clean preview of the join URI. Fields are interface state only.");

    const float leftWidth = (width - 28.0f - 12.0f) * 0.46f;
    const float rightWidth = width - 28.0f - 12.0f - leftWidth;

    BeginCard("target_card", ImVec2(leftWidth, 300.0f));
    ImGui::TextColored(AccentColor(), "PARAMETERS");
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Place ID", g_placeBuffer, IM_ARRAYSIZE(g_placeBuffer));
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Port", g_portBuffer, IM_ARRAYSIZE(g_portBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Retries", g_retriesBuffer, IM_ARRAYSIZE(g_retriesBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputText("Delay ms", g_delayBuffer, IM_ARRAYSIZE(g_delayBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::Spacing();
    ImGui::TextDisabled("Editable preview values. Nothing connects from this build.");
    EndCard();

    ImGui::SameLine(0.0f, 12.0f);

    UpdateUriBuffer();
    BeginCard("preview_card", ImVec2(rightWidth, 300.0f));
    ImGui::TextColored(AccentColor(), "URI PREVIEW");
    ImGui::PushFont(g_fontSmall);
    ImGui::InputTextMultiline("##uri", g_uriBuffer, IM_ARRAYSIZE(g_uriBuffer),
                              ImVec2(-1.0f, 128.0f), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();
    ImGui::Spacing();
    if (PrimaryButton("COPY URI", ImVec2(-1.0f, 38.0f))) {
        ImGui::SetClipboardText(g_uriBuffer);
        SetToast("uri copied");
    }
    ImGui::Spacing();
    ImGui::TextWrapped("The preview rebuilds from the fields on the left as you type.");
    EndCard();

    ImGui::Spacing();
    BeginCard("flow_card", ImVec2(-1.0f, 116.0f));
    ImGui::TextColored(AccentColor(), "LAYOUT FLOW");
    ImGui::Text("01  target values     02  preview string     03  clipboard");
    ImGui::TextDisabled("Cards use low alpha so the artwork stays readable behind the interface.");
    EndCard();
}

void DrawSwatch(int index, ImVec2 position) {
    ImVec2 size(36.0f, 36.0f);
    ImGui::SetCursorScreenPos(position);
    ImGui::PushID(index);
    if (ImGui::InvisibleButton("swatch", size)) {
        g_accentIndex = index;
        SetToast("accent changed");
    }
    bool hovered = ImGui::IsItemHovered();
    ImGui::PopID();

    ImVec4 color;
    switch (index) {
        case 1: color = ImVec4(1.00f, 0.35f, 0.70f, 1.0f); break;
        case 2: color = ImVec4(0.25f, 0.76f, 1.00f, 1.0f); break;
        case 3: color = ImVec4(0.92f, 0.92f, 1.00f, 1.0f); break;
        default: color = ImVec4(0.69f, 0.31f, 1.00f, 1.0f); break;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 center = position + ImVec2(18.0f, 18.0f);
    drawList->AddCircleFilled(center, hovered ? 16.0f : 14.0f, ImGui::ColorConvertFloat4ToU32(color), 24);
    if (g_accentIndex == index) drawList->AddCircle(center, 19.0f, IM_COL32(255, 255, 255, 210), 24, 2.0f);
}

void RenderAppearance() {
    float width = ImGui::GetContentRegionAvail().x;
    DrawPageHeader("THEME", "Appearance", "Accent, glass opacity, corners. Changes apply instantly.");

    const float leftWidth = (width - 28.0f - 12.0f) * 0.44f;
    const float rightWidth = width - 28.0f - 12.0f - leftWidth;

    BeginCard("accent_card", ImVec2(leftWidth, 300.0f));
    ImGui::TextColored(AccentColor(), "ACCENT");
    ImVec2 cardStart = ImGui::GetCursorScreenPos();
    for (int index = 0; index < 4; ++index) {
        DrawSwatch(index, cardStart + ImVec2(static_cast<float>(index % 2) * 56.0f,
                                              static_cast<float>(index / 2) * 56.0f));
    }
    ImGui::SetCursorScreenPos(cardStart + ImVec2(0.0f, 124.0f));
    ImGui::TextDisabled("Violet, rose, cyan, white.");
    ImGui::Spacing();
    ImGui::TextWrapped("The same accent drives tabs, borders, sliders, buttons, and the top bar wash.");
    EndCard();

    ImGui::SameLine(0.0f, 12.0f);

    BeginCard("glass_card", ImVec2(rightWidth, 300.0f));
    ImGui::TextColored(AccentColor(), "GLASS");
    ImGui::SliderFloat("Panel opacity", &g_glassAlpha, 0.08f, 0.70f, "%.2f");
    ImGui::SliderFloat("Corner radius", &g_cornerRadius, 6.0f, 24.0f, "%.0f px");
    if (ImGui::Checkbox("Blur behind window", &g_blurEnabled)) {
        RenderBlur();
        SetToast(g_blurEnabled ? "blur on" : "blur off");
    }
    if (ImGui::Checkbox("Cover image crop", &g_coverFit)) SetToast(g_coverFit ? "cover crop" : "stretched image");
    ImGui::Spacing();
    ImGui::TextDisabled("Lower opacity shows more of login.jpg.");
    EndCard();

    ImGui::Spacing();
    BeginCard("type_card", ImVec2(-1.0f, 106.0f));
    ImGui::TextColored(AccentColor(), "TYPE");
    ImGui::PushFont(g_fontBold);
    ImGui::Text("Clean sections. Smooth widgets. Short paths.");
    ImGui::PopFont();
    ImGui::TextDisabled("Segoe UI at 15 px for body, 19 px semibold for headers, 12 px for meta.");
    EndCard();
}

void RenderAssets() {
    float width = ImGui::GetContentRegionAvail().x;
    DrawPageHeader("FILES", "Assets", "Background texture loaded from the executable folder.");

    const float previewWidth = (width - 28.0f - 12.0f) * 0.63f;
    const float detailWidth = width - 28.0f - 12.0f - previewWidth;

    BeginCard("asset_preview_card", ImVec2(previewWidth, 320.0f));
    ImVec2 imageSize(previewWidth - 28.0f, 248.0f);
    DrawImageBox(imageSize);
    ImVec2 imageStart = ImGui::GetItemRectMin();
    ImVec2 imageEnd = ImGui::GetItemRectMax();
    ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
        imageStart, imageEnd, IM_COL32(0, 0, 0, 0), IM_COL32(0, 0, 0, 0),
        IM_COL32(10, 3, 20, 190), IM_COL32(10, 3, 20, 210));
    ImGui::SetCursorScreenPos(imageStart + ImVec2(16.0f, imageSize.y - 36.0f));
    ImGui::PushFont(g_fontBold);
    ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "login.jpg");
    ImGui::PopFont();
    EndCard();

    ImGui::SameLine(0.0f, 12.0f);

    BeginCard("asset_detail_card", ImVec2(detailWidth, 320.0f));
    ImGui::TextColored(AccentColor(), "TEXTURE");
    ImGui::Text("source");
    ImGui::TextWrapped("%s", g_backgroundPath.empty() ? "assets\\login.jpg" : g_backgroundPath.c_str());
    ImGui::Spacing();
    ImGui::Text("width");
    ImGui::SameLine(100.0f);
    ImGui::Text("%d px", g_backgroundWidth);
    ImGui::Text("height");
    ImGui::SameLine(100.0f);
    ImGui::Text("%d px", g_backgroundHeight);
    ImGui::Text("shader view");
    ImGui::SameLine(100.0f);
    ImGui::TextColored(g_background ? ImVec4(0.55f, 1.0f, 0.72f, 1.0f) : ImVec4(1.0f, 0.48f, 0.58f, 1.0f),
                       "%s", g_background ? "active" : "missing");
    ImGui::Spacing();
    if (PrimaryButton("RELOAD IMAGE", ImVec2(-1.0f, 36.0f))) {
        LoadBackgroundTexture();
        SetToast(g_background ? "image reloaded" : "image missing");
    }
    if (GhostButton("COPY PATH", ImVec2(-1.0f, 36.0f))) {
        ImGui::SetClipboardText(g_backgroundPath.c_str());
        SetToast("asset path copied");
    }
    EndCard();

    ImGui::Spacing();
    BeginCard("pipeline_card", ImVec2(-1.0f, 96.0f));
    ImGui::TextColored(AccentColor(), "PIPELINE");
    ImGui::Text("JPEG file  ->  GDI+ bitmap  ->  DX11 shader resource  ->  ImGui draw list");
    ImGui::TextDisabled("Keep the assets folder beside the executable when moving the build.");
    EndCard();
}

void RenderAbout() {
    float width = ImGui::GetContentRegionAvail().x;
    DrawPageHeader("INFO", "About", "A GUI-only shell built around a clean ImGui loader layout.");

    const float cardWidth = (width - 28.0f - 12.0f) / 2.0f;
    DrawStatCard("RENDERER", "ImGui 1.90", "Win32 + DX11", ImVec2(cardWidth, 112.0f));
    ImGui::SameLine(0.0f, 12.0f);
    DrawStatCard("WINDOW", "Borderless", "rounded + layered", ImVec2(cardWidth, 112.0f));
    ImGui::Spacing();
    DrawStatCard("RUNTIME", "Static", "no extra DLLs", ImVec2(cardWidth, 112.0f));
    ImGui::SameLine(0.0f, 12.0f);
    DrawStatCard("LOGIC", "UI ONLY", "no listener, no script", ImVec2(cardWidth, 112.0f));

    ImGui::Spacing();
    BeginCard("about_card", ImVec2(-1.0f, 148.0f));
    ImGui::TextColored(AccentColor(), "BUILD");
    ImGui::TextWrapped("Three-eye quality control. Purple glass, white type, compact cards, and a background you can actually see. Built for smooth mouse input and one-click access.");
    if (PrimaryButton("COPY BUILD TAG", ImVec2(190.0f, 34.0f))) {
        ImGui::SetClipboardText("skyro.wtf / gui shell / 2026.11");
        SetToast("build tag copied");
    }
    EndCard();
}

void DrawContent() {
    const ImVec2 contentStart(kRailWidth + 22.0f, kTopBarHeight + 18.0f);
    const ImVec2 contentSize(g_windowWidth - kRailWidth - 44.0f,
                             g_windowHeight - kTopBarHeight - 36.0f);

    ImGui::SetCursorScreenPos(contentStart);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("content", contentSize, 0,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);

    switch (g_activeTab) {
        case 0: RenderOverview(); break;
        case 1: RenderDeepLink(); break;
        case 2: RenderAppearance(); break;
        case 3: RenderAssets(); break;
        default: RenderAbout(); break;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
}

void DrawToast() {
    if (g_toast.empty() || ImGui::GetTime() > g_toastUntil) return;

    const double remaining = g_toastUntil - ImGui::GetTime();
    const float alpha = static_cast<float>(std::clamp(remaining / 0.35, 0.0, 1.0));
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    ImVec2 min(g_windowWidth - 350.0f, kTopBarHeight + 14.0f);
    ImVec2 max(g_windowWidth - 18.0f, kTopBarHeight + 58.0f);
    drawList->AddRectFilled(min, max, ImGui::ColorConvertFloat4ToU32(ImVec4(0.05f, 0.02f, 0.10f, 0.90f * alpha)), 10.0f);
    drawList->AddRect(min, max, AccentU32(0.55f * alpha), 10.0f, 0, 1.0f);
    drawList->AddText(g_fontSmall, 13.0f, min + ImVec2(14.0f, 13.0f),
                      ImGui::ColorConvertFloat4ToU32(ImVec4(0.96f, 0.94f, 1.0f, alpha)),
                      g_toast.c_str());
}

void DrawInterface() {
    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(static_cast<float>(g_windowWidth), static_cast<float>(g_windowHeight)));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::Begin("##skyro-shell", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();

    DrawTopBar();
    DrawRail();
    DrawContent();
    DrawToast();

    ImGui::End();
}

}  // namespace

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) return 1;

    switch (message) {
        case WM_NCHITTEST: {
            POINT point{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                        static_cast<int>(static_cast<short>(HIWORD(lparam)))};
            ScreenToClient(hwnd, &point);
            if (point.y >= 0 && point.y < kTopBarHeight && point.x >= 0 && point.x < g_windowWidth - 104)
                return HTCAPTION;
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
        case WM_SIZE:
            if (wparam == SIZE_MINIMIZED) return 0;
            g_resizeWidth = LOWORD(lparam);
            g_resizeHeight = HIWORD(lparam);
            g_windowWidth = static_cast<int>(g_resizeWidth);
            g_windowHeight = static_cast<int>(g_resizeHeight);
            UpdateWindowRegion();
            return 0;
        case WM_SYSCOMMAND:
            if ((wparam & 0xfff0) == SC_KEYMENU) return 0;
            break;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                DestroyWindow(hwnd);
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            break;
    }
    return DefWindowProcW(hwnd, message, wparam, lparam);
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDPIAware();

    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    WNDCLASSW windowClass = {};
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WndProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    windowClass.lpszClassName = L"SkyroWtfGuiWindow";
    if (!RegisterClassW(&windowClass)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    const int startX = (GetSystemMetrics(SM_CXSCREEN) - kWindowWidth) / 2;
    const int startY = (GetSystemMetrics(SM_CYSCREEN) - kWindowHeight) / 2;
    g_window = CreateWindowExW(WS_EX_LAYERED, windowClass.lpszClassName, L"skyro.wtf",
                               WS_POPUP, startX, startY, kWindowWidth, kWindowHeight,
                               nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    ApplyWindowEffects();
    if (!CreateDeviceD3D(g_window)) {
        CleanupDeviceD3D();
        DestroyWindow(g_window);
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    LoadBackgroundTexture();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ApplyTheme();
    LoadFonts();

    ImGui_ImplWin32_Init(g_window);
    ImGui_ImplDX11_Init(g_device, g_context);
    UpdateUriBuffer();

    ShowWindow(g_window, SW_SHOW);
    UpdateWindow(g_window);

    bool running = true;
    while (running) {
        MSG message = {};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
            if (message.message == WM_QUIT) running = false;
        }
        if (!running) break;

        if (g_resizeWidth != 0 && g_resizeHeight != 0) {
            CleanupRenderTarget();
            g_swapChain->ResizeBuffers(0, g_resizeWidth, g_resizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_resizeWidth = 0;
            g_resizeHeight = 0;
            CreateRenderTarget();
        }

        RenderBlur();
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        ApplyTheme();
        DrawBackdrop();
        DrawInterface();
        ImGui::Render();

        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        const float clearColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_window);
    UnregisterClassW(windowClass.lpszClassName, instance);
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
