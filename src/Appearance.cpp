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
int ParseScalePercent(const std::wstring& contents) {
    auto value = IniValue(contents,L"RainRegExp",L"ScalePercent",L"Auto");
    const auto first = value.find_first_not_of(L" \t\r\n");
    value = first == std::wstring::npos ? L"" : value.substr(first,value.find_last_not_of(L" \t\r\n")-first+1);
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
