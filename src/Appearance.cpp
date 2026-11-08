// Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
#include "Appearance.h"
#include <algorithm>
#include <stdexcept>
namespace rainregexp {
static void InvalidScale() {
    throw std::runtime_error("ScalePercent must be Auto or a whole number from 75 to 500.");
}
static bool Allowed(int percent) {
    return percent >= 75 && percent <= 500;
}
static std::wstring AppearanceValue(const std::wstring& contents, const wchar_t* key) {
    auto value = IniValue(contents,L"RainRegExp",key,L"Auto");
    const auto first = value.find_first_not_of(L" \t\r\n");
    value = first == std::wstring::npos ? L"" : value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
    return value;
}
DarkMode ParseDarkMode(const std::wstring& contents) {
    const auto value = AppearanceValue(contents,L"DarkMode");
    if (value.empty() || _wcsicmp(value.c_str(),L"Auto") == 0) return DarkMode::Auto;
    if (_wcsicmp(value.c_str(),L"True") == 0) return DarkMode::Enabled;
    if (_wcsicmp(value.c_str(),L"False") == 0) return DarkMode::Disabled;
    throw std::runtime_error("DarkMode must be Auto, True, or False.");
}
bool ResolveDarkMode(DarkMode mode, bool windowsDark, bool highContrast) {
    return !highContrast && (mode == DarkMode::Enabled || (mode == DarkMode::Auto && windowsDark));
}
int ParseScalePercent(const std::wstring& contents) {
    const auto value = AppearanceValue(contents,L"ScalePercent");
    if (value.empty() || _wcsicmp(value.c_str(),L"Auto") == 0) return 0;
    if (value.size() > 3 || value.find_first_not_of(L"0123456789") != std::wstring::npos) InvalidScale();
    const int percent = std::stoi(value);
    if (!Allowed(percent)) InvalidScale();
    return percent;
}
UINT LayoutDpi(UINT windowsDpi, int percent) {
    if (!percent) return windowsDpi;
    if (!Allowed(percent)) InvalidScale();
    return static_cast<UINT>(MulDiv(96,percent,100));
}
int ScaleLogical(int value, UINT windowsDpi, int percent) {
    if (!percent) return MulDiv(value,windowsDpi,96);
    if (!Allowed(percent)) InvalidScale();
    // Avoid rounding through integer DPI: 113% means exactly 113%, not 112.5%.
    return MulDiv(value,percent,100);
}
int ConstrainResize(RECT& bounds, UINT edge, int currentPercent, int frameWidth, int frameHeight) {
    const int fromWidth = MulDiv(bounds.right-bounds.left-frameWidth,100,BaseClientWidth);
    const int fromHeight = MulDiv(bounds.bottom-bounds.top-frameHeight,100,BaseClientHeight);
    const bool vertical = edge == WMSZ_TOP || edge == WMSZ_BOTTOM;
    const bool corner = edge != WMSZ_LEFT && edge != WMSZ_RIGHT && !vertical;
    const bool useHeight = vertical || (corner && std::abs(fromHeight-currentPercent) > std::abs(fromWidth-currentPercent));
    const int percent = std::clamp(useHeight ? fromHeight : fromWidth,75,500);
    const int width = MulDiv(BaseClientWidth,percent,100)+frameWidth;
    const int height = MulDiv(BaseClientHeight,percent,100)+frameHeight;
    if (edge == WMSZ_LEFT || edge == WMSZ_TOPLEFT || edge == WMSZ_BOTTOMLEFT) bounds.left = bounds.right-width;
    else bounds.right = bounds.left+width;
    if (edge == WMSZ_TOP || edge == WMSZ_TOPLEFT || edge == WMSZ_TOPRIGHT) bounds.top = bounds.bottom-height;
    else bounds.bottom = bounds.top+height;
    return percent;
}
std::wstring SetScalePercent(const std::wstring& contents, int percent) {
    if (!Allowed(percent)) InvalidScale();
    const auto trim = [](const std::wstring& value) {
        const auto first = value.find_first_not_of(L" \t\r\n");
        return first == std::wstring::npos ? std::wstring{} : value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
    };
    const std::wstring newline = contents.find(L"\r\n") != std::wstring::npos ? L"\r\n" : L"\n";
    const std::wstring setting = L"ScalePercent="+std::to_wstring(percent);
    bool active = false;
    size_t insert = std::wstring::npos;
    for (size_t start = 0; start < contents.size();) {
        const size_t end = contents.find(L'\n',start);
        const size_t next = end == std::wstring::npos ? contents.size() : end+1;
        const auto line = trim(contents.substr(start,next-start));
        if (!line.empty() && line.front() != L';') {
            if (line.front() == L'[' && line.back() == L']') {
                active = _wcsicmp(trim(line.substr(1,line.size()-2)).c_str(),L"RainRegExp") == 0;
                if (active && insert == std::wstring::npos) insert = next;
            } else if (active) {
                const auto equals = line.find(L'=');
                if (equals != std::wstring::npos && _wcsicmp(trim(line.substr(0,equals)).c_str(),L"ScalePercent") == 0) {
                    size_t valueEnd = end == std::wstring::npos ? contents.size() : end;
                    if (valueEnd > start && contents[valueEnd-1] == L'\r') --valueEnd;
                    auto result = contents;
                    result.replace(start,valueEnd-start,setting);
                    return result;
                }
            }
        }
        start = next;
    }
    auto result = contents;
    if (insert != std::wstring::npos) {
        if (insert == result.size() && !result.empty() && result.back() != L'\n') result += newline;
        else result.insert(insert,setting+newline);
        if (insert == contents.size() && !contents.empty() && contents.back() != L'\n') result += setting+newline;
    } else {
        if (!result.empty() && result.back() != L'\n') result += newline;
        result += L"[RainRegExp]"+newline+setting+newline;
    }
    return result;
}
}
