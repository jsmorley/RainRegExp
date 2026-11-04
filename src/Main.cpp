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
#include "ModernTheme.h"
#include <thread>
#include <atomic>
#include <functional>
#include <stdexcept>
#include <algorithm>
#include <memory>

using namespace rainregexp;
namespace {
enum Id { Url = 100, Connect, Browse, SearchText, Search, Input, SaveInput, WrapInput,
    Pattern, Parse, Copy, WrapPattern, Save, Load, Output, Tabs, Notes, Help, Link, Banner, Status, Engine, CaptureCount,
    HtmlHeading, PatternHeading, OutputHeading };
constexpr UINT Completed = WM_APP + 1;
constexpr DWORD EditStyle = WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL;
struct Control { HWND window; int x, y, width, height; int page; };
struct App {
    HINSTANCE instance{};
    HWND window{}, tooltip{};
    HFONT font{}, linkFont{}, codeFont{}, headingFont{}, smallFont{};
    modern::Theme theme;
    HWND hoveredButton{};
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
    int completedCaptures = -1;
    ~App() {
        closing = true;
        if (worker.joinable()) worker.join();
        banner.reset();
        if (bannerStream) bannerStream->Release();
        if (font) DeleteObject(font);
        if (linkFont) DeleteObject(linkFont);
        if (codeFont) DeleteObject(codeFont);
        if (headingFont) DeleteObject(headingFont);
        if (smallFont) DeleteObject(smallFont);
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
        HWND control = CreateWindowExW(extended, type, text, WS_CHILD | WS_VISIBLE | style,
            Scale(x), Scale(y), Scale(width), Scale(height), window,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
        if (!control) throw std::runtime_error("Unable to create a window control.");
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), FALSE);
        if (_wcsicmp(type, L"EDIT") == 0) {
            SendMessageW(control, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetWindowSubclass(control, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
        }
        if (_wcsicmp(type, L"BUTTON") == 0) SetWindowSubclass(control, ButtonProc, 1, reinterpret_cast<DWORD_PTR>(this));
        controls.push_back({control, x, y, width, height, page});
        return control;
    }
    void Button(const wchar_t* title, int id, int x, int y, int width, int height, const wchar_t* tip) {
        HWND control = Add(L"BUTTON", title, WS_TABSTOP | BS_OWNERDRAW, id, x, y, width, height);
        TOOLINFOW info{sizeof(info)};
        info.uFlags = TTF_IDISHWND | TTF_SUBCLASS;
        info.hwnd = window;
        info.uId = reinterpret_cast<UINT_PTR>(control);
        info.lpszText = const_cast<wchar_t*>(tip);
        SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&info));
    }
    void Edit(int id, const wchar_t* text, int x, int y, int width, int height, DWORD extra = 0, int page = -1) {
        Add(L"EDIT", text, EditStyle | extra, id, x, y, width, height, page);
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
        auto previousCode = codeFont, previousHeading = headingFont, previousSmall = smallFont;
        font = CreateFontW(-MulDiv(10, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        linkFont = CreateFontW(-MulDiv(9, dpi, 72), 0, 0, 0, FW_NORMAL, FALSE, TRUE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        codeFont = CreateFontW(-MulDiv(10, dpi, 72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,FIXED_PITCH,L"Consolas");
        headingFont = CreateFontW(-MulDiv(10, dpi, 72),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        smallFont = CreateFontW(-MulDiv(9, dpi, 72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        for (const auto& control : controls) {
            int id = GetDlgCtrlID(control.window);
            HFONT selected = id == Link ? linkFont : (id == HtmlHeading || id == PatternHeading || id == OutputHeading ? headingFont :
                (id == Input || id == Pattern || id == Output ? codeFont : (id == Status || id == Engine || id == CaptureCount ? smallFont : font)));
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(selected), TRUE);
            if (id == Url || id == SearchText)
                SetWindowPos(control.window,nullptr,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        if (previous) DeleteObject(previous);
        if (previousLink) DeleteObject(previousLink);
        if (previousCode) DeleteObject(previousCode);
        if (previousHeading) DeleteObject(previousHeading);
        if (previousSmall) DeleteObject(previousSmall);
    }
    void Layout() {
        for (const auto& c : controls) MoveWindow(c.window, Scale(c.x), Scale(c.y), Scale(c.width), Scale(c.height), TRUE);
        if (Get(Tabs)) SendMessageW(Get(Tabs), TCM_SETITEMSIZE, 0, MAKELPARAM(Scale(91), Scale(45)));
        InvalidateRect(window, nullptr, TRUE);
    }
    void Init() {
        dpi = GetDpiForWindow(window);
        theme.Load(window);
        Fonts();
        tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, window, nullptr, instance, nullptr);
        Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, Url, 29, 20, 323, 32);
        SendMessageW(Get(Url), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Enter web site URL or browse for HTML / text file"));
        Button(L"Connect", Connect, 369, 18, 88, 36, L"Download source using RainRegExp.ini settings");
        Button(L"Browse", Browse, 465, 18, 83, 36, L"Browse for a local HTML or text file");
        Add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL, SearchText, 591, 20, 244, 32);
        SendMessageW(Get(SearchText), EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"Find in HTML…"));
        Button(L"Search", Search, 858, 18, 84, 36, L"Find the next occurrence in the HTML (case insensitive)");
        Add(L"STATIC", L"HTML to Parse", SS_CENTERIMAGE, HtmlHeading, 33, 83, 300, 30);
        Edit(Input, L"", 32, 132, 896, 162, ES_AUTOHSCROLL | WS_HSCROLL);
        Button(L"Save", SaveInput, 789, 83, 62, 30, L"Save the HTML to disk");
        Button(L"Wrap", WrapInput, 859, 83, 68, 30, L"Toggle HTML word wrapping");
        Add(L"STATIC", L"Your RegExp", SS_CENTERIMAGE, PatternHeading, 33, 334, 300, 30);
        Edit(Pattern, L"(?siU)", 134, 387, 783, 69, ES_AUTOHSCROLL | WS_HSCROLL);
        Button(L"Parse", Parse, 32, 379, 80, 36, L"Test with Rainmeter WebParser's PCRE engine");
        Button(L"Copy", Copy, 32, 424, 80, 36, L"Copy the expression to the clipboard");
        Button(L"Wrap", WrapPattern, 859, 334, 68, 30, L"Toggle expression word wrapping");
        Button(L"Save", Save, 719, 334, 62, 30, L"Save URL, expression and notes as a session");
        Button(L"Load", Load, 789, 334, 62, 30, L"Load a saved session");
        Add(L"STATIC", L"Output", SS_CENTERIMAGE, OutputHeading, 33, 505, 63, 30);
        Edit(Output, L"", 32, 558, 427, 168, ES_READONLY);
        Add(L"STATIC", L"", SS_RIGHT, CaptureCount, 385, 511, 72, 24);
        Add(WC_TABCONTROLW, L"", WS_TABSTOP | WS_CLIPSIBLINGS | TCS_FIXEDWIDTH, Tabs, 490, 494, 452, 246);
        SetWindowSubclass(Get(Tabs), TabProc, 1, reinterpret_cast<DWORD_PTR>(this));
        SendMessageW(Get(Tabs), TCM_SETITEMSIZE, 0, MAKELPARAM(Scale(91), Scale(45)));
        for (auto title : {L"Welcome", L"Notepad", L"RegExp"}) {
            TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = const_cast<wchar_t*>(title);
            TabCtrl_InsertItem(Get(Tabs), TabCtrl_GetItemCount(Get(Tabs)), &item);
        }
        Add(L"STATIC", L"", SS_OWNERDRAW, Banner, 512, 565, 246, 49, 0);
        auto pngResource = FindResourceW(instance, MAKEINTRESOURCEW(5), RT_RCDATA);
        auto pngHandle = LoadResource(instance, pngResource);
        bannerStream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(pngHandle)), SizeofResource(instance, pngResource));
        if (!bannerStream) throw std::runtime_error("Unable to load the embedded banner.");
        banner = std::make_unique<Gdiplus::Bitmap>(bannerStream, FALSE);
        if (banner->GetLastStatus() != Gdiplus::Ok) throw std::runtime_error("Unable to decode the embedded banner PNG.");
        Add(L"STATIC", L"Create and test your RegExp options.", 0, 0, 512, 626, 405, 22, 0);
        Add(L"STATIC", L"Use Notepad for notes and general help.", 0, 0, 512, 652, 405, 22, 0);
        Add(L"STATIC", L"Open RegExp for regular expression hints.", 0, 0, 512, 678, 405, 22, 0);
        Add(L"STATIC", L"© 2026 Jeffrey Morley · CC BY-NC-SA 3.0", 0, 0, 512, 709, 282, 20, 0);
        Add(L"STATIC", L"rainmeter.net", SS_NOTIFY | SS_RIGHT, Link, 809, 709, 110, 20, 0);
        Edit(Notes, L"", 512, 559, 408, 166, 0, 1);
        Edit(Help, L"", 512, 559, 408, 166, ES_READONLY, 2);
        Add(L"STATIC", L"Ready", 0, Status, 20, 748, 440, 20);
        Add(L"STATIC", L"PCRE 8.45 · WebParser compatible", SS_RIGHT, Engine, 550, 748, 390, 20);
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
        InvalidateRect(Get(Tabs), nullptr, FALSE);
        RedrawWindow(window, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
    }
    void TabBackground(HWND control, HDC dc) const {
        RECT rect{}; GetClientRect(control, &rect); FillRect(dc, &rect, theme.surfaceBrush);
    }
    RECT Rect(int x, int y, int width, int height) const { return {Scale(x), Scale(y), Scale(x + width), Scale(y + height)}; }
    void PaintMain(HDC dc) {
        RECT client{}; GetClientRect(window, &client); FillRect(dc, &client, theme.backgroundBrush);
        for (const auto& panel : {Rect(18,72,924,235),Rect(18,323,924,155),Rect(18,494,456,246)}) {
            modern::Round(dc,panel,Scale(10),theme.surface,theme.border);
            modern::Line(dc,panel.left+1,panel.top+Scale(48),panel.right-1,panel.top+Scale(48),theme.border);
        }
        modern::Text(dc,smallFont,theme.muted,Rect(102,505,240,30),L"— StringIndex ⇒ Value");
        for (int id : {Url,SearchText,Pattern}) {
            RECT rect{}; GetWindowRect(Get(id), &rect); MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&rect),2);
            InflateRect(&rect,Scale(id == Pattern ? 10 : 11),Scale(id == Pattern ? 8 : 2));
            bool focused = GetFocus() == Get(id);
            modern::Round(dc,rect,Scale(8),id == Pattern ? theme.editor : theme.surface,focused ? theme.accent : theme.border,1.0f);
        }
        if (GetFocus() == Get(Input) || GetFocus() == Get(Output)) {
            RECT rect{}; GetWindowRect(GetFocus(),&rect); MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&rect),2);
            InflateRect(&rect,Scale(4),Scale(4));
            modern::Round(dc,rect,Scale(5),theme.surface,theme.accent,1.0f);
        }
    }
    void DrawButton(const DRAWITEMSTRUCT& item) {
        const bool primary = item.CtlID == Connect || item.CtlID == Parse;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool wrapped = (item.CtlID == WrapInput && wrapInput) || (item.CtlID == WrapPattern && wrapPattern);
        COLORREF fill = primary ? theme.accent : (pressed ? theme.pressed : (hoveredButton == item.hwndItem ? theme.hover : theme.surface));
        COLORREF edge = primary || wrapped ? theme.accent : theme.border;
        COLORREF foreground = primary ? theme.onAccent : (wrapped ? theme.accent : theme.text);
        if (disabled) { fill = theme.background; edge = theme.border; foreground = theme.muted; }
        else if (primary && pressed && !theme.highContrast) fill = theme.dark ? RGB(135,195,250) : RGB(30,93,153);
        else if (primary && hoveredButton == item.hwndItem && !theme.highContrast) fill = theme.dark ? RGB(125,185,245) : RGB(48,131,207);
        FillRect(item.hDC,&item.rcItem,item.CtlID == Connect || item.CtlID == Browse || item.CtlID == Search ? theme.backgroundBrush : theme.surfaceBrush);
        modern::Round(item.hDC,item.rcItem,theme.highContrast ? 0 : Scale(7),fill,edge);
        wchar_t text[80]{}; GetWindowTextW(item.hwndItem,text,80);
        modern::Text(item.hDC,font,foreground,item.rcItem,text,DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        if ((item.itemState & ODS_FOCUS) && !(item.itemState & ODS_NOFOCUSRECT)) {
            RECT focus = item.rcItem; InflateRect(&focus,-Scale(4),-Scale(4));
            SetTextColor(item.hDC,foreground); SetBkColor(item.hDC,fill); DrawFocusRect(item.hDC,&focus);
        }
    }
    void RefreshTheme() {
        theme.Load(window);
        RedrawWindow(window,nullptr,nullptr,RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN);
    }
    static LRESULT CALLBACK ButtonProc(HWND button, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto app = reinterpret_cast<App*>(data);
        if (message == WM_MOUSEMOVE && app->hoveredButton != button) {
            auto old = app->hoveredButton; app->hoveredButton = button;
            if (old) InvalidateRect(old,nullptr,FALSE);
            TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,button,0}; TrackMouseEvent(&track);
            InvalidateRect(button,nullptr,FALSE);
        } else if (message == WM_MOUSELEAVE) {
            if (app->hoveredButton == button) app->hoveredButton = nullptr;
            InvalidateRect(button,nullptr,FALSE);
        }
        return DefSubclassProc(button,message,wParam,lParam);
    }
    static LRESULT CALLBACK TabProc(HWND tab, UINT message, WPARAM wParam, LPARAM lParam, UINT_PTR, DWORD_PTR data) {
        auto app = reinterpret_cast<App*>(data);
        if (message == WM_ERASEBKGND) return 1;
        if (message == WM_PAINT) {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(tab,&paint);
            RECT client{}; GetClientRect(tab,&client); FillRect(dc,&client,app->theme.backgroundBrush);
            modern::Round(dc,client,app->Scale(10),app->theme.surface,app->theme.border);
            int selected = TabCtrl_GetCurSel(tab);
            modern::Line(dc,1,app->Scale(48),client.right-1,app->Scale(48),app->theme.border);
            for (int index = 0; index < TabCtrl_GetItemCount(tab); ++index) {
                RECT rect{}; TabCtrl_GetItemRect(tab,index,&rect);
                wchar_t label[64]{}; TCITEMW item{}; item.mask = TCIF_TEXT; item.pszText = label; item.cchTextMax = 64;
                TabCtrl_GetItem(tab,index,&item);
                rect.top = app->Scale(3); rect.bottom = app->Scale(47);
                modern::Text(dc,index == selected ? app->headingFont : app->font,index == selected ? app->theme.accent : app->theme.muted,rect,label,DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
                if (index == selected) {
                    RECT underline{rect.left+app->Scale(14),app->Scale(46),rect.right-app->Scale(14),app->Scale(49)};
                    HBRUSH brush = CreateSolidBrush(app->theme.accent); FillRect(dc,&underline,brush); DeleteObject(brush);
                    if (GetFocus() == tab) { InflateRect(&rect,-app->Scale(6),-app->Scale(7)); DrawFocusRect(dc,&rect); }
                }
            }
            EndPaint(tab,&paint); return 0;
        }
        LRESULT result = DefSubclassProc(tab,message,wParam,lParam);
        if (message == WM_LBUTTONDOWN || message == WM_KEYDOWN || message == WM_SETFOCUS || message == WM_KILLFOCUS) InvalidateRect(tab,nullptr,FALSE);
        return result;
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
            HWND previousSibling = GetWindow(control.window,GW_HWNDPREV);
            DestroyWindow(control.window);
            wrapped = !wrapped;
            control.window = CreateWindowExW(0, L"EDIT", text.c_str(), WS_CHILD | WS_VISIBLE | EditStyle |
                (wrapped ? 0 : ES_AUTOHSCROLL | WS_HSCROLL), Scale(saved.x), Scale(saved.y), Scale(saved.width), Scale(saved.height),
                window, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
            SendMessageW(control.window, WM_SETFONT, reinterpret_cast<WPARAM>(codeFont), TRUE);
            SendMessageW(control.window, EM_SETLIMITTEXT, 0x7ffffffe, 0);
            SetWindowSubclass(control.window, EditProc, 1, reinterpret_cast<DWORD_PTR>(this));
            SetWindowPos(control.window,previousSibling ? previousSibling : HWND_TOP,0,0,0,0,SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            SendMessageW(control.window, EM_SETSEL, start, end);
            SetFocus(control.window);
            SendMessageW(control.window, EM_SCROLLCARET, 0, 0);
            InvalidateRect(Get(id == Input ? WrapInput : WrapPattern),nullptr,FALSE);
            InvalidateRect(window,nullptr,FALSE);
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
        Set(Status,L"Expression copied to clipboard");
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
        Set(Status,target == Input ? L"Connecting…" : L"Parsing…");
        try {
            worker = std::thread([this, task = std::move(task)] {
                try { completedText = task(); }
                catch (const std::exception& error) { completedText = DecodeText(error.what()); }
                if (!closing) PostMessageW(window, Completed, 0, 0);
            });
        } catch (...) { Busy(false); Set(Status,L"Ready"); throw; }
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
            if (!path.empty()) { auto s = LoadSession(path); Set(Url, s.url); Set(Pattern, s.pattern); Set(Notes, s.notes); Set(Output, L""); Set(CaptureCount,L""); }
            break;
        }
        case Parse: {
            if (busy) break;
            const auto input = Text(Input), pattern = Text(Pattern);
            Set(Output, L"Parsing...");
            Start(Output, [this, input, pattern] {
                const auto result = Match(input, pattern);
                completedCaptures = result.error.empty() ? static_cast<int>(result.captures.size()) - 1 : -1;
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
        const int id = GetDlgCtrlID(edit);
        if ((id == Url || id == SearchText) && message == WM_NCCALCSIZE) {
            DefSubclassProc(edit,message,wParam,lParam);
            auto rect = wParam ? &reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam)->rgrc[0] : reinterpret_cast<RECT*>(lParam);
            HDC dc = GetDC(edit); auto previous = SelectObject(dc,app->font);
            TEXTMETRICW metrics{}; GetTextMetricsW(dc,&metrics); SelectObject(dc,previous); ReleaseDC(edit,dc);
            LONG padding = std::max(0L,(rect->bottom-rect->top-metrics.tmHeight)/2);
            rect->top += padding; rect->bottom = std::min(rect->bottom,rect->top+metrics.tmHeight);
            return 0;
        }
        if ((id == Url || id == SearchText) && message == WM_NCPAINT) {
            HDC dc = GetWindowDC(edit);
            RECT outer{}; GetWindowRect(edit,&outer);
            POINT origin{0,0}; ClientToScreen(edit,&origin);
            RECT client{}; GetClientRect(edit,&client);
            int x = origin.x-outer.left, y = origin.y-outer.top;
            ExcludeClipRect(dc,x,y,x+client.right,y+client.bottom);
            RECT bounds{0,0,outer.right-outer.left,outer.bottom-outer.top}; FillRect(dc,&bounds,app->theme.surfaceBrush);
            ReleaseDC(edit,dc); return 0;
        }
        if (message == WM_SETFOCUS || message == WM_KILLFOCUS) InvalidateRect(app->window,nullptr,FALSE);
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
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT paint{}; HDC dc = BeginPaint(window,&paint);
            RECT client{}; GetClientRect(window,&client);
            HDC buffer = CreateCompatibleDC(dc); HBITMAP bitmap = CreateCompatibleBitmap(dc,client.right,client.bottom);
            if (buffer && bitmap) {
                auto previous = SelectObject(buffer,bitmap); app->PaintMain(buffer);
                BitBlt(dc,0,0,client.right,client.bottom,buffer,0,0,SRCCOPY);
                SelectObject(buffer,previous);
            } else app->PaintMain(dc);
            if (bitmap) DeleteObject(bitmap); if (buffer) DeleteDC(buffer);
            EndPaint(window,&paint); return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
        case WM_SYSCOLORCHANGE: app->RefreshTheme(); return 0;
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
            app->Busy(false);
            app->Set(Status,L"Ready");
            if (app->completedTarget == Output) app->Set(CaptureCount,app->completedCaptures >= 0 ? std::to_wstring(app->completedCaptures) + L" captures" : L"");
            return 0;
        case WM_TIMER: SetWindowTextW(app->Get(Copy), L"Copy"); KillTimer(window, 1); return 0;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC: {
            const auto dc = reinterpret_cast<HDC>(wParam);
            int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
            bool footer = id == Status || id == Engine;
            bool pattern = id == Pattern;
            SetTextColor(dc,id == Link ? app->theme.accent : (footer || id == CaptureCount ? app->theme.muted : app->theme.text));
            SetBkColor(dc,footer ? app->theme.background : (pattern ? app->theme.editor : app->theme.surface));
            return reinterpret_cast<LRESULT>(footer ? app->theme.backgroundBrush : (pattern ? app->theme.editorBrush : app->theme.surfaceBrush));
        }
        case WM_DRAWITEM: {
            const auto item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam);
            if (item->CtlID == Banner) {
                app->TabBackground(item->hwndItem, item->hDC);
                Gdiplus::Graphics graphics(item->hDC);
                graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                graphics.DrawImage(app->banner.get(), 0, 0, item->rcItem.right, item->rcItem.bottom);
                return TRUE;
            }
            if (item->CtlType == ODT_BUTTON) { app->DrawButton(*item); return TRUE; }
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
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.hbrBackground = nullptr;
    type.hIcon = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED));
    type.hIconSm = static_cast<HICON>(LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_SHARED));
    RegisterClassExW(&type);
    UINT dpi = GetDpiForSystem();
    RECT rect{0, 0, MulDiv(modern::ClientWidth, dpi, 96), MulDiv(modern::ClientHeight, dpi, 96)};
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    AdjustWindowRectExForDpi(&rect, style, FALSE, 0, dpi);
    int width = rect.right - rect.left, height = rect.bottom - rect.top;
    HWND window = CreateWindowExW(0, type.lpszClassName, L"RainRegExp 5.1", style,
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
