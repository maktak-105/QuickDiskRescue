#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dwmapi.h>
#include <unknwn.h>
#include <WebView2.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <string>
#include <atomic>
#include <thread>
#include <functional>
#include <cstdarg>
#include <cstdio>

#include "engine.h"

#define WM_POST_JSON (WM_APP + 1)
#define IDI_ICON1 101
#define IDR_INDEX_HTML 201
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

typedef HRESULT (STDAPICALLTYPE *CreateEnvFn)(
    PCWSTR, PCWSTR, ICoreWebView2EnvironmentOptions*,
    ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*);

class EnvCompletedHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler {
    std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> m_fn;
    std::atomic<ULONG> m_ref{1};
public:
    EnvCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Environment*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Environment* env) override {
        return m_fn(result, env);
    }
};

class ControllerCompletedHandler : public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler {
    std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> m_fn;
    std::atomic<ULONG> m_ref{1};
public:
    ControllerCompletedHandler(std::function<HRESULT(HRESULT, ICoreWebView2Controller*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, ICoreWebView2Controller* controller) override {
        return m_fn(result, controller);
    }
};

class WebMessageReceivedHandler : public ICoreWebView2WebMessageReceivedEventHandler {
    std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> m_fn;
    std::atomic<ULONG> m_ref{1};
public:
    WebMessageReceivedHandler(std::function<HRESULT(ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs*)> fn) : m_fn(fn) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_ref; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --m_ref;
        if (r == 0) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* sender, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        return m_fn(sender, args);
    }
};

HWND g_hWnd = NULL;
ICoreWebView2Controller* g_controller = NULL;
ICoreWebView2* g_webview = NULL;
volatile int g_stopFlag = 0;
std::atomic<bool> g_busy(false);
std::thread g_worker;
std::wstring g_openPath;
std::wstring g_logPath;
static FILE* g_log = nullptr;

static void LOG(const char* fmt, ...) {
    if (!g_log) {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, exePath, MAX_PATH);
        g_logPath = exePath;
        size_t slash = g_logPath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) g_logPath.resize(slash + 1);
        g_logPath += L"QuickDiskRescue_debug.log";
        char path[MAX_PATH * 3] = {0};
        WideCharToMultiByte(CP_UTF8, 0, g_logPath.c_str(), -1, path, sizeof(path), NULL, NULL);
        g_log = fopen(path, "w");
    }
    if (!g_log) return;
    va_list ap; va_start(ap, fmt); vfprintf(g_log, fmt, ap); va_end(ap);
    fprintf(g_log, "\n"); fflush(g_log);
}

void PostJson(const std::wstring& json) {
    if (g_hWnd) PostMessageW(g_hWnd, WM_POST_JSON, 0, (LPARAM)new std::wstring(json));
}

bool ExtractJsonString(const std::wstring& json, const std::wstring& key, std::wstring& out) {
    std::wstring needle = L"\"" + key + L"\"";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) return false;
    pos = json.find(L':', pos + needle.size());
    if (pos == std::wstring::npos) return false;
    pos = json.find_first_not_of(L" \t\r\n", pos + 1);
    if (pos == std::wstring::npos || json[pos] != L'"') return false;
    ++pos;
    std::wstring result;
    while (pos < json.size() && json[pos] != L'"') {
        wchar_t ch = json[pos];
        if (ch == L'\\' && pos + 1 < json.size()) {
            wchar_t next = json[pos + 1];
            switch (next) {
            case L'"': result += L'"'; pos += 2; break;
            case L'\\': result += L'\\'; pos += 2; break;
            case L'n': result += L'\n'; pos += 2; break;
            case L'r': result += L'\r'; pos += 2; break;
            case L't': result += L'\t'; pos += 2; break;
            default: result += next; pos += 2; break;
            }
        } else {
            result += ch;
            ++pos;
        }
    }
    out = result;
    return true;
}

bool ExtractJsonInt(const std::wstring& json, const std::wstring& key, int& out) {
    std::wstring needle = L"\"" + key + L"\"";
    size_t pos = json.find(needle);
    if (pos == std::wstring::npos) return false;
    pos = json.find(L':', pos + needle.size());
    if (pos == std::wstring::npos) return false;
    pos = json.find_first_not_of(L" \t\r\n", pos + 1);
    if (pos == std::wstring::npos) return false;
    out = _wtoi(json.c_str() + pos);
    return true;
}

std::wstring LoadEmbeddedIndexHtml() {
    HRSRC hRes = FindResourceW(NULL, MAKEINTRESOURCEW(IDR_INDEX_HTML), (LPCWSTR)RT_RCDATA);
    if (!hRes) return L"";
    HGLOBAL hData = LoadResource(NULL, hRes);
    if (!hData) return L"";
    DWORD size = SizeofResource(NULL, hRes);
    const char* data = static_cast<const char*>(LockResource(hData));
    if (!data || size == 0) return L"";
    int count = MultiByteToWideChar(CP_UTF8, 0, data, (int)size, NULL, 0);
    std::wstring wstr(count, 0);
    MultiByteToWideChar(CP_UTF8, 0, data, (int)size, &wstr[0], count);
    return wstr;
}

std::wstring BrowseFile(int save, int folder) {
    IFileDialog* dlg = nullptr;
    HRESULT hr = CoCreateInstance(save ? CLSID_FileSaveDialog : CLSID_FileOpenDialog, nullptr,
                                  CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg));
    if (FAILED(hr) || !dlg) return L"";
    DWORD opt = 0;
    dlg->GetOptions(&opt);
    if (folder) opt |= FOS_PICKFOLDERS;
    dlg->SetOptions(opt);
    std::wstring result;
    if (SUCCEEDED(dlg->Show(g_hWnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dlg->GetResult(&item)) && item) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p)) && p) {
                result = p;
                CoTaskMemFree(p);
            }
            item->Release();
        }
    }
    dlg->Release();
    return result;
}

void LaunchWork(std::function<void()> fn) {
    if (g_busy.exchange(true)) {
        PostJson(L"{\"type\":\"error\",\"message\":\"busy\"}");
        return;
    }
    g_stopFlag = 0;
    if (g_worker.joinable()) g_worker.detach();
    g_worker = std::thread([fn]() {
        fn();
        g_busy = false;
        PostJson(L"{\"type\":\"state\",\"busy\":false,\"readonly\":true}");
    });
}

void HandleMessage(const std::wstring& msg) {
    std::wstring type;
    if (!ExtractJsonString(msg, L"type", type)) return;

    if (type == L"ping") {
        PostJson(L"{\"type\":\"pong\"}");
        return;
    }
    if (type == L"quit") {
        PostMessageW(g_hWnd, WM_CLOSE, 0, 0);
        return;
    }
    if (type == L"cancel") {
        g_stopFlag = 1;
        return;
    }
    if (type == L"list_disks") {
        LaunchWork([]() {
            wchar_t* json = nullptr;
            qdr_list_disks_json(&json);
            if (json) {
                std::wstring o = L"{\"type\":\"disks\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"open") {
        ExtractJsonString(msg, L"path", g_openPath);
        PostJson(L"{\"type\":\"state\",\"busy\":false,\"readonly\":true}");
        return;
    }
    if (type == L"browse_open") {
        std::wstring p = BrowseFile(0, 0);
        if (!p.empty()) {
            g_openPath = p;
            std::wstring o = L"{\"type\":\"opened\",\"path\":\"";
            for (wchar_t c : p) {
                if (c == L'\\') o += L"\\\\";
                else if (c == L'"') o += L"\\\"";
                else o += c;
            }
            o += L"\"}";
            PostJson(o);
        }
        return;
    }
    if (type == L"browse_bek") {
        std::wstring p = BrowseFile(0, 0);
        if (!p.empty()) {
            std::wstring o = L"{\"type\":\"bek_path\",\"path\":\"";
            for (wchar_t c : p) {
                if (c == L'\\') o += L"\\\\";
                else if (c == L'"') o += L"\\\"";
                else o += c;
            }
            o += L"\"}";
            PostJson(o);
        }
        return;
    }
    if (type == L"browse_save") {
        std::wstring kind;
        ExtractJsonString(msg, L"kind", kind);
        std::wstring p = BrowseFile(kind == L"image", kind != L"image");
        if (!p.empty()) {
            std::wstring o = L"{\"type\":\"save_path\",\"kind\":\"" + kind + L"\",\"path\":\"";
            for (wchar_t c : p) {
                if (c == L'\\') o += L"\\\\";
                else if (c == L'"') o += L"\\\"";
                else o += c;
            }
            o += L"\"}";
            PostJson(o);
        }
        return;
    }

    std::wstring source, dest, path;
    ExtractJsonString(msg, L"source", source);
    ExtractJsonString(msg, L"dest", dest);
    ExtractJsonString(msg, L"path", path);
    int part = -1;
    ExtractJsonInt(msg, L"partition", part);
    if (source.empty()) source = g_openPath;

    if (type == L"diagnose" || type == L"deep_scan") {
        int deep = type == L"deep_scan";
        LaunchWork([source, deep]() {
            wchar_t* json = nullptr;
            qdr_diagnose_json(source.c_str(), deep, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"diagnose_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"tree") {
        LaunchWork([source, part, path]() {
            wchar_t* json = nullptr;
            auto cb = [](unsigned long long done, unsigned long long total, const wchar_t*, void*) {
                std::wstring o = L"{\"type\":\"progress\",\"done\":" + std::to_wstring(done) +
                                 L",\"total\":" + std::to_wstring(total) + L",\"message\":\"解析中\"}";
                PostJson(o);
            };
            qdr_tree_json(source.c_str(), part, path.c_str(), &g_stopFlag, cb, nullptr, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"tree_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"image") {
        LaunchWork([source, dest]() {
            wchar_t* json = nullptr;
            auto cb = [](unsigned long long done, unsigned long long total, const wchar_t*, void*) {
                std::wstring o = L"{\"type\":\"progress\",\"done\":" + std::to_wstring(done) +
                                 L",\"total\":" + std::to_wstring(total) + L",\"message\":\"イメージ作成中\"}";
                PostJson(o);
            };
            qdr_image(source.c_str(), dest.c_str(), &g_stopFlag, cb, nullptr, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"image_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"copy_out") {
        LaunchWork([source, part, path, dest]() {
            wchar_t* json = nullptr;
            auto cb = [](unsigned long long done, unsigned long long total, const wchar_t*, void*) {
                std::wstring o = L"{\"type\":\"progress\",\"done\":" + std::to_wstring(done) +
                                 L",\"total\":" + std::to_wstring(total) + L",\"message\":\"解析中\"}";
                PostJson(o);
            };
            qdr_copy_out(source.c_str(), part, path.c_str(), dest.c_str(), &g_stopFlag, cb, nullptr, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"copy_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"carve") {
        LaunchWork([source, part, dest]() {
            wchar_t* json = nullptr;
            qdr_carve(source.c_str(), part, dest.c_str(), &g_stopFlag, nullptr, nullptr, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"carve_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"unlock_bitlocker") {
        std::wstring volume, rp, bek, pw;
        ExtractJsonString(msg, L"volume", volume);
        ExtractJsonString(msg, L"recovery_password", rp);
        ExtractJsonString(msg, L"bek", bek);
        ExtractJsonString(msg, L"passphrase", pw);
        LaunchWork([volume, rp, bek, pw]() {
            wchar_t* json = nullptr;
            qdr_bitlocker_unlock(volume.c_str(), rp.c_str(), bek.c_str(), pw.c_str(), &json);
            if (json) {
                std::wstring o = L"{\"type\":\"unlock_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
    if (type == L"repair_gpt") {
        bool phys = source.find(L"PhysicalDrive") != std::wstring::npos;
        if (phys) {
            int r = MessageBoxW(g_hWnd,
                L"GPT ヘッダとパーティション表だけをディスクへ書き込みます。\n"
                L"ファイル内容は変更しません。先にイメージ保存することを推奨します。\n\n続行しますか？",
                L"GPT修復", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (r != IDYES) return;
            r = MessageBoxW(g_hWnd,
                L"最終確認: 実ディスクの GPT を修復します。よろしいですか？",
                L"GPT修復", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2);
            if (r != IDYES) return;
        }
        LaunchWork([source]() {
            wchar_t* json = nullptr;
            qdr_repair_gpt(source.c_str(), 1, &json);
            if (json) {
                std::wstring o = L"{\"type\":\"repair_gpt_result\",\"payload\":";
                o += json;
                o += L"}";
                PostJson(o);
                qdr_free_json(json);
            }
        });
        return;
    }
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_SIZE:
        if (g_controller) {
            RECT bounds;
            GetClientRect(hWnd, &bounds);
            g_controller->put_Bounds(bounds);
        }
        break;
    case WM_POST_JSON: {
        std::wstring* pJson = (std::wstring*)lParam;
        if (pJson) {
            if (g_webview) g_webview->PostWebMessageAsJson(pJson->c_str());
            delete pJson;
        }
        break;
    }
    case WM_DESTROY:
        g_stopFlag = 1;
        if (g_worker.joinable()) g_worker.detach();
        if (g_controller) { g_controller->Close(); g_controller->Release(); g_controller = NULL; }
        if (g_webview) { g_webview->Release(); g_webview = NULL; }
        PostQuitMessage(0);
        break;
    default:
        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static int EnsureAdministrator() {
    HANDLE token = NULL;
    TOKEN_ELEVATION elevation = {0};
    DWORD returned = 0;
    bool elevated = false;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
        elevated = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &returned)
                   && elevation.TokenIsElevated != 0;
        CloseHandle(token);
    }
    if (elevated) return 1;
    wchar_t exePath[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    SHELLEXECUTEINFOW executeInfo = {0};
    executeInfo.cbSize = sizeof(executeInfo);
    executeInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
    executeInfo.lpVerb = L"runas";
    executeInfo.lpFile = exePath;
    executeInfo.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&executeInfo)) return GetLastError() == ERROR_CANCELLED ? 0 : -1;
    if (executeInfo.hProcess) CloseHandle(executeInfo.hProcess);
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    char tm[8];
    if (GetEnvironmentVariableA("QUICKAPPSTEST", tm, sizeof(tm)) && tm[0] == '1') {
        qdr_set_test_mode(1);
    } else {
        int elevationResult = EnsureAdministrator();
        if (elevationResult != 1) return elevationResult == 0 ? 0 : 1;
    }

    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::wstring(argv[i]) == L"--open") g_openPath = argv[i + 1];
    }
    LocalFree(argv);

    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    OleInitialize(NULL);
    SetProcessDPIAware();

    WNDCLASSEXW wc = {0};
    wc.cbSize = sizeof(WNDCLASSEXW);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.lpszClassName = L"QuickDiskRescueNativeWebView2Class";
    wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    wc.hIconSm = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON1));
    if (!RegisterClassExW(&wc)) return 1;

    g_hWnd = CreateWindowExW(0, wc.lpszClassName, L"QuickDiskRescue — ディスク救出 [読取専用]",
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1280, 800,
                             NULL, NULL, hInstance, NULL);
    if (!g_hWnd) return 1;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(g_hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    ShowWindow(g_hWnd, nCmdShow);

    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    std::wstring appDir = exePath;
    size_t lastSlash = appDir.find_last_of(L"\\/");
    if (lastSlash != std::wstring::npos) appDir = appDir.substr(0, lastSlash);

    std::wstring htmlContent = LoadEmbeddedIndexHtml();
    wchar_t tempPath[MAX_PATH];
    GetTempPathW(MAX_PATH, tempPath);
    std::wstring userDataFolder = std::wstring(tempPath) + L"QuickDiskRescue_WVData";
    CreateDirectoryW(userDataFolder.c_str(), NULL);

    std::wstring loaderDll = appDir + L"\\WebView2Loader.dll";
    HMODULE hLoader = LoadLibraryW(loaderDll.c_str());
    if (!hLoader) hLoader = LoadLibraryW(L"C:\\tools\\webview2\\build\\native\\x64\\WebView2Loader.dll");
    if (!hLoader) {
        MessageBoxW(g_hWnd, L"WebView2Loader.dll が見つかりません。", L"QuickDiskRescue", MB_ICONERROR);
        return 1;
    }
    CreateEnvFn createEnv = (CreateEnvFn)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
    if (!createEnv) return 1;

    createEnv(nullptr, userDataFolder.c_str(), nullptr,
        new EnvCompletedHandler([htmlContent](HRESULT result, ICoreWebView2Environment* env) -> HRESULT {
            if (FAILED(result) || !env) {
                MessageBoxW(g_hWnd, L"WebView2 Runtime を初期化できませんでした。", L"QuickDiskRescue", MB_ICONERROR);
                return result;
            }
            env->CreateCoreWebView2Controller(g_hWnd,
                new ControllerCompletedHandler([htmlContent](HRESULT result, ICoreWebView2Controller* controller) -> HRESULT {
                    if (FAILED(result) || !controller) return result;
                    g_controller = controller;
                    g_controller->AddRef();
                    g_controller->get_CoreWebView2(&g_webview);
                    RECT bounds;
                    GetClientRect(g_hWnd, &bounds);
                    g_controller->put_Bounds(bounds);
                    g_controller->put_IsVisible(TRUE);
                    ICoreWebView2Settings* settings = NULL;
                    g_webview->get_Settings(&settings);
                    if (settings) {
                        settings->put_AreDefaultContextMenusEnabled(FALSE);
                        settings->put_IsStatusBarEnabled(FALSE);
                        settings->Release();
                    }
                    EventRegistrationToken token;
                    g_webview->add_WebMessageReceived(
                        new WebMessageReceivedHandler(
                            [](ICoreWebView2*, ICoreWebView2WebMessageReceivedEventArgs* args) -> HRESULT {
                                LPWSTR jsonRaw = NULL;
                                args->get_WebMessageAsJson(&jsonRaw);
                                std::wstring msg = jsonRaw ? jsonRaw : L"";
                                if (jsonRaw) CoTaskMemFree(jsonRaw);
                                HandleMessage(msg);
                                return S_OK;
                            }),
                        &token);
                    if (!htmlContent.empty()) g_webview->NavigateToString(htmlContent.c_str());
                    return S_OK;
                }));
            return S_OK;
        }));

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    OleUninitialize();
    CoUninitialize();
    if (g_log) fclose(g_log);
    return (int)msg.wParam;
}
