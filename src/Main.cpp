#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <objidl.h>
#include <gdiplus.h>
#include <uxtheme.h>
#include <vssym32.h>
#include "Core.h"
#include "Network.h"
#include <thread>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <memory>

using namespace rainregexp;
namespace {
enum Id { Url = 100, Connect, Browse, SearchText, Search, Input, SaveInput, WrapInput,
    Pattern, Parse, Copy, WrapPattern, Save, Load, Output, Tabs, Notes, Help, Link, Banner };
constexpr UINT Completed = WM_APP + 1;
constexpr DWORD EditStyle = WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL;
struct Control { HWND window; int x, y, width, height; int page; };
struct App {
    HINSTANCE instance{};
    HWND window{}, tooltip{};
    HFONT font{}, linkFont{};
    std::unique_ptr<Gdiplus::Bitmap> banner;
    IStream* bannerStream{};
    UINT dpi = 96;
    std::filesystem::path directory, browseDirectory;
    std::vector<Control> controls;
    bool wrapInput = false, wrapPattern = false, busy = false;
    int searchOffset = 0;
    std::thread worker;
    std::atomic<bool> closing{false};
    std::wstring completedText;
    int completedTarget = Output;
    ~App() {
        closing = true;
        if (worker.joinable()) worker.join();
        banner.reset();
        if (bannerStream) bannerStream->Release();
        if (font) DeleteObject(font);
        if (linkFont) DeleteObject(linkFont);
    }
    HWND Get(int id) const { return GetDlgItem(window, id); }
    int Scale(int value) const { return MulDiv(value, dpi, 96); }
    std::wstring Text(int id) const {
        HWND control = Get(id);
        const int length = GetWindowTextLengthW(control);
        std::wstring text(length + 1, L'\0');
        GetWindowTextW(control, text.data(), length + 1);
        text.resize(length);
        return text;
    }
    void Set(int id, const std::wstring& text) { SetWindowTextW(Get(id), text.c_str()); }
    HWND Add(const wchar_t* type, const wchar_t* text, DWORD style, int id,
        int x, int y, int width, int height, int page = -1, DWORD extended = 0) {
        if (page == 0 && _wcsicmp(type, L"STATIC") == 0 && id != Banner) extended |= WS_EX_TRANSPARENT;
        HWND control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            Scale(x), Scale(y), Scale(width), Scale(height), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (!control) throw std::runtime_error("Unable to create a window control.");
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (_wcsicmp(type, L"EDIT") == 0) {
            SendMessageW(control, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetWindowSubclass(control, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        controls.push_back({control, x, y, width, height, page});
        return control;
    }
    void Button(const wchar_t* title, int id, int x, int y, int width, int height, const wchar_t* tip) {
        HWND control = Add(L"BUTTON", title, WS_TABSTOP | BS_PUSHBUTTON, id, x, y, width, height);
        TOOLINFOW info{sizeof(info)};
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = window;
        info.uId = reinterpret_cast<UINT_PTR>(control);
        info.lpszText = const_cast<wchar_t*>(tip);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    void Group(const wchar_t* title, int x, int y, int width, int height) {
        Add(L"BUTTON", title, BS_GROUPBOX, 0, x, y, width, height);
    }
    void Edit(int id, const wchar_t* text, int x, int y, int width, int height, DWORD extra = 0, int page = -1) {
        Add(L"EDIT", text, EditStyle | extra, id, x, y, width, height, page, WS_EX_CLIENTEDGE);
    }
    std::wstring ResourceText(int id) const {
        auto resource = FindResourceW(instance, MAKEINTRESOURCEW(id), RT_RCDATA);
        auto handle = LoadResource(instance, resource);
        if (!resource || !handle) return {};
        return DecodeText(std::string(static_cast<const char*>(LockResource(handle)), SizeofResource(instance, resource)));
    }
    void Fonts() {
        auto previous = font;
        auto previousLink = linkFont;
        font = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        linkFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        for (const auto& control : controls)
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(GetDlgCtrlID(control.window) == Link ? linkFont : font), TRUE);
        if (previous) DeleteObject(previous);
        if (previousLink) DeleteObject(previousLink);
    }
    void Layout() {
        for (const auto& c : controls) MoveWindow(c.window, Scale(c.x), Scale(c.y), Scale(c.width), Scale(c.height), TRUE);
        InvalidateRect(window, nullptr, TRUE);
    }
    void Init() {
        dpi = GetDpiForWindow(window);
        Fonts();
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
        Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Url, 20, 11, 355, 24, -1, WS_EX_CLIENTEDGE);
        SendMessageW(Get(Url), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Enter web site URL or browse for HTML / text file"));
        Button(L"Connect", Connect, 381, 10, 67, 25, L"Download source using RainRegExp.ini settings");
        Button(L"Browse", Browse, 454, 10, 67, 25, L"Browse for a local HTML or text file");
        Group(L"", 554, 2, 306, 38);
        Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, SearchText, 560, 13, 240, 21, -1, WS_EX_CLIENTEDGE);
        Button(L"Search", Search, 807, 13, 43, 20, L"Find the next occurrence in the HTML (case insensitive)");
        Group(L"HTML to Parse", 19, 39, 843, 214);
        Edit(Input, L"", 26, 74, 827, 169, ES_AUTOHSCROLL | WS_HSCROLL);
        Button(L"Save", SaveInput, 764, 52, 36, 20, L"Save the HTML to disk");
        Button(L"Wrap", WrapInput, 813, 52, 38, 20, L"Toggle HTML word wrapping");
        Group(L"Your RegExp", 17, 261, 842, 117);
        Edit(Pattern, L"(?siU)", 74, 297, 775, 72, ES_AUTOHSCROLL | WS_HSCROLL);
        Button(L"Parse", Parse, 24, 297, 44, 25, L"Test with Rainmeter WebParser's PCRE engine");
        Button(L"Copy", Copy, 24, 344, 44, 25, L"Copy the expression to the clipboard");
        Button(L"Wrap", WrapPattern, 811, 274, 38, 20, L"Toggle expression word wrapping");
        Button(L"Save", Save, 721, 274, 36, 20, L"Save URL, expression and notes as a session");
        Button(L"Load", Load, 762, 274, 36, 20, L"Load a saved session");
        Group(L"Output - (StringIndex => Value)", 15, 384, 419, 276);
        Edit(Output, L"", 22, 414, 400, 239, ES_READONLY);
        Add(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS, Tabs, 447, 391, 408, 263);
        for (auto title : {L"Welcome", L"Notepad", L" RegExp  "}) {
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(title);
            TabCtrl_InsertItem(Get(Tabs), TabCtrl_GetItemCount(Get(Tabs)), &item);
        }
        Add(L"STATIC", L"", SS_OWNERDRAW, Banner, 461, 430, 246, 49, 0);
        auto pngResource = FindResourceW(instance, MAKEINTRESOURCEW(5), RT_RCDATA);
        auto pngHandle = LoadResource(instance, pngResource);
        bannerStream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(pngHandle)), SizeofResource(instance, pngResource));
        if (!bannerStream) throw std::runtime_error("Unable to load the embedded banner.");
        banner = std::make_unique<Gdiplus::Bitmap>(bannerStream, FALSE);
        if (banner->GetLastStatus() != Gdiplus::Ok) throw std::runtime_error("Unable to decode the embedded banner PNG.");
        Add(L"STATIC", L"Welcome to RainRegExp", 0, 0, 461, 490, 300, 19, 0);
        Add(L"STATIC", L"Create and test your RegExp options", 0, 0, 461, 515, 360, 19, 0);
        Add(L"STATIC", L"Click Notepad for general help", 0, 0, 461, 533, 360, 19, 0);
        Add(L"STATIC", L"Click RegExp for some regular expression hints", 0, 0, 461, 551, 375, 19, 0);
        Add(L"STATIC", L"RainRegExp 5.0", SS_RIGHT, 0, 705, 576, 137, 19, 0);
        Add(L"STATIC", L"Copyright © 2026 Jeffrey Morley", SS_RIGHT, 0, 600, 595, 242, 19, 0);
        Add(L"STATIC", L"CC BY-NC-SA 3.0", SS_RIGHT, 0, 650, 610, 192, 19, 0);
        Add(L"STATIC", L"https://rainmeter.net", SS_NOTIFY | SS_RIGHT, Link, 700, 628, 142, 19, 0);
        Edit(Notes, L"", 459, 425, 382, 217, 0, 1);
        Edit(Help, L"", 459, 425, 382, 217, ES_READONLY, 2);
        Set(Notes, ResourceText(3));
        Set(Help, L"Rainmeter WebParser compatibility\r\n\r\nPCRE 8.45 (16-bit), Rainmeter's exact source and configuration. "
            L"Your expression runs once, as entered. Output shows captured groups at StringIndex 1–99. "
            L"Optional captures may be empty.\r\n\r\n" + ResourceText(4));
        Fonts(); Layout(); SelectPage();
        wchar_t documents[MAX_PATH]{};
        if (std::filesystem::exists(directory / L"Rainmeter.ini")) browseDirectory = directory;
        else {
            SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents);
            browseDirectory = std::filesystem::path(documents) / L"Rainmeter" / L"RainRegExp";
            wchar_t saved[32768]{}; DWORD size = sizeof(saved);
            if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Rainmeter\\RainRegExp", L"CurrentBrowseDir",
                RRF_RT_REG_SZ, nullptr, saved, &size) == ERROR_SUCCESS && std::filesystem::is_directory(saved)) browseDirectory = saved;
            std::error_code error;
            std::filesystem::create_directories(browseDirectory, error);
        }
        SetFocus(Get(Url));
    }
    void SelectPage() {
        int page = TabCtrl_GetCurSel(Get(Tabs));
        for (const auto& c : controls) if (c.page >= 0) ShowWindow(c.window, c.page == page ? SW_SHOW : SW_HIDE);
        InvalidateRect(window, nullptr, TRUE);
    }
    void TabBackground(HWND control, HDC dc) const {
        RECT pane{}; GetClientRect(Get(Tabs), &pane);
        pane.top += Scale(24);
        MapWindowPoints(Get(Tabs), control, reinterpret_cast<POINT*>(&pane), 2);
        if (auto theme = OpenThemeData(Get(Tabs), L"TAB")) {
            DrawThemeBackground(theme, dc, TABP_PANE, 0, &pane, nullptr);
            CloseThemeData(theme);
        } else {
            RECT rect{}; GetClientRect(control, &rect); FillRect(dc, &rect, GetSysColorBrush(COLOR_BTNFACE));
        }
    }
    std::filesystem::path FileDialog(bool save, bool session) {
        wchar_t file[32768]{};
        OPENFILENAMEW dialog{sizeof(dialog)};
        dialog.hwndOwner = window; dialog.lpstrFile = file; dialog.nMaxFile = 32768;
        dialog.lpstrInitialDir = browseDirectory.c_str();
        dialog.lpstrFilter = session ? L"RainRegExp sessions (*.txt;*.ini)\0*.txt;*.ini\0All files (*.*)\0*.*\0" : L"HTML and text files\0*.html;*.htm;*.txt;*.xml;*.json\0All files (*.*)\0*.*\0";
        dialog.lpstrDefExt = L"txt";
        dialog.lpstrTitle = session ? (save ? L"Save RainRegExp session" : L"Open saved RegExp") : (save ? L"Save HTML or text file" : L"Open HTML or text file");
        dialog.Flags = OFN_EXPLORER | OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
        if (!(save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))) {
            if (CommDlgExtendedError()) throw std::runtime_error("Unable to open the file dialog.");
            return {};
        }
        std::filesystem::path path(file);
        browseDirectory = path.parent_path();
        const auto dir = browseDirectory.wstring();
        if (!std::filesystem::exists(directory / L"Rainmeter.ini"))
            RegSetKeyValueW(HKEY_CURRENT_USER, L"Software\\Rainmeter\\RainRegExp", L"CurrentBrowseDir", REG_SZ,
                dir.c_str(), static_cast<DWORD>((dir.size() + 1) * sizeof(wchar_t)));
        return path;
    }
    void Wrap(int id, bool& wrapped) {
        const auto text = Text(id);
        DWORD start = 0, end = 0;
        SendMessageW(Get(id), EM_GETSEL, reinterpret_cast<WPARAM>(&start), reinterpret_cast<LPARAM>(&end));
        for (auto& control : controls) if (control.window == Get(id)) {
            auto saved = control;
            DestroyWindow(control.window);
            wrapped = !wrapped;
            control.window = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | EditStyle |
                (wrapped ? 0 : ES_AUTOHSCROLL | WS_HSCROLL), Scale(saved.x), Scale(saved.y), Scale(saved.width), Scale(saved.height),
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
            SendMessageW(control.window, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetWindowSubclass(control.window, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
            SendMessageW(control.window, EM_SETSEL, start, end);
            SetFocus(control.window);
            SendMessageW(control.window, EM_SCROLLCARET, 0, 0);
            break;
        }
    }
    void Find() {
        const auto needle = Text(SearchText), haystack = Text(Input);
        if (needle.empty() || haystack.empty()) return;
        if (searchOffset > static_cast<int>(haystack.size())) searchOffset = 0;
        int found = -1;
        for (int i = searchOffset; static_cast<size_t>(i) + needle.size() <= haystack.size(); ++i) {
            if (CompareStringOrdinal(haystack.data() + i, static_cast<int>(needle.size()), needle.data(), static_cast<int>(needle.size()), TRUE) == CSTR_EQUAL) { found = i; break; }
        }
        if (found < 0) { searchOffset = 0; MessageBeep(MB_OK); return; }
        SetFocus(Get(Input));
        SendMessageW(Get(Input), EM_SETSEL, found, found + needle.size());
        SendMessageW(Get(Input), EM_SCROLLCARET, 0, 0);
        searchOffset = found + static_cast<int>(needle.size());
    }
    void Clipboard() {
        const auto text = Text(Pattern);
        if (!OpenClipboard(window)) throw std::runtime_error("Unable to open the clipboard.");
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
        if (!data) { CloseClipboard(); throw std::bad_alloc(); }
        void* buffer = GlobalLock(data);
        if (!buffer) { GlobalFree(data); CloseClipboard(); throw std::bad_alloc(); }
        memcpy(buffer, text.c_str(), (text.size() + 1) * sizeof(wchar_t)); GlobalUnlock(data);
        EmptyClipboard();
        if (!SetClipboardData(CF_UNICODETEXT, data)) { GlobalFree(data); CloseClipboard(); throw std::runtime_error("Unable to copy the expression."); }
        CloseClipboard();
        SetWindowTextW(Get(Copy), L"Copied"); SetTimer(window, 1, 2000, nullptr);
    }
    void Busy(bool value) {
        busy = value;
        for (int id : {Connect, Parse, Browse, Load}) EnableWindow(Get(id), !value);
    }
    void Start(int target, std::function<std::wstring()> task) {
        if (busy) return;
        if (worker.joinable()) worker.join();
        Busy(true); completedTarget = target;
        try {
            worker = std::thread([this, task = std::move(task)] {
                try { completedText = task(); }
                catch (const std::exception& error) { completedText = DecodeText(error.what()); }
                if (!closing) PostMessageW(window, Completed, 0, 0);
            });
        } catch (...) { Busy(false); throw; }
    }
    void Command(int id) {
        switch (id) {
        case Connect: {
            if (busy) break;
            const auto options = ReadNetworkOptions(directory / L"RainRegExp.ini");
            const auto url = Text(Url);
            Set(Input, L"Connecting...");
            Start(Input, [this, url, options] {
                try { return Fetch(url, options, closing); }
                catch (const std::exception& error) { return L"Connection Error!\r\nUnable to connect to web site: (" + url + L")\r\n\r\n" + DecodeText(error.what()); }
            });
            break;
        }
        case Browse: {
            const auto path = FileDialog(false, false);
            if (!path.empty()) { const auto text = ReadText(path); Set(Input, text); Set(Url, path.wstring()); }
            break;
        }
        case SaveInput: { const auto path = FileDialog(true, false); if (!path.empty()) WriteText(path, Text(Input)); break; }
        case Save: { const auto path = FileDialog(true, true); if (!path.empty()) SaveSession(path, {Text(Url), Text(Pattern), Text(Notes)}); break; }
        case Load: {
            const auto path = FileDialog(false, true);
            if (!path.empty()) { auto s = LoadSession(path); Set(Url, s.url); Set(Pattern, s.pattern); Set(Notes, s.notes); Set(Output, L""); }
            break;
        }
        case Parse: {
            if (busy) break;
            const auto input = Text(Input), pattern = Text(Pattern);
            Set(Output, L"Parsing...");
            Start(Output, [input, pattern] {
                const auto result = Match(input, pattern);
                if (!result.error.empty()) return result.error;
                std::wstring output;
                for (size_t i = 1; i < result.captures.size(); ++i) output += std::to_wstring(i) + L" => " + result.captures[i] + L"\r\n";
                return output;
            });
            break;
        }
        case Copy: Clipboard(); break;
        case Search: Find(); break;
        case WrapInput: Wrap(Input, wrapInput); break;
        case WrapPattern: Wrap(Pattern, wrapPattern); break;
        case Link: ShellExecuteW(window, L"open", L"https://rainmeter.net", nullptr, nullptr, SW_SHOWNORMAL); break;
        }
    }
    static LRESULT CALLBACK EditProc(HWND edit, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto app = reinterpret_cast<App*>(data);
        if (message == WM_KEYDOWN && wParam == 'A' && GetKeyState(VK_CONTROL) < 0) { SendMessageW(edit, EM_SETSEL, 0, -1); return 0; }
        if (message == WM_KEYDOWN && wParam == VK_RETURN && GetDlgCtrlID(edit) == SearchText) { app->Find(); return 0; }
        if (message == WM_CHAR && (wParam == 1 || (wParam == L'\r' && GetDlgCtrlID(edit) == SearchText))) return 0;
        return DefSubclassProc(edit, message, wParam, lParam);
    }
};

LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    App* app = reinterpret_cast<App*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        app = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        app->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app));
    }
    if (!app) return DefWindowProcW(window, message, wParam, lParam);
    try {
        switch(message) {
        case WM_CREATE: app->Init(); return 0;
        case WM_COMMAND:
            if (HIWORD(wParam) == EN_CHANGE && (LOWORD(wParam) == SearchText || LOWORD(wParam) == Input)) app->searchOffset = 0;
            if (HIWORD(wParam) == BN_CLICKED) app->Command(LOWORD(wParam));
            return 0;
        case WM_NOTIFY:
            if (reinterpret_cast<NMHDR*>(lParam)->idFrom == Tabs && reinterpret_cast<NMHDR*>(lParam)->code == TCN_SELCHANGE) app->SelectPage();
            return 0;
        case Completed:
            if (app->worker.joinable()) app->worker.join();
            app->Set(app->completedTarget, app->completedText);
            app->Busy(false); return 0;
        case WM_TIMER: SetWindowTextW(app->Get(Copy), L"Copy"); KillTimer(window, 1); return 0;
        case WM_CTLCOLORSTATIC:
            for (const auto& control : app->controls) {
                if (control.window == reinterpret_cast<HWND>(lParam) && control.page == 0) {
                    app->TabBackground(control.window, reinterpret_cast<HDC>(wParam));
                    SetTextColor(reinterpret_cast<HDC>(wParam), GetSysColor(COLOR_WINDOWTEXT));
                    SetBkMode(reinterpret_cast<HDC>(wParam), TRANSPARENT);
                    return reinterpret_cast<LRESULT>(GetStockObject(HOLLOW_BRUSH));
                }
            }
            if (reinterpret_cast<HWND>(lParam) == app->Get(Help) || reinterpret_cast<HWND>(lParam) == app->Get(Output)) {
                SetTextColor(reinterpret_cast<HDC>(wParam), RGB(0, 0, 0));
                SetBkColor(reinterpret_cast<HDC>(wParam), RGB(255, 255, 255));
                return reinterpret_cast<LRESULT>(GetStockObject(WHITE_BRUSH));
            }
            break;
        case WM_DRAWITEM: {
            const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == Banner) {
                app->TabBackground(item->hwndItem, item->hDC);
                Gdiplus::Graphics graphics(item->hDC);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(app->banner.get(), 0, 0, item->rcItem.right, item->rcItem.bottom);
                return TRUE;
            }
            break;
        }
        case WM_SETCURSOR:
            if (reinterpret_cast<HWND>(wParam) == app->Get(Link)) { SetCursor(LoadCursorW(nullptr, IDC_HAND)); return TRUE; }
            break;
        case WM_DPICHANGED: {
            app->dpi = HIWORD(wParam); app->Fonts();
            auto rect = reinterpret_cast<RECT*>(lParam);
            SetWindowPos(window, nullptr, rect->left, rect->top, rect->right - rect->left, rect->bottom - rect->top, SWP_NOZORDER | SWP_NOACTIVATE);
            app->Layout(); return 0;
        }
        case WM_CLOSE: app->closing = true; DestroyWindow(window); return 0;
        case WM_DESTROY: PostQuitMessage(0); return 0;
        }
    } catch (const std::exception& error) {
        MessageBoxW(window, DecodeText(error.what()).c_str(), L"RainRegExp", MB_OK | MB_ICONERROR);
        if (message == WM_CREATE) return -1;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int show) {
    struct GdiRuntime {
        ULONG_PTR token{};
        GdiRuntime() { Gdiplus::GdiplusStartupInput options; Gdiplus::GdiplusStartup(&token, &options, nullptr); }
        ~GdiRuntime() { if (token) Gdiplus::GdiplusShutdown(token); }
    } graphicsRuntime;
    HANDLE singleton = CreateMutexW(nullptr, TRUE, L"Local\\RainRegExp.Cpp");
    if (singleton && GetLastError() == ERROR_ALREADY_EXISTS) {
        if (auto existing = FindWindowW(L"RainRegExp.Cpp.Window", nullptr)) { ShowWindow(existing, SW_RESTORE); SetForegroundWindow(existing); }
        CloseHandle(singleton); return 0;
    }
    INITCOMMONCONTROLSEX common{sizeof(common), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES}; InitCommonControlsEx(&common);
    App app; app.instance = instance;
    wchar_t path[32768]{}; GetModuleFileNameW(nullptr, path, 32768);
    app.directory = std::filesystem::path(path).parent_path();
    WNDCLASSEXW type{sizeof(type)};
    type.lpfnWndProc = WindowProc; type.hInstance = instance; type.lpszClassName = L"RainRegExp.Cpp.Window";
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
    type.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    RegisterClassExW(&type);
    UINT dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(878, dpi, 96), MulDiv(672, dpi, 96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HWND window = CreateWindowExW(0, type.lpszClassName, L"RainRegExp", style,
        (GetSystemMetrics(SM_CXSCREEN) - width) / 2, (GetSystemMetrics(SM_CYSCREEN) - height) / 2,
        width, height, nullptr, nullptr, instance, &app);
    if (!window) { if (singleton) CloseHandle(singleton); return 1; }
    ShowWindow(window, show); UpdateWindow(window);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        // Dialog navigation without treating Escape as a request to close.
        if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) continue;
        if (!IsDialogMessageW(window, &message)) { TranslateMessage(&message); DispatchMessageW(&message); }
    }
    if (singleton) CloseHandle(singleton);
    return static_cast<int>(message.wParam);
}
