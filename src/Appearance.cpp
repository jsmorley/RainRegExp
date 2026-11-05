// Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
#include "Appearance.h"
#include <algorithm>
#include <stdexcept>
namespace rainregexp {
static void InvalidScale() {
    throw std::runtime_error("ScalePercent must be Auto, a whole number from 100 to 150, or 175, 200, 225, 250, 300, 350, 400, 450, 500.");
}
static bool Allowed(int percent) {
    return (percent >= 100 && percent <= 150) || std::find(ScalePresets.begin(),ScalePresets.end(),percent) != ScalePresets.end();
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
}
