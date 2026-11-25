#ifndef UNICODE
#define UNICODE
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>
#include <d3d11.h>
#include <dxgi.h>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#endif

#include "imgui.h"
#include "backends/imgui_impl_win32.h"
#include "backends/imgui_impl_dx11.h"

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

namespace {

constexpr int kPortMin = 1;
constexpr int kPortMax = 65535;
constexpr int kRetriesMin = 1;
constexpr int kRetriesMax = 1000;
constexpr int kDelayMin = 50;
constexpr int kDelayMax = 60000;

struct Config {
    std::wstring placeId = L"109983668079237";
    int port = 6969;
    int retries = 10;
    int delayMs = 1500;
    bool enabled = true;
};

std::mutex g_configMutex;
Config g_config;

std::atomic<bool> g_stop{false};
std::atomic<bool> g_serverRunning{false};
std::atomic<int> g_captured{0};
std::atomic<int> g_attempts{0};
std::thread g_serverThread;
std::thread g_workerThread;

std::mutex g_jobMutex;
std::condition_variable g_jobCv;
std::queue<std::wstring> g_jobs;

std::mutex g_statusMutex;
std::wstring g_status = L"idle";
std::mutex g_lastJobMutex;
std::wstring g_lastJob = L"none";

ID3D11Device* g_device = nullptr;
ID3D11DeviceContext* g_context = nullptr;
IDXGISwapChain* g_swapChain = nullptr;
ID3D11RenderTargetView* g_renderTarget = nullptr;
ID3D11ShaderResourceView* g_background = nullptr;
ULONG_PTR g_gdiplusToken = 0;
HWND g_window = nullptr;

char g_placeBuffer[64] = "109983668079237";
char g_portBuffer[16] = "6969";
char g_retriesBuffer[16] = "10";
char g_delayBuffer[16] = "1500";
char g_scriptBuffer[16384] = "";

ImFont* g_uiFont = nullptr;
ImFont* g_monoFont = nullptr;

std::wstring Utf8ToWide(const char* text) {
    if (!text) return L"";
    int needed = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring wide(static_cast<size_t>(needed) - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text, -1, &wide[0], needed);
    return wide;
}

std::string WideToUtf8(const std::wstring& text) {
    int needed = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return "";
    std::string narrow(static_cast<size_t>(needed), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), &narrow[0], needed, nullptr, nullptr);
    return narrow;
}

std::wstring Trim(const std::wstring& text) {
    const wchar_t* start = text.c_str();
    while (*start == L' ' || *start == L'\t' || *start == L'\r' || *start == L'\n') ++start;
    const wchar_t* end = start + std::wcslen(start);
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t' || end[-1] == L'\r' || end[-1] == L'\n')) --end;
    return std::wstring(start, end);
}

int ParseBoundedInt(const char* text, int fallback, int low, int high) {
    if (!text || !*text) return fallback;
    char* tail = nullptr;
    long value = std::strtol(text, &tail, 10);
    if (tail == text || *tail != '\0') return fallback;
    if (value < low) return low;
    if (value > high) return high;
    return static_cast<int>(value);
}

void SetStatus(const std::wstring& value) {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    g_status = value;
}

std::wstring GetStatus() {
    std::lock_guard<std::mutex> lock(g_statusMutex);
    return g_status;
}

void SetLastJob(const std::wstring& value) {
    std::lock_guard<std::mutex> lock(g_lastJobMutex);
    g_lastJob = value;
}

std::wstring GetLastJob() {
    std::lock_guard<std::mutex> lock(g_lastJobMutex);
    return g_lastJob;
}

void ReplaceAll(std::wstring& text, const std::wstring& from, const std::wstring& to) {
    if (from.empty()) return;
    size_t at = 0;
    while ((at = text.find(from, at)) != std::wstring::npos) {
        text.replace(at, from.length(), to);
        at += to.length();
    }
}

std::wstring BuildScript(const Config& cfg) {
    std::wstring script = LR"lua(-- skyro.wtf deep link interceptor
-- paste into auto execute

local PLACE_ID = __PLACE__
local PORT = __PORT__
local RETRIES = __RETRIES__
local DELAY = __DELAY__
local ENDPOINT = "http://127.0.0.1:" .. PORT .. "/job"

local Http = game:GetService("HttpService")
local TS = game:GetService("TeleportService")
local Players = game:GetService("Players")
local ReplicatedStorage = game:GetService("ReplicatedStorage")
local ReplicatedFirst = game:GetService("ReplicatedFirst")

local seen = {}
local cacheCount = 0
local running = true
local connections = {}

local function isJobId(value)
    if type(value) ~= "string" then return false end
    return value:match("^%x+%-%x+%-%x+%-%x+%-%x+$") ~= nil
end

local function remember(value)
    if not isJobId(value) then return false end
    if seen[value] then return false end
    seen[value] = true
    cacheCount = cacheCount + 1
    if cacheCount > 128 then
        for key in pairs(seen) do seen[key] = nil end
        seen[value] = true
        cacheCount = 1
    end
    return true
end

local function postJob(jobId)
    local body = Http:JSONEncode({jobId = jobId, placeId = PLACE_ID})
    local headers = {["Content-Type"] = "application/json"}
    pcall(function()
        if syn and syn.request then
            syn.request({Url = ENDPOINT, Method = "POST", Headers = headers, Body = body})
            return
        end
        if http_request then
            http_request({Url = ENDPOINT, Method = "POST", Headers = headers, Body = body})
            return
        end
        if request then
            request({Url = ENDPOINT, Method = "POST", Headers = headers, Body = body})
        end
    end)
end

local function deepLink(jobId)
    -- desktop launcher opens roblox://experiences/start?placeId=<PLACE_ID>&gameInstanceId=<jobId>
    for attempt = 1, RETRIES do
        local sent = pcall(function()
            TS:TeleportToPlaceInstance(PLACE_ID, jobId, Players.LocalPlayer)
        end)
        if sent then
            local started = tick()
            while tick() - started < (DELAY * 2) do
                if game.JobId == jobId then return true end
                wait(0.1)
            end
        end
        wait(DELAY)
    end
    return false
end

local function handle(value)
    if not remember(value) then return end
    pcall(postJob, value)
    pcall(deepLink, value)
end

local function scanValue(value)
    if type(value) == "string" then
        handle(value)
        return
    end
    if type(value) == "table" then
        for key, item in pairs(value) do
            if type(item) == "string" then handle(item) end
        end
    end
end

local function scanInstance(instance)
    local kindOk, kind = pcall(function() return instance.ClassName end)
    if not kindOk then return end
    if kind == "StringValue" or kind == "TextBox" or kind == "TextLabel" then
        local valueOk, content = pcall(function()
            if kind == "StringValue" then return instance.Value end
            return instance.Text
        end)
        if valueOk then scanValue(content) end
    end
    local attrOk, attrs = pcall(function() return instance:GetAttributes() end)
    if attrOk then scanValue(attrs) end
end

local function scanTree(root, limit)
    if not root then return end
    local listOk, items = pcall(function() return root:GetDescendants() end)
    if not listOk then return end
    local scanned = 0
    for index = 1, #items do
        if scanned >= limit then return end
        scanInstance(items[index])
        scanned = scanned + 1
    end
end

local function watch(root)
    if not root then return end
    local addedOk, connection = pcall(function() return root.DescendantAdded:Connect(scanInstance) end)
    if addedOk then table.insert(connections, connection) end
end

watch(ReplicatedStorage)
watch(ReplicatedFirst)
watch(Players)

local playerOk, playerConnection = pcall(function()
    return Players.PlayerAdded:Connect(function(player)
        local attrOk, attrs = pcall(function() return player:GetAttributes() end)
        if attrOk then scanValue(attrs) end
    end)
end)
if playerOk then table.insert(connections, playerConnection) end

spawn(function()
    while running do
        handle(game.JobId)
        scanTree(ReplicatedStorage, 400)
        scanTree(ReplicatedFirst, 200)
        scanTree(Players, 200)
        wait(0.5)
    end
end)
)lua";

    ReplaceAll(script, L"__PLACE__", cfg.placeId);
    ReplaceAll(script, L"__PORT__", std::to_wstring(cfg.port));
    ReplaceAll(script, L"__RETRIES__", std::to_wstring(cfg.retries));
    ReplaceAll(script, L"__DELAY__", std::to_wstring(static_cast<double>(cfg.delayMs) / 1000.0).substr(0, 6));
    return script;
}

void RefreshScriptBuffer() {
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg = g_config;
    }
    std::string narrow = WideToUtf8(BuildScript(cfg));
    if (narrow.size() >= sizeof(g_scriptBuffer)) narrow.resize(sizeof(g_scriptBuffer) - 1);
    std::memcpy(g_scriptBuffer, narrow.c_str(), narrow.size() + 1);
}

void SyncConfigFromUi() {
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg = g_config;
    }

    std::wstring place = Trim(Utf8ToWide(g_placeBuffer));
    if (place.empty()) place = L"109983668079237";
    int port = ParseBoundedInt(g_portBuffer, cfg.port, kPortMin, kPortMax);
    int retries = ParseBoundedInt(g_retriesBuffer, cfg.retries, kRetriesMin, kRetriesMax);
    int delay = ParseBoundedInt(g_delayBuffer, cfg.delayMs, kDelayMin, kDelayMax);

    if (place != cfg.placeId || port != cfg.port || retries != cfg.retries || delay != cfg.delayMs) {
        {
            std::lock_guard<std::mutex> lock(g_configMutex);
            g_config.placeId = place;
            g_config.port = port;
            g_config.retries = retries;
            g_config.delayMs = delay;
        }
        RefreshScriptBuffer();
    }
}

std::wstring ExtractField(const std::string& request, const std::string& key) {
    const std::string marker = "\"" + key + "\"";
    size_t at = request.find(marker);
    if (at == std::string::npos) return L"";
    at = request.find(':', at + marker.size());
    if (at == std::string::npos) return L"";
    at = request.find('"', at + 1);
    if (at == std::string::npos) return L"";
    size_t end = request.find('"', at + 1);
    if (end == std::string::npos || end == at + 1) return L"";
    std::string raw = request.substr(at + 1, end - at - 1);
    int needed = MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
    if (needed <= 0) return L"";
    std::wstring wide(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), &wide[0], needed);
    return wide;
}

void HandleClient(SOCKET client) {
    std::string request;
    request.reserve(8192);
    char buffer[2048] = {};
    while (!g_stop) {
        int got = recv(client, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        request.append(buffer, static_cast<size_t>(got));
        if (request.size() > 65536 || request.find("\r\n\r\n") != std::string::npos) break;
    }

    const char* response =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: application/json\r\n"
        "Access-Control-Allow-Origin: *\r\n"
        "Content-Length: 15\r\n"
        "Connection: close\r\n\r\n"
        "{\"ok\":true}\r\n";
    send(client, response, static_cast<int>(std::strlen(response)), 0);
    closesocket(client);

    std::wstring jobId = ExtractField(request, "jobId");
    if (jobId.empty()) return;

    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        enabled = g_config.enabled;
    }
    if (!enabled) {
        SetStatus(L"job ignored - interceptor off");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_jobMutex);
        g_jobs.push(jobId);
    }
    g_jobCv.notify_one();
    g_captured.fetch_add(1);
    SetLastJob(jobId);
    SetStatus(L"job captured: " + jobId.substr(0, 18));
}

void ServerThread() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        SetStatus(L"winsock init failed");
        return;
    }

    int port = 6969;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        port = g_config.port;
    }

    while (!g_stop) {
        SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == INVALID_SOCKET) {
            SetStatus(L"socket creation failed");
            Sleep(1000);
            continue;
        }

        sockaddr_in address = {};
        address.sin_family = AF_INET;
        address.sin_port = htons(static_cast<u_short>(port));
        address.sin_addr.s_addr = inet_addr("127.0.0.1");

        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == SOCKET_ERROR) {
            closesocket(listener);
            SetStatus(L"bind failed on port " + std::to_wstring(port));
            Sleep(1000);
            continue;
        }
        if (listen(listener, 16) == SOCKET_ERROR) {
            closesocket(listener);
            SetStatus(L"listen failed");
            Sleep(1000);
            continue;
        }

        SetStatus(L"listening on 127.0.0.1:" + std::to_wstring(port));
        while (!g_stop) {
            SOCKET client = accept(listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) break;
            std::thread(HandleClient, client).detach();
        }
        closesocket(listener);
    }
    WSACleanup();
    SetStatus(L"listener stopped");
}

void OpenDeepLink(const std::wstring& jobId) {
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg = g_config;
    }
    std::wstring uri = L"roblox://experiences/start?placeId=" + cfg.placeId + L"&gameInstanceId=" + jobId;
    ShellExecuteW(nullptr, L"open", uri.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void WorkerThread() {
    while (!g_stop) {
        std::unique_lock<std::mutex> lock(g_jobMutex);
        g_jobCv.wait(lock, [] { return !g_jobs.empty() || g_stop.load(); });
        if (g_stop) break;
        std::wstring job = g_jobs.front();
        g_jobs.pop();
        lock.unlock();

        Config cfg;
        {
            std::lock_guard<std::mutex> configLock(g_configMutex);
            cfg = g_config;
        }
        if (!cfg.enabled) continue;

        for (int attempt = 1; attempt <= cfg.retries && !g_stop; ++attempt) {
            SetStatus(L"deep link attempt " + std::to_wstring(attempt) + L"/" +
                      std::to_wstring(cfg.retries) + L" - " + job.substr(0, 18));
            OpenDeepLink(job);
            g_attempts.fetch_add(1);
            DWORD step = static_cast<DWORD>(cfg.delayMs);
            while (step > 0 && !g_stop) {
                DWORD slice = step > 100 ? 100 : step;
                Sleep(slice);
                step -= slice;
            }
        }
        SetStatus(L"finished retries for " + job.substr(0, 18));
    }
}

void StartServer() {
    if (g_serverRunning) return;
    SyncConfigFromUi();
    g_stop = false;
    g_serverThread = std::thread(ServerThread);
    g_workerThread = std::thread(WorkerThread);
    g_serverRunning = true;
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg = g_config;
    }
    std::wstring base = L"roblox://experiences/start?placeId=" + cfg.placeId;
    ShellExecuteW(nullptr, L"open", base.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    SetStatus(L"server started - base deep link fired");
}

void StopServer() {
    if (!g_serverRunning) return;
    g_stop = true;
    g_jobCv.notify_all();
    if (g_workerThread.joinable()) g_workerThread.join();
    if (g_serverThread.joinable()) g_serverThread.join();
    g_serverRunning = false;
    SetStatus(L"server stopped");
}

bool CreateDeviceD3D(HWND hwnd) {
    DXGI_SWAP_CHAIN_DESC desc = {};
    desc.BufferCount = 2;
    desc.BufferDesc.Width = 0;
    desc.BufferDesc.Height = 0;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow = hwnd;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Windowed = TRUE;

    D3D_FEATURE_LEVEL level = D3D_FEATURE_LEVEL_11_0;
    if (FAILED(D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                             &level, 1, D3D11_SDK_VERSION, &desc,
                                             &g_swapChain, &g_device, nullptr, &g_context))) {
        return false;
    }
    return true;
}

void CleanupDeviceD3D() {
    if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
    if (g_background) { g_background->Release(); g_background = nullptr; }
    if (g_swapChain) { g_swapChain->Release(); g_swapChain = nullptr; }
    if (g_context) { g_context->Release(); g_context = nullptr; }
    if (g_device) { g_device->Release(); g_device = nullptr; }
}

void CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer)))) {
        g_device->CreateRenderTargetView(backBuffer, nullptr, &g_renderTarget);
        backBuffer->Release();
    }
}

void CleanupRenderTarget() {
    if (g_renderTarget) { g_renderTarget->Release(); g_renderTarget = nullptr; }
}

void LoadBackgroundTexture() {
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) return;
    std::wstring path(modulePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    path.resize(slash + 1);
    path += L"assets\\login.jpg";

    Gdiplus::Image source(path.c_str());
    if (source.GetLastStatus() != Gdiplus::Ok) return;

    const INT width = static_cast<INT>(source.GetWidth());
    const INT height = static_cast<INT>(source.GetHeight());
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
    if (SUCCEEDED(g_device->CreateTexture2D(&textureDesc, &upload, &texture))) {
        D3D11_SHADER_RESOURCE_VIEW_DESC viewDesc = {};
        viewDesc.Format = textureDesc.Format;
        viewDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        viewDesc.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(texture, &viewDesc, &g_background);
        texture->Release();
    }
    bitmap.UnlockBits(&data);
}

void LoadFonts() {
    wchar_t fontsDirectory[MAX_PATH] = {};
    GetWindowsDirectoryW(fontsDirectory, MAX_PATH);
    std::wstring base(fontsDirectory);
    base += L"\\Fonts\\";

    ImGuiIO& io = ImGui::GetIO();
    std::string uiPath = WideToUtf8(base + L"segoeui.ttf");
    std::string monoPath = WideToUtf8(base + L"consola.ttf");
    g_uiFont = io.Fonts->AddFontFromFileTTF(uiPath.c_str(), 15.0f);
    g_monoFont = io.Fonts->AddFontFromFileTTF(monoPath.c_str(), 13.0f);
    if (!g_uiFont) g_uiFont = io.Fonts->AddFontDefault();
    if (!g_monoFont) g_monoFont = g_uiFont;
}

void ApplyStyle() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 12.0f;
    style.FrameRounding = 6.0f;
    style.GrabRounding = 6.0f;
    style.PopupRounding = 8.0f;
    style.WindowPadding = ImVec2(14.0f, 12.0f);
    style.FramePadding = ImVec2(8.0f, 5.0f);
    style.ItemSpacing = ImVec2(10.0f, 7.0f);
    style.ItemInnerSpacing = ImVec2(6.0f, 4.0f);
    style.ScrollbarSize = 10.0f;

    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.07f, 0.03f, 0.13f, 0.94f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.10f, 0.05f, 0.18f, 0.92f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.08f, 0.27f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.11f, 0.36f, 1.00f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.28f, 0.14f, 0.45f, 1.00f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.35f, 0.15f, 0.58f, 1.00f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.69f, 0.31f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.52f, 0.22f, 0.82f, 1.00f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.69f, 0.31f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.41f, 0.20f, 0.62f, 1.00f);
    style.Colors[ImGuiCol_Text] = ImVec4(0.97f, 0.95f, 1.00f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.54f, 0.72f, 1.00f);
}

void Section(const char* title) {
    ImGui::TextColored(ImVec4(0.69f, 0.31f, 1.00f, 1.00f), "%s", title);
    ImGui::Separator();
    ImGui::Spacing();
}

void DrawInterface() {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList* backdrop = ImGui::GetBackgroundDrawList();
    if (g_background) {
        backdrop->AddImage(g_background, ImVec2(0, 0), display, ImVec2(0, 0), ImVec2(1, 1),
                           IM_COL32(74, 44, 112, 235));
    } else {
        backdrop->AddRectFilled(ImVec2(0, 0), display, IM_COL32(13, 6, 24, 255));
    }

    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(520, 590), ImGuiCond_FirstUseEver);
    ImGui::Begin("skyro.wtf", nullptr, ImGuiWindowFlags_NoCollapse);

    ImGui::PushFont(g_uiFont);
    ImGui::TextColored(ImVec4(0.69f, 0.31f, 1.00f, 1.00f), "skyro.wtf");
    ImGui::PopFont();
    ImGui::TextDisabled("deep link server joiner");
    ImGui::Spacing();

    Section("CONTROL");
    bool enabled = false;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        enabled = g_config.enabled;
    }
    if (ImGui::Checkbox("Interceptor", &enabled)) {
        std::lock_guard<std::mutex> lock(g_configMutex);
        g_config.enabled = enabled;
        SetStatus(enabled ? L"interceptor on" : L"interceptor off");
    }
    ImGui::SameLine();
    ImGui::TextDisabled(enabled ? "capturing job ids" : "jobs ignored");
    if (ImGui::Button(g_serverRunning ? "STOP SERVER" : "START SERVER", ImVec2(-1, 0))) {
        if (g_serverRunning) StopServer();
        else StartServer();
    }

    ImGui::Spacing();
    Section("TARGET");
    ImGui::PushItemWidth(240.0f);
    ImGui::InputText("Place ID", g_placeBuffer, IM_ARRAYSIZE(g_placeBuffer));
    ImGui::InputText("Port", g_portBuffer, IM_ARRAYSIZE(g_portBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::InputText("Retries", g_retriesBuffer, IM_ARRAYSIZE(g_retriesBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::InputText("Delay (ms)", g_delayBuffer, IM_ARRAYSIZE(g_delayBuffer), ImGuiInputTextFlags_CharsDecimal);
    ImGui::PopItemWidth();
    SyncConfigFromUi();

    ImGui::Spacing();
    Section("LIVE");
    std::wstring status = GetStatus();
    std::wstring lastJob = GetLastJob();
    ImGui::Text("status");
    ImGui::SameLine(110.0f);
    ImGui::TextWrapped("%s", WideToUtf8(status).c_str());
    ImGui::Text("captured");
    ImGui::SameLine(110.0f);
    ImGui::Text("%d", g_captured.load());
    ImGui::Text("attempts");
    ImGui::SameLine(110.0f);
    ImGui::Text("%d", g_attempts.load());
    ImGui::Text("last job");
    ImGui::SameLine(110.0f);
    ImGui::TextWrapped("%s", WideToUtf8(lastJob).c_str());

    ImGui::Spacing();
    Section("AUTO EXECUTE SCRIPT");
    if (ImGui::Button("COPY SCRIPT", ImVec2(-1, 0))) {
        ImGui::SetClipboardText(g_scriptBuffer);
        SetStatus(L"lua script copied to clipboard");
    }
    ImGui::PushFont(g_monoFont);
    ImGui::InputTextMultiline("##script", g_scriptBuffer, IM_ARRAYSIZE(g_scriptBuffer),
                              ImVec2(-1, 250), ImGuiInputTextFlags_ReadOnly);
    ImGui::PopFont();

    ImGui::End();
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, message, wparam, lparam)) return 1;

    switch (message) {
        case WM_SIZE:
            if (g_device && wparam != SIZE_MINIMIZED) {
                CleanupRenderTarget();
                g_swapChain->ResizeBuffers(0, static_cast<UINT>(LOWORD(lparam)),
                                           static_cast<UINT>(HIWORD(lparam)), DXGI_FORMAT_UNKNOWN, 0);
                CreateRenderTarget();
            }
            return 0;
        case WM_CLOSE:
            StopServer();
            DestroyWindow(hwnd);
            return 0;
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    Gdiplus::GdiplusStartupInput gdiplusInput;
    if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &gdiplusInput, nullptr) != Gdiplus::Ok) return 1;

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"SkyroWtfWindow";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    g_window = CreateWindowExW(0, L"SkyroWtfWindow", L"skyro.wtf - Deep Link Server Joiner",
                               WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 560, 640,
                               nullptr, nullptr, instance, nullptr);
    if (!g_window) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    if (!CreateDeviceD3D(g_window)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }
    CreateRenderTarget();
    LoadBackgroundTexture();

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ApplyStyle();
    LoadFonts();

    ImGui_ImplWin32_Init(g_window);
    ImGui_ImplDX11_Init(g_device, g_context);
    RefreshScriptBuffer();

    ShowWindow(g_window, show);
    UpdateWindow(g_window);

    bool running = true;
    while (running) {
        MSG msg = {};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) running = false;
        }
        if (!running) break;

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();
        DrawInterface();
        ImGui::Render();

        g_context->OMSetRenderTargets(1, &g_renderTarget, nullptr);
        const float clearColor[4] = {0.05f, 0.02f, 0.09f, 1.0f};
        g_context->ClearRenderTargetView(g_renderTarget, clearColor);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_swapChain->Present(1, 0);
    }

    StopServer();
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
