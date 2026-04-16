#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <initguid.h>
#include <string>
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <shellapi.h>
#include "WebView2.h"

// ID del icono
#define ID_ICON 1

// Definir IIDs manualmente para evitar errores de enlace
DEFINE_GUID(IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 0x8B4F98CE, 0xDB0D, 0x4E71, 0x85, 0xFD, 0xC4, 0xC4, 0xEF, 0x1F, 0x26, 0x30);
DEFINE_GUID(IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler, 0x3448514C, 0x2353, 0x497D, 0x9A, 0xAA, 0x9D, 0xA0, 0x12, 0x2D, 0x6E, 0xEB);
DEFINE_GUID(IID_ICoreWebView2WebMessageReceivedEventHandler, 0x57213F19, 0x00E6, 0x4973, 0xA9, 0x51, 0x50, 0x04, 0xF9, 0x71, 0x74, 0x47);

// Helper para extraer valores simples de JSON (solo para prototipo)
std::wstring GetJsonValue(const std::wstring& json, const std::wstring& key) {
    size_t pos = json.find(L"\"" + key + L"\":");
    if (pos == std::wstring::npos) return L"";
    pos = json.find(L"\"", pos + key.length() + 3);
    if (pos == std::wstring::npos) return L"";
    size_t end = json.find(L"\"", pos + 1);
    if (end == std::wstring::npos) return L"";
    return json.substr(pos + 1, end - pos - 1);
}

class WebViewHandler : public ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler, 
                       public ICoreWebView2CreateCoreWebView2ControllerCompletedHandler,
                       public ICoreWebView2WebMessageReceivedEventHandler {
    LONG m_ref = 1;
    HWND m_hWnd;
    ICoreWebView2Controller* m_controller = nullptr;
    ICoreWebView2* m_webview = nullptr;

public:
    WebViewHandler(HWND hwnd) : m_hWnd(hwnd) {}

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) {
        if (riid == IID_IUnknown || riid == IID_ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler*>(this);
        } else if (riid == IID_ICoreWebView2CreateCoreWebView2ControllerCompletedHandler) {
            *ppv = static_cast<ICoreWebView2CreateCoreWebView2ControllerCompletedHandler*>(this);
        } else if (riid == IID_ICoreWebView2WebMessageReceivedEventHandler) {
            *ppv = static_cast<ICoreWebView2WebMessageReceivedEventHandler*>(this);
        } else {
            *ppv = nullptr; return E_NOINTERFACE;
        }
        AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() { if (InterlockedDecrement(&m_ref) == 0) { delete this; return 0; } return m_ref; }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Environment* env) override {
        return env->CreateCoreWebView2Controller(m_hWnd, this);
    }

    HRESULT STDMETHODCALLTYPE Invoke(HRESULT res, ICoreWebView2Controller* controller) override {
        m_controller = controller;
        m_controller->AddRef();
        m_controller->get_CoreWebView2(&m_webview);
        m_webview->AddRef();
        
        RECT bounds; GetClientRect(m_hWnd, &bounds);
        m_controller->put_Bounds(bounds);
        m_controller->put_IsVisible(TRUE);

        // Registrar el manejador de mensajes
        EventRegistrationToken token;
        m_webview->add_WebMessageReceived(this, &token);

        // Navegar a la URL de GitHub Pages del usuario
        m_webview->Navigate(L"https://pepecarlos546.github.io/vsstudiohtml/");
        
        SetWindowLongPtr(m_hWnd, GWLP_USERDATA, (LONG_PTR)m_controller);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2* webview, ICoreWebView2WebMessageReceivedEventArgs* args) override {
        LPWSTR messageRaw;
        args->get_WebMessageAsJson(&messageRaw);
        std::wstring message(messageRaw);
        CoTaskMemFree(messageRaw);

        std::wstring action = GetJsonValue(message, L"action");

        if (action == L"ls") {
            std::wstring path = GetJsonValue(message, L"path");
            if (path == L"." || path.empty()) path = L"*";
            else path += L"\\*";

            std::wstring response = L"{\"type\":\"ls\", \"data\":[";
            WIN32_FIND_DATAW fd;
            HANDLE hFind = FindFirstFileW(path.c_str(), &fd);
            bool first = true;
            if (hFind != INVALID_HANDLE_VALUE) {
                do {
                    if (!first) response += L",";
                    response += L"{\"name\":\"" + std::wstring(fd.cFileName) + L"\", \"type\":\"" + 
                                ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? L"dir" : L"file") + L"\"}";
                    first = false;
                } while (FindNextFileW(hFind, &fd));
                FindClose(hFind);
            }
            response += L"]}";
            webview->PostWebMessageAsJson(response.c_str());
        } 
        else if (action == L"read") {
            std::wstring path = GetJsonValue(message, L"path");
            std::ifstream file(path);
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string content = buffer.str();
            // Escapar contenido para JSON (muy básico)
            std::wstring wcontent;
            for (char c : content) {
                if (c == '\"') wcontent += L"\\\"";
                else if (c == '\\') wcontent += L"\\\\";
                else if (c == '\n') wcontent += L"\\n";
                else if (c == '\r') wcontent += L"\\r";
                else wcontent += (wchar_t)c;
            }
            std::wstring response = L"{\"type\":\"read\", \"path\":\"" + path + L"\", \"content\":\"" + wcontent + L"\"}";
            webview->PostWebMessageAsJson(response.c_str());
        }
        else if (action == L"exec") {
            std::wstring command = GetJsonValue(message, L"command");
            std::wstring fullCmd = L"cmd /c " + command + L" > out.tmp 2>&1";
            _wsystem(fullCmd.c_str());
            
            std::ifstream file("out.tmp");
            std::stringstream buffer;
            buffer << file.rdbuf();
            std::string output = buffer.str();
            std::wstring woutput;
            for (char c : output) {
                if (c == '\"') woutput += L"\\\"";
                else if (c == '\\') woutput += L"\\\\";
                else if (c == '\n') woutput += L"\\n";
                else woutput += (wchar_t)c;
            }
            std::wstring response = L"{\"type\":\"exec\", \"output\":\"" + woutput + L"\"}";
            webview->PostWebMessageAsJson(response.c_str());
        }
        else if (action == L"write") {
            std::wstring path = GetJsonValue(message, L"path");
            std::wstring content = GetJsonValue(message, L"content");
            // Convertir wstring content (UTF-16) a string (UTF-8/ANSI) para guardar
            std::string scontent(content.begin(), content.end());
            std::ofstream file(path);
            file << scontent;
            file.close();
            std::wstring response = L"{\"type\":\"info\", \"message\":\"Archivo guardado\"}";
            webview->PostWebMessageAsJson(response.c_str());
        }

        return S_OK;
    }
};

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_SIZE) {
        ICoreWebView2Controller* ctrl = (ICoreWebView2Controller*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
        if (ctrl) { RECT r; GetClientRect(hWnd, &r); ctrl->put_Bounds(r); }
    } else if (msg == WM_DESTROY) PostQuitMessage(0);
    return DefWindowProcW(hWnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hIcon = LoadIcon(hInst, MAKEINTRESOURCE(ID_ICON));
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.lpszClassName = L"CloudShellNativeClass";
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowW(L"CloudShellNativeClass", L"Google Cloud Shell - Native C++", 
                             WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 1024, 768, 
                             NULL, NULL, hInst, NULL);
    ShowWindow(hWnd, nShow);

    HMODULE hLoader = LoadLibraryW(L"WebView2Loader.dll");
    if (hLoader) {
        typedef HRESULT (STDAPICALLTYPE *CreateEnvPtr)(LPCWSTR, LPCWSTR, void*, void*);
        auto CreateEnv = (CreateEnvPtr)GetProcAddress(hLoader, "CreateCoreWebView2EnvironmentWithOptions");
        if (CreateEnv) {
            std::wstring dataPath = _wgetenv(L"LOCALAPPDATA");
            dataPath += L"\\CloudShellData";
            CreateEnv(nullptr, dataPath.c_str(), nullptr, new WebViewHandler(hWnd));
        }
    }

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    return 0;
}
