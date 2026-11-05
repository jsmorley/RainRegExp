// Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <dwmapi.h>
#include <string>
#include <algorithm>
#include "Appearance.h"

namespace modern {
constexpr int ClientWidth = 960, ClientHeight = 772;
struct Theme {
    bool dark = false, highContrast = false;
    COLORREF background{}, surface{}, text{}, muted{}, border{}, hover{}, pressed{}, accent{}, onAccent{}, editor{};
    HBRUSH backgroundBrush{}, surfaceBrush{}, editorBrush{};
    ~Theme() { ClearBrushes(); }
    void ClearBrushes() {
        for (auto brush : {backgroundBrush, surfaceBrush, editorBrush}) if (brush) DeleteObject(brush);
        backgroundBrush = surfaceBrush = editorBrush = nullptr;
    }
    void Load(HWND window, rainregexp::DarkMode mode) {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0);
        highContrast = (contrast.dwFlags & HCF_HIGHCONTRASTON) != 0;
        DWORD light = 1, size = sizeof(light);
        RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
            L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &light, &size);
        dark = rainregexp::ResolveDarkMode(mode,!light,highContrast);
        background = dark ? RGB(27,31,38) : RGB(243,245,248);
        surface = dark ? RGB(37,43,52) : RGB(255,255,255);
        text = dark ? RGB(230,237,247) : RGB(36,50,71);
        muted = dark ? RGB(169,182,200) : RGB(99,113,135);
        border = dark ? RGB(59,68,82) : RGB(220,226,235);
        hover = dark ? RGB(51,62,77) : RGB(237,242,248);
        pressed = dark ? RGB(62,77,94) : RGB(220,232,244);
        accent = dark ? RGB(105,175,239) : RGB(40,118,191);
        onAccent = dark ? RGB(15,32,49) : RGB(255,255,255);
        editor = dark ? RGB(31,37,46) : RGB(247,249,252);
        if (highContrast) {
            background = GetSysColor(COLOR_BTNFACE); surface = GetSysColor(COLOR_WINDOW);
            editor = surface; text = GetSysColor(COLOR_WINDOWTEXT); muted = text;
            border = text; hover = background; pressed = GetSysColor(COLOR_HIGHLIGHT);
            accent = GetSysColor(COLOR_HIGHLIGHT); onAccent = GetSysColor(COLOR_HIGHLIGHTTEXT);
        }
        ClearBrushes(); backgroundBrush = CreateSolidBrush(background);
        surfaceBrush = CreateSolidBrush(surface); editorBrush = CreateSolidBrush(editor);
        BOOL useDark = dark;
        DwmSetWindowAttribute(window, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &useDark, sizeof(useDark));
    }
};
inline Gdiplus::Color Color(COLORREF value) { return Gdiplus::Color(255, GetRValue(value), GetGValue(value), GetBValue(value)); }
inline void Round(HDC dc, const RECT& rect, int radius, COLORREF fill, COLORREF stroke, float thickness = 1.0f) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    const float x = static_cast<float>(rect.left) + thickness / 2, y = static_cast<float>(rect.top) + thickness / 2;
    const float width = static_cast<float>(rect.right - rect.left) - thickness, height = static_cast<float>(rect.bottom - rect.top) - thickness;
    const float diameter = std::min(static_cast<float>(radius * 2), std::min(width, height));
    if (width <= 0 || height <= 0) return;
    Gdiplus::GraphicsPath path;
    if (diameter > 0) {
        path.AddArc(x,y,diameter,diameter,180,90); path.AddArc(x+width-diameter,y,diameter,diameter,270,90);
        path.AddArc(x+width-diameter,y+height-diameter,diameter,diameter,0,90); path.AddArc(x,y+height-diameter,diameter,diameter,90,90);
        path.CloseFigure();
    } else path.AddRectangle(Gdiplus::RectF(x,y,width,height));
    Gdiplus::SolidBrush brush(Color(fill)); graphics.FillPath(&brush,&path);
    Gdiplus::Pen pen(Color(stroke),thickness); graphics.DrawPath(&pen,&path);
}
inline void Line(HDC dc, int x1, int y1, int x2, int y2, COLORREF color) {
    auto pen = CreatePen(PS_SOLID,1,color); auto previous = SelectObject(dc,pen);
    MoveToEx(dc,x1,y1,nullptr); LineTo(dc,x2,y2); SelectObject(dc,previous); DeleteObject(pen);
}
inline void Text(HDC dc, HFONT font, COLORREF color, RECT rect, const wchar_t* text, UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX) {
    auto previous = SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
    DrawTextW(dc,text,-1,&rect,flags); SelectObject(dc,previous);
}
}
