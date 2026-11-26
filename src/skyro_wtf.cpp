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
#include <cmath>
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

constexpr int kWindowWidth = 600;
constexpr int kWindowHeight = 400;
constexpr int kTopBarHeight = 56;
constexpr float kContentHeight = 480.0f;
constexpr float kDefaultDelaySeconds = 0.15f;
constexpr int kDefaultRetryCount = 10;
constexpr float kMinDelaySeconds = 0.01f;
constexpr float kMaxDelaySeconds = 60.0f;
constexpr int kMinRetryCount = 1;
constexpr int kMaxRetryCount = 1000;
constexpr float kScrollStep = 52.0f;
constexpr float kScrollSmoothing = 13.0f;
constexpr float kPi = 3.14159265358979323846f;

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

ImFont* g_font = nullptr;
ImFont* g_fontBold = nullptr;
ImFont* g_fontSmall = nullptr;

float g_delaySeconds = kDefaultDelaySeconds;
int g_retryCount = kDefaultRetryCount;
float g_rejoinPulse = 0.0f;
bool g_topMost = false;
bool g_serverOn = false;
float g_serverPulse = 0.0f;
float g_pendingWheel = 0.0f;
float g_scrollTarget = 0.0f;
float g_scrollCurrent = 0.0f;
float g_scrollMax = 0.0f;
std::string g_scriptMessage;
float g_scriptPulse = 0.0f;
float g_autoStopTimer = 0.0f;

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

std::string BuildLuaScript() {
    char buffer[4096];
    std::snprintf(buffer, sizeof(buffer), R"lua(-- tp guard bi pass - rejoin helper
-- Copy this file into your auto execute folder.
-- Reads the current game JobId and opens the deep link from the REJOIN button.
local config = { delay = %.2f, retry = %d }
if getgenv then getgenv().tp_guard_bi_pass = config end

local function openDeepLink(url)
    if syn and syn.request then
        syn.request({ Url = url, Method = "GET" })
        return true
    end
    if request then
        request({ Url = url, Method = "GET" })
        return true
    end
    if http_request then
        http_request({ Url = url, Method = "GET" })
        return true
    end
    if setclipboard then
        setclipboard(url)
        return true
    end
    return false
end

local function saveRejoin(pos, placeId, jobId, url)
    if makefolder then pcall(makefolder, "tp_guard") end
    if writefile then
        local payload = string.format("pos=%%s\nplaceId=%%d\njobId=%%s\nlink=%%s\ndelay=%%.2f\nretry=%%d\n",
            pos, placeId, jobId, url, config.delay, config.retry)
        pcall(writefile, "tp_guard/rejoin.txt", payload)
    end
end

local players = game:GetService("Players")
local localPlayer = players.LocalPlayer
local ok, coreGui = pcall(function() return game:GetService("CoreGui") end)
local parent = ok and coreGui or localPlayer.PlayerGui

local gui = Instance.new("ScreenGui")
gui.Name = "TpGuardBiPassRejoin"
gui.ResetOnSpawn = false
gui.Parent = parent

local button = Instance.new("TextButton")
button.Name = "Rejoin"
button.Size = UDim2.new(0, 188, 0, 46)
button.Position = UDim2.new(0.5, -94, 0.76, 0)
button.BackgroundColor3 = Color3.fromRGB(176, 79, 255)
button.TextColor3 = Color3.fromRGB(255, 255, 255)
button.Font = Enum.Font.SourceSansBold
button.TextSize = 18
button.Text = "REJOIN"
button.Parent = gui

local enabled = true

button.MouseButton1Click:Connect(function()
    if not enabled then
        enabled = true
        button.Text = "REJOIN"
        button.BackgroundColor3 = Color3.fromRGB(176, 79, 255)
        return
    end

    local placeId = game.PlaceId
    local jobId = game.JobId or ""
    local url = string.format("roblox://placeId=%%d&gameInstanceId=%%s", placeId, jobId)
    local pos = string.format("%%.3f,%%.3f", button.Position.X.Scale, button.Position.Y.Scale)

    saveRejoin(pos, placeId, jobId, url)

    if openDeepLink(url) then
        enabled = false
        button.Text = "TURN ON"
        button.BackgroundColor3 = Color3.fromRGB(84, 84, 96)
    end
end)

print(string.format("[tp guard bi pass] loaded - delay %%.2fs, retry %%d", config.delay, config.retry))
return config
)lua",
                  g_delaySeconds, g_retryCount);
    return std::string(buffer);
}

bool WriteLuaScript() {
    std::wstring directory = ModuleDirectory();
    std::wstring scriptDirectory = directory.empty() ? L"lua" : directory + L"lua";
    CreateDirectoryW(scriptDirectory.c_str(), nullptr);

    std::wstring scriptPath = scriptDirectory + L"\\skyro_wtf.lua";
    HANDLE file = CreateFileW(scriptPath.c_str(), GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;

    std::string script = BuildLuaScript();
    DWORD written = 0;
    BOOL ok = WriteFile(file, script.data(), static_cast<DWORD>(script.size()), &written, nullptr);
    CloseHandle(file);
    return ok != FALSE && written == static_cast<DWORD>(script.size());
}

ImVec4 AccentColor() {
    return ImVec4(0.69f, 0.31f, 1.00f, 1.0f);
}

ImU32 AccentU32(float alpha) {
    ImVec4 accent = AccentColor();
    return ImGui::ColorConvertFloat4ToU32(ImVec4(accent.x, accent.y, accent.z, alpha));
}

void ApplyTheme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4 accent = AccentColor();

    style.WindowBorderSize = 0.0f;
    style.FrameBorderSize = 0.0f;
    style.PopupBorderSize = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.WindowRounding = 14.0f;
    style.ChildRounding = 14.0f;
    style.FrameRounding = 8.0f;
    style.GrabRounding = 7.0f;
    style.ScrollbarRounding = 7.0f;
    style.WindowPadding = ImVec2(14.0f, 8.0f);
    style.FramePadding = ImVec2(9.0f, 7.0f);
    style.ItemSpacing = ImVec2(10.0f, 8.0f);
    style.ItemInnerSpacing = ImVec2(7.0f, 4.0f);
    style.Alpha = 1.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.035f, 0.018f, 0.070f, 0.32f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.12f, 0.07f, 0.19f, 0.62f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(accent.x * 0.42f, accent.y * 0.42f, accent.z * 0.42f, 0.70f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.78f);
    style.Colors[ImGuiCol_Button] = ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.84f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 0.94f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(accent.x, accent.y, accent.z, 0.22f);
    style.Colors[ImGuiCol_Separator] = ImVec4(accent.x, accent.y, accent.z, 0.28f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(accent.x, accent.y, accent.z, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(accent.x, accent.y, accent.z, 0.92f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.97f, 0.96f, 1.00f, 1.0f);
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
    HRGN region = CreateRoundRectRgn(0, 0, g_windowWidth + 1, g_windowHeight + 1, 22, 22);
    if (region) SetWindowRgn(g_window, region, TRUE);
}

void ApplyTopMost() {
    if (!g_window) return;
    SetWindowPos(g_window, g_topMost ? HWND_TOPMOST : HWND_NOTOPMOST,
                 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
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
        AccentPolicy policy = {3, 0, 0, 0};
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

    drawList->AddRectFilledMultiColor(ImVec2(display.x * 0.28f, 0.0f), display,
                                      IM_COL32(0, 0, 0, 0), AccentU32(0.12f),
                                      IM_COL32(0, 0, 0, 0), AccentU32(0.05f));
}

void BeginCard(const char* id, ImVec2 size) {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 14.0f);
    ImGui::BeginChild(id, size, ImGuiChildFlags_Border, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
}

void EndCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar();
}

bool PrimaryButton(const char* label, ImVec2 size) {
    ImVec4 accent = AccentColor();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(accent.x * 0.55f, accent.y * 0.55f, accent.z * 0.55f, 0.84f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(accent.x * 0.82f, accent.y * 0.82f, accent.z * 0.82f, 0.94f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(accent.x, accent.y, accent.z, 1.0f));
    bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

void DrawPowerGlyph(ImDrawList* drawList, ImVec2 center, ImU32 color) {
    drawList->PathArcTo(center, 8.5f, -kPi * 0.5f + 0.45f, kPi * 1.5f - 0.45f, 24);
    drawList->PathStroke(color, 0, 2.0f);
    drawList->AddLine(center + ImVec2(0.0f, -9.0f), center + ImVec2(0.0f, -1.0f), color, 2.0f);
}

void DrawTopBar() {
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const float width = ImGui::GetWindowWidth();

    drawList->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(width, kTopBarHeight), IM_COL32(12, 5, 24, 142));
    drawList->AddLine(ImVec2(0.0f, kTopBarHeight - 1.0f), ImVec2(width, kTopBarHeight - 1.0f), AccentU32(0.28f), 1.0f);

    ImVec2 logoMin(18.0f, 11.0f);
    ImVec2 logoMax = logoMin + ImVec2(32.0f, 32.0f);
    drawList->AddRectFilled(logoMin, logoMax, AccentU32(0.95f), 10.0f);
    drawList->AddText(g_fontBold, 21.0f, logoMin + ImVec2(9.0f, 4.0f), IM_COL32(255, 255, 255, 255), "S");

    drawList->AddText(g_fontBold, 21.0f, ImVec2(62.0f, 9.0f), IM_COL32(250, 248, 255, 255), "skyro.wtf");
    drawList->AddText(g_fontSmall, 11.0f, ImVec2(63.0f, 34.0f), IM_COL32(180, 166, 202, 255), "TP GUARD BI PASS");

    ImVec2 powerPos(width - 196.0f, 14.0f);
    ImVec2 powerCenter = powerPos + ImVec2(15.0f, 14.0f);
    if (g_serverPulse > 0.0f) {
        drawList->AddCircle(powerCenter, 15.0f + (1.0f - g_serverPulse) * 9.0f,
                            AccentU32(g_serverPulse * 0.30f), 28, 1.0f);
        g_serverPulse -= ImGui::GetIO().DeltaTime * 2.0f;
        if (g_serverPulse < 0.0f) g_serverPulse = 0.0f;
    }

    ImGui::SetCursorScreenPos(powerPos);
    if (ImGui::InvisibleButton("##power", ImVec2(30.0f, 28.0f))) {
        g_serverOn = !g_serverOn;
        g_serverPulse = 1.0f;
        g_autoStopTimer = 0.0f;
    }
    bool powerHovered = ImGui::IsItemHovered();
    ImU32 powerColor = g_serverOn ? AccentU32(0.98f)
                                  : (powerHovered ? IM_COL32(220, 210, 238, 240) : IM_COL32(190, 178, 210, 220));
    if (powerHovered) {
        drawList->AddRectFilled(powerPos, powerPos + ImVec2(30.0f, 28.0f), IM_COL32(255, 255, 255, 18), 7.0f);
    }
    DrawPowerGlyph(drawList, powerCenter, powerColor);
    drawList->AddText(g_fontSmall, 10.0f, powerPos + ImVec2(34.0f, 8.0f),
                      g_serverOn ? IM_COL32(170, 255, 210, 255) : IM_COL32(160, 148, 182, 255),
                      g_serverOn ? "ON" : "OFF");

    ImVec2 topPos(width - 142.0f, 14.0f);
    ImGui::SetCursorScreenPos(topPos);
    if (ImGui::InvisibleButton("##topmost", ImVec2(38.0f, 28.0f))) {
        g_topMost = !g_topMost;
        ApplyTopMost();
    }
    bool topHovered = ImGui::IsItemHovered();
    drawList->AddRectFilled(topPos, topPos + ImVec2(38.0f, 28.0f),
                            g_topMost ? AccentU32(0.84f)
                                      : (topHovered ? IM_COL32(255, 255, 255, 28) : IM_COL32(255, 255, 255, 12)), 7.0f);
    drawList->AddText(g_fontSmall, 11.0f, topPos + ImVec2(9.0f, 8.0f),
                      g_topMost ? IM_COL32(255, 255, 255, 255) : IM_COL32(210, 200, 228, 230), "TOP");

    ImVec2 closePos(width - 50.0f, 14.0f);
    ImGui::SetCursorScreenPos(closePos);
    if (ImGui::InvisibleButton("##close", ImVec2(30.0f, 28.0f))) PostMessageW(g_window, WM_CLOSE, 0, 0);
    bool closeHovered = ImGui::IsItemHovered();
    drawList->AddRectFilled(closePos, closePos + ImVec2(30.0f, 28.0f),
                            closeHovered ? IM_COL32(236, 68, 110, 155) : IM_COL32(255, 255, 255, 12), 7.0f);
    drawList->AddText(g_fontBold, 16.0f, closePos + ImVec2(9.0f, 3.0f), IM_COL32(255, 245, 250, 255), "x");
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

    const ImVec2 contentStart(20.0f, kTopBarHeight + 16.0f);
    const ImVec2 contentSize(g_windowWidth - 40.0f, g_windowHeight - kTopBarHeight - 32.0f);
    const ImVec2 contentEnd = contentStart + contentSize;

    g_scrollMax = std::max(0.0f, kContentHeight - contentSize.y);
    if (ImGui::IsMouseHoveringRect(contentStart, contentEnd)) {
        g_scrollTarget = std::clamp(g_scrollTarget - g_pendingWheel * kScrollStep, 0.0f, g_scrollMax);
    }
    g_pendingWheel = 0.0f;

    const float blend = 1.0f - std::exp(-ImGui::GetIO().DeltaTime * kScrollSmoothing);
    g_scrollCurrent += (g_scrollTarget - g_scrollCurrent) * blend;
    if (std::fabs(g_scrollTarget - g_scrollCurrent) < 0.15f) g_scrollCurrent = g_scrollTarget;
    g_scrollTarget = std::clamp(g_scrollTarget, 0.0f, g_scrollMax);
    g_scrollCurrent = std::clamp(g_scrollCurrent, 0.0f, g_scrollMax);

    if (g_autoStopTimer > 0.0f) {
        g_autoStopTimer -= ImGui::GetIO().DeltaTime;
        if (g_autoStopTimer <= 0.0f) {
            g_autoStopTimer = 0.0f;
            g_serverOn = false;
            g_serverPulse = 1.0f;
            g_scriptMessage = "Join sent. Power is off until you turn it on again.";
        }
    }

    ImGui::SetCursorScreenPos(contentStart);
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.0f));
    ImGui::BeginChild("content", contentSize, 0,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::SetScrollY(g_scrollCurrent);

    ImGui::TextColored(AccentColor(), "TP GUARD BI PASS");
    ImGui::PushFont(g_fontBold);
    ImGui::Text("Rejoin");
    ImGui::PopFont();
    ImGui::TextDisabled("Set the timing, then run the rejoin.");

    ImGui::Spacing();

    BeginCard("timing_card", ImVec2(-1.0f, 150.0f));
    ImGui::TextColored(AccentColor(), "TIMING");
    g_delaySeconds = std::clamp(g_delaySeconds, kMinDelaySeconds, kMaxDelaySeconds);
    g_retryCount = std::clamp(g_retryCount, kMinRetryCount, kMaxRetryCount);

    ImGui::Text("Delay Between Each Join");
    ImGui::SetNextItemWidth(-76.0f);
    ImGui::InputFloat("##delay", &g_delaySeconds, 0.01f, 0.01f, "%.2f", ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    ImGui::TextDisabled("sec");

    ImGui::Text("Retry");
    ImGui::SetNextItemWidth(-76.0f);
    ImGui::InputInt("##retry", &g_retryCount, 0, 0);
    ImGui::SameLine();
    ImGui::TextDisabled("times");
    EndCard();

    ImGui::Spacing();

    ImGui::BeginDisabled(!g_serverOn);
    if (g_rejoinPulse > 0.0f) {
        ImVec2 buttonMin = ImGui::GetCursorScreenPos();
        ImVec2 buttonMax = buttonMin + ImVec2(ImGui::GetContentRegionAvail().x, 34.0f);
        ImGui::GetWindowDrawList()->AddRect(buttonMin, buttonMax, AccentU32(g_rejoinPulse * 0.55f), 9.0f, 0, 2.0f);
        g_rejoinPulse -= ImGui::GetIO().DeltaTime * 1.8f;
        if (g_rejoinPulse < 0.0f) g_rejoinPulse = 0.0f;
    }
    if (PrimaryButton("REJOIN", ImVec2(-1.0f, 34.0f))) {
        g_rejoinPulse = 1.0f;
        g_autoStopTimer = 1.2f;
    }
    ImGui::EndDisabled();

    ImGui::Spacing();

    BeginCard("instructions_card", ImVec2(-1.0f, 116.0f));
    ImGui::TextColored(AccentColor(), "START");
    ImGui::TextDisabled("1");
    ImGui::SameLine(34.0f);
    ImGui::Text("Click the power button in the top bar.");
    ImGui::TextDisabled("2");
    ImGui::SameLine(34.0f);
    ImGui::Text("Set delay and retry, then press REJOIN.");
    ImGui::TextDisabled("3");
    ImGui::SameLine(34.0f);
    ImGui::Text("Join sends and power turns off.");
    EndCard();

    ImGui::Spacing();

    if (g_scriptPulse > 0.0f) {
        ImVec2 buttonMin = ImGui::GetCursorScreenPos();
        ImVec2 buttonMax = buttonMin + ImVec2(ImGui::GetContentRegionAvail().x, 34.0f);
        ImGui::GetWindowDrawList()->AddRect(buttonMin, buttonMax, AccentU32(g_scriptPulse * 0.55f), 9.0f, 0, 2.0f);
        g_scriptPulse -= ImGui::GetIO().DeltaTime * 1.8f;
        if (g_scriptPulse < 0.0f) g_scriptPulse = 0.0f;
    }
    if (PrimaryButton("GET LUA SCRIPT", ImVec2(-1.0f, 34.0f))) {
        g_scriptMessage = WriteLuaScript()
                              ? "Saved to lua\\skyro_wtf.lua - put it in your auto execute folder."
                              : "Could not write lua\\skyro_wtf.lua.";
        g_scriptPulse = 1.0f;
    }

    if (!g_scriptMessage.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("%s", g_scriptMessage.c_str());
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();

    const float railX = contentStart.x + contentSize.x - 5.0f;
    const float trackTop = contentStart.y + 8.0f;
    const float trackBottom = contentEnd.y - 8.0f;
    const float trackHeight = trackBottom - trackTop;
    const float thumbHeight = std::max(28.0f, trackHeight * contentSize.y / std::max(1.0f, kContentHeight));
    const float maxTravel = std::max(1.0f, trackHeight - thumbHeight);
    const float thumbY = trackTop + (g_scrollMax > 0.0f ? (g_scrollCurrent / g_scrollMax) : 0.0f) * maxTravel;

    ImGui::SetCursorScreenPos(ImVec2(railX - 6.0f, trackTop));
    if (ImGui::InvisibleButton("##scrollrail", ImVec2(12.0f, trackHeight))) {
        const float ratio = std::clamp((ImGui::GetMousePos().y - trackTop - thumbHeight * 0.5f) / maxTravel, 0.0f, 1.0f);
        g_scrollTarget = ratio * g_scrollMax;
    }
    bool railHovered = ImGui::IsItemHovered();
    bool railActive = ImGui::IsItemActive();
    if (railActive) {
        const float ratio = std::clamp((ImGui::GetMousePos().y - trackTop - thumbHeight * 0.5f) / maxTravel, 0.0f, 1.0f);
        g_scrollTarget = ratio * g_scrollMax;
        g_scrollCurrent = g_scrollTarget;
    }

    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(ImVec2(railX - 2.0f, trackTop), ImVec2(railX + 2.0f, trackBottom), IM_COL32(255, 255, 255, 16), 2.0f);
    drawList->AddRectFilled(ImVec2(railX - 2.0f, thumbY), ImVec2(railX + 2.0f, thumbY + thumbHeight),
                            railActive ? AccentU32(1.0f) : (railHovered ? AccentU32(0.78f) : AccentU32(0.48f)), 2.0f);

    ImGui::End();
}

}  // namespace

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (message == WM_MOUSEWHEEL) {
        g_pendingWheel += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
    }
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) return 1;

    switch (message) {
        case WM_NCHITTEST: {
            POINT point{static_cast<int>(static_cast<short>(LOWORD(lparam))),
                        static_cast<int>(static_cast<short>(HIWORD(lparam)))};
            ScreenToClient(hwnd, &point);
            if (point.y >= 0 && point.y < kTopBarHeight && point.x >= 0 && point.x < g_windowWidth - 210)
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
