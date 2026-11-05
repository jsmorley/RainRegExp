// Copyright (c) 2026 Jeffrey Morley. CC BY-NC-SA 3.0.
#pragma once
#include "Core.h"
#include <windows.h>
#include <array>
namespace rainregexp {
inline constexpr std::array<int,12> ScalePresets{100,125,150,175,200,225,250,300,350,400,450,500};
// Zero internally means Auto, following the DPI of the window's monitor.
int ParseScalePercent(const std::wstring& contents);
UINT LayoutDpi(UINT windowsDpi, int scalePercent);
int ScaleLogical(int logicalPixels, UINT windowsDpi, int scalePercent);
}
