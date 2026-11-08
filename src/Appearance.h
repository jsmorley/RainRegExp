// Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
#pragma once
#include "Core.h"
#include <windows.h>
namespace rainregexp {
inline constexpr int BaseClientWidth = 960, BaseClientHeight = 772;
// Constrain a dragged outer window rectangle to a whole-percent client scale.
int ConstrainResize(RECT& bounds, UINT edge, int currentPercent, int frameWidth, int frameHeight);
std::wstring SetScalePercent(const std::wstring& contents, int percent);
enum class DarkMode { Auto, Enabled, Disabled };
DarkMode ParseDarkMode(const std::wstring& contents);
bool ResolveDarkMode(DarkMode mode, bool windowsDark, bool highContrast);
// Zero internally means Auto, following the DPI of the window's monitor.
int ParseScalePercent(const std::wstring& contents);
UINT LayoutDpi(UINT windowsDpi, int scalePercent);
int ScaleLogical(int logicalPixels, UINT windowsDpi, int scalePercent);
}
