#ifndef UNICODE
#define UNICODE
#endif

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <shellapi.h>
#include <gdiplus.h>

#ifdef _MSC_VER
#pragma comment(lib, "gdiplus.lib")
#endif

#include <atomic>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

namespace {

constexpr COLORREF kBackground = RGB(13, 6, 24);
constexpr COLORREF kPanel = RGB(25, 12, 44);
constexpr COLORREF kPanelAlt = RGB(34, 17, 58);
constexpr COLORREF kPurple = RGB(177, 79, 255);
constexpr COLORREF kPurpleDeep = RGB(104, 41, 176);
constexpr COLORREF kWhite = RGB(247, 244, 255);
constexpr COLORREF kMuted = RGB(154, 139, 184);

constexpr int kIdPlace = 101;
constexpr int kIdPort = 102;
constexpr int kIdRetries = 103;
constexpr int kIdDelay = 104;
constexpr int kIdScript = 105;
constexpr int kIdToggle = 201;
constexpr int kIdStart = 202;
constexpr int kIdCopy = 203;
constexpr int kTimerStatus = 1;

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
std::thread g_serverThread;
std::thread g_workerThread;

std::mutex g_jobMutex;
std::condition_variable g_jobCv;
std::queue<std::wstring> g_jobs;

std::mutex g_statusMutex;
std::wstring g_status = L"idle";

HWND g_window = nullptr;
HFONT g_uiFont = nullptr;
HFONT g_monoFont = nullptr;
HBRUSH g_panelBrush = nullptr;
HBRUSH kaltBrush = nullptr;
HBRUSH g_bgBrush = nullptr;
Gdiplus::Image* g_background = nullptr;
ULONG_PTR g_gdiplusToken = 0;

void LoadBackground() {
    wchar_t modulePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(nullptr, modulePath, MAX_PATH)) return;
    std::wstring path(modulePath);
    size_t slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) return;
    path.resize(slash + 1);
    path += L"assets\\login.jpg";

    Gdiplus::Image* image = new Gdiplus::Image(path.c_str());
    if (image->GetLastStatus() == Gdiplus::Ok) {
        g_background = image;
    } else {
        delete image;
    }
}

std::wstring Trim(const std::wstring& text) {
    const wchar_t* start = text.c_str();
    while (*start == L' ' || *start == L'\t' || *start == L'\r' || *start == L'\n') ++start;
    const wchar_t* end = start + std::wcslen(start);
    while (end > start && (end[-1] == L' ' || end[-1] == L'\t' || end[-1] == L'\r' || end[-1] == L'\n')) --end;
    return std::wstring(start, end);
}

int ParseBoundedInt(const std::wstring& text, int fallback, int low, int high) {
    std::wstring clean = Trim(text);
    if (clean.empty()) return fallback;
    wchar_t* tail = nullptr;
    long value = std::wcstol(clean.c_str(), &tail, 10);
    if (tail == clean.c_str() || *tail != L'\0') return fallback;
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

local seen = {}
local cacheCount = 0

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

local function scan(value)
    if isJobId(value) then
        handle(value)
        return
    end
    if type(value) == "table" then
        for key, item in pairs(value) do
            if isJobId(item) then handle(item) end
        end
    end
end

pcall(function()
    if hookfunction then
        local originalTeleport = hookfunction(TS.TeleportToPlaceInstance, function(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
            if jobId then handle(tostring(jobId)) end
            return originalTeleport(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
        end)
        if TS.TeleportToPlaceInstanceAsync then
            local originalAsync = hookfunction(TS.TeleportToPlaceInstanceAsync, function(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
                if jobId then handle(tostring(jobId)) end
                return originalAsync(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
            end)
        end
    else
        local originalTeleport = TS.TeleportToPlaceInstance
        TS.TeleportToPlaceInstance = function(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
            if jobId then handle(tostring(jobId)) end
            return originalTeleport(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
        end
        if TS.TeleportToPlaceInstanceAsync then
            local originalAsync = TS.TeleportToPlaceInstanceAsync
            TS.TeleportToPlaceInstanceAsync = function(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
                if jobId then handle(tostring(jobId)) end
                return originalAsync(self, placeId, jobId, player, spawnName, teleportData, customData, extra)
            end
        end
    end
end)

pcall(function()
    if hookmetamethod and getrawmetatable then
        local mt = getrawmetatable(game)
        if mt then
            if setreadonly then setreadonly(mt, false) end
            local originalNamecall = hookmetamethod(game, "__namecall", function(self, a, b, c, d, e, f, g, h, i, j, k, l)
                scan(a); scan(b); scan(c); scan(d); scan(e); scan(f); scan(g); scan(h); scan(i); scan(j); scan(k); scan(l)
                return originalNamecall(self, a, b, c, d, e, f, g, h, i, j, k, l)
            end)
            if setreadonly then setreadonly(mt, true) end
        end
    end
end)
)lua";

    ReplaceAll(script, L"__PLACE__", cfg.placeId);
    ReplaceAll(script, L"__PORT__", std::to_wstring(cfg.port));
    ReplaceAll(script, L"__RETRIES__", std::to_wstring(cfg.retries));
    ReplaceAll(script, L"__DELAY__", std::to_wstring(static_cast<double>(cfg.delayMs) / 1000.0).substr(0, 6));
    return script;
}

void RefreshScript() {
    HWND edit = GetDlgItem(g_window, kIdScript);
    if (!edit) return;
    Config cfg;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg = g_config;
    }
    SetWindowTextW(edit, BuildScript(cfg).c_str());
}

void ReadConfigFromControls() {
    HWND place = GetDlgItem(g_window, kIdPlace);
    HWND port = GetDlgItem(g_window, kIdPort);
    HWND retries = GetDlgItem(g_window, kIdRetries);
    HWND delay = GetDlgItem(g_window, kIdDelay);
    if (!place || !port || !retries || !delay) return;

    wchar_t buffer[128] = {};
    Config cfg;
    GetWindowTextW(place, buffer, 128);
    cfg.placeId = Trim(buffer);
    if (cfg.placeId.empty()) cfg.placeId = L"109983668079237";

    GetWindowTextW(port, buffer, 128);
    cfg.port = ParseBoundedInt(buffer, 6969, 1, 65535);
    GetWindowTextW(retries, buffer, 128);
    cfg.retries = ParseBoundedInt(buffer, 10, 1, 1000);
    GetWindowTextW(delay, buffer, 128);
    cfg.delayMs = ParseBoundedInt(buffer, 1500, 50, 60000);

    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        cfg.enabled = g_config.enabled;
        g_config = cfg;
    }
    RefreshScript();
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
    ReadConfigFromControls();
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

void CopyScriptToClipboard() {
    HWND edit = GetDlgItem(g_window, kIdScript);
    if (!edit) return;
    int length = GetWindowTextLengthW(edit);
    if (length <= 0) return;
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(edit, &text[0], static_cast<int>(text.size()));
    text.resize(static_cast<size_t>(length));

    if (!OpenClipboard(g_window)) return;
    EmptyClipboard();
    HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (memory) {
        wchar_t* target = static_cast<wchar_t*>(GlobalLock(memory));
        std::memcpy(target, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(memory);
        SetClipboardData(CF_UNICODETEXT, memory);
    }
    CloseClipboard();
    SetStatus(L"lua script copied to clipboard");
}

void DrawRounded(HDC hdc, int left, int top, int right, int bottom, COLORREF fill, COLORREF edge) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, edge);
    HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, brush));
    HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, pen));
    RoundRect(hdc, left, top, right, bottom, 10, 10);
    SelectObject(hdc, oldBrush);
    SelectObject(hdc, oldPen);
    DeleteObject(brush);
    DeleteObject(pen);
}

void PaintWindow(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    auto label = [&](const wchar_t* text, long left, long top, long right, long bottom, UINT flags) {
        RECT box{left, top, right, bottom};
        DrawTextW(hdc, text, -1, &box, flags);
    };

    RECT rect;
    GetClientRect(hwnd, &rect);
    {
        Gdiplus::Graphics graphics(hdc);
        if (g_background) {
            graphics.DrawImage(g_background, 0, 0, rect.right, rect.bottom);
            Gdiplus::SolidBrush veil(Gdiplus::Color(214, 13, 6, 24));
            graphics.FillRectangle(&veil, 0, 0, rect.right, rect.bottom);
        } else {
            Gdiplus::SolidBrush base(Gdiplus::Color(255, 13, 6, 24));
            graphics.FillRectangle(&base, 0, 0, rect.right, rect.bottom);
        }
    }

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kPurple);
    SelectObject(hdc, g_uiFont);
    label(L"skyro.wtf", 24, 18, 320, 58, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, kWhite);
    label(L"DEEP LINK SERVER JOINER", 24, 54, 420, 78, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, kMuted);
    label(L"APPLICATION", 28, 100, 250, 120, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, kWhite);
    label(L"Roblox Server Launcher", 28, 122, 360, 146, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(hdc, kMuted);
    label(L"PLACE ID", 28, 160, 150, 182, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"PORT", 240, 160, 340, 182, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"RETRIES", 410, 160, 520, 182, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    label(L"DELAY (MS)", 570, 160, 720, 182, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawRounded(hdc, 26, 226, 724, 250, kPanel, kPurpleDeep);
    SetTextColor(hdc, kMuted);
    label(L"INTERCEPTOR", 40, 228, 220, 248, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    SetTextColor(hdc, kWhite);
    label(L"AUTO EXECUTE SCRIPT", 28, 352, 400, 374, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    DrawRounded(hdc, 26, 560, 724, 584, kPanel, kPurpleDeep);
    SetTextColor(hdc, kMuted);
    std::wstring status = L"STATUS: " + GetStatus();
    label(status.c_str(), 40, 562, 710, 582, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    EndPaint(hwnd, &ps);
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
        case WM_CREATE: {
            g_window = hwnd;
            g_uiFont = CreateFontW(-18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
            g_monoFont = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                     DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                     DEFAULT_QUALITY, DEFAULT_PITCH, L"Consolas");
            g_panelBrush = CreateSolidBrush(kPanel);
            kaltBrush = CreateSolidBrush(kPanelAlt);
            g_bgBrush = CreateSolidBrush(kBackground);

            const int editStyle = WS_CHILD | WS_VISIBLE | WS_BORDER | ES_AUTOHSCROLL;
            CreateWindowExW(0, L"EDIT", L"109983668079237", editStyle, 28, 184, 190, 26, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPlace)), nullptr, nullptr);
            CreateWindowExW(0, L"EDIT", L"6969", editStyle, 240, 184, 140, 26, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdPort)), nullptr, nullptr);
            CreateWindowExW(0, L"EDIT", L"10", editStyle, 410, 184, 130, 26, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdRetries)), nullptr, nullptr);
            CreateWindowExW(0, L"EDIT", L"1500", editStyle, 570, 184, 150, 26, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDelay)), nullptr, nullptr);

            CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                            600, 225, 110, 26, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdToggle)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"START SERVER", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                            28, 306, 220, 40, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdStart)), nullptr, nullptr);
            CreateWindowExW(0, L"BUTTON", L"COPY SCRIPT", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | WS_TABSTOP,
                            500, 306, 220, 40, hwnd,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCopy)), nullptr, nullptr);

            HWND script = CreateWindowExW(0, L"EDIT", L"",
                                          WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL | ES_MULTILINE |
                                          ES_READONLY | ES_AUTOVSCROLL,
                                          28, 378, 696, 168, hwnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdScript)), nullptr, nullptr);
            (void)script;

            SetTimer(hwnd, kTimerStatus, 200, nullptr);
            RefreshScript();
            return 0;
        }

        case WM_PAINT:
            PaintWindow(hwnd);
            return 0;

        case WM_TIMER: {
            if (wparam == kTimerStatus) {
                RECT zone{0, 556, 750, 590};
                InvalidateRect(hwnd, &zone, FALSE);
            }
            return 0;
        }

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLOREDIT: {
            HDC hdc = reinterpret_cast<HDC>(wparam);
            SetTextColor(hdc, kWhite);
            SetBkColor(hdc, kPanel);
            HWND child = reinterpret_cast<HWND>(lparam);
            if (GetDlgCtrlID(child) == kIdScript) SelectObject(hdc, g_monoFont);
            return reinterpret_cast<LRESULT>(g_panelBrush);
        }

        case WM_DRAWITEM: {
            DRAWITEMSTRUCT* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam);
            HDC hdc = item->hDC;
            RECT box = item->rcItem;
            bool hover = (item->itemState & ODS_HOTLIGHT) != 0;
            if (item->CtlID == kIdToggle) {
                bool on = false;
                {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    on = g_config.enabled;
                }
                COLORREF track = on ? kPurple : kPanelAlt;
                COLORREF edge = on ? kPurple : kMuted;
                DrawRounded(hdc, box.left + 62, box.top + 6, box.left + 108, box.top + 22, track, edge);
                int knobX = on ? box.left + 88 : box.left + 65;
                HBRUSH knob = CreateSolidBrush(kWhite);
                SelectObject(hdc, knob);
                Ellipse(hdc, knobX, box.top + 7, knobX + 14, box.top + 21);
                DeleteObject(knob);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, kMuted);
                SelectObject(hdc, g_uiFont);
                RECT toggleLabel{box.left, box.top, box.left + 56, box.bottom};
                DrawTextW(hdc, on ? L"ON" : L"OFF", -1, &toggleLabel,
                          DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            } else {
                COLORREF fill = hover ? kPurpleDeep : kPanelAlt;
                COLORREF edge = item->CtlID == kIdStart ? kPurple : kPurpleDeep;
                if (item->CtlID == kIdStart && g_serverRunning) fill = kPurple;
                DrawRounded(hdc, box.left, box.top, box.right, box.bottom, fill, edge);
                SetBkMode(hdc, TRANSPARENT);
                SetTextColor(hdc, kWhite);
                SelectObject(hdc, g_uiFont);
                wchar_t label[64] = {};
                GetWindowTextW(item->hwndItem, label, 64);
                if (item->CtlID == kIdStart && g_serverRunning) {
                    std::wcscpy(label, L"STOP SERVER");
                }
                DrawTextW(hdc, label, -1, &box, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }
            return 0;
        }

        case WM_COMMAND: {
            int id = LOWORD(wparam);
            int event = HIWORD(wparam);
            if (event == EN_CHANGE && id >= kIdPlace && id <= kIdDelay) {
                ReadConfigFromControls();
                return 0;
            }
            if (event != BN_CLICKED) return 0;
            if (id == kIdToggle) {
                {
                    std::lock_guard<std::mutex> lock(g_configMutex);
                    g_config.enabled = !g_config.enabled;
                }
                RECT toggleZone{26, 226, 724, 250};
                InvalidateRect(hwnd, &toggleZone, FALSE);
                SetStatus(g_config.enabled ? L"interceptor on" : L"interceptor off");
            } else if (id == kIdStart) {
                if (g_serverRunning) StopServer();
                else StartServer();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (id == kIdCopy) {
                CopyScriptToClipboard();
            }
            return 0;
        }

        case WM_CLOSE:
            StopServer();
            KillTimer(hwnd, kTimerStatus);
            DestroyWindow(hwnd);
            return 0;

        case WM_DESTROY:
            DeleteObject(g_uiFont);
            DeleteObject(g_monoFont);
            DeleteObject(g_panelBrush);
            DeleteObject(kaltBrush);
            DeleteObject(g_bgBrush);
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
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"SkyroWtfWindow";
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    if (!RegisterClassW(&wc)) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    HWND window = CreateWindowExW(0, L"SkyroWtfWindow", L"skyro.wtf - Deep Link Server Joiner",
                                  WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                  CW_USEDEFAULT, CW_USEDEFAULT, 760, 640,
                                  nullptr, nullptr, instance, nullptr);
    if (!window) {
        Gdiplus::GdiplusShutdown(g_gdiplusToken);
        return 1;
    }

    LoadBackground();

    ShowWindow(window, show);
    UpdateWindow(window);

    MSG msg = {};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    delete g_background;
    g_background = nullptr;
    Gdiplus::GdiplusShutdown(g_gdiplusToken);
    return 0;
}
