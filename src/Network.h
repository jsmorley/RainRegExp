#pragma once
#include "Core.h"
#include <windows.h>
#include <wininet.h>
#include <atomic>
namespace rainregexp {
struct NetworkOptions {
    std::wstring proxy = L"/auto";
    std::wstring userAgent = L"Rainmeter WebParser plugin";
    std::wstring headers;
    DWORD flags = INTERNET_FLAG_RESYNCHRONIZE;
};
DWORD ParseFlags(const std::wstring& text);
NetworkOptions ReadNetworkOptions(const std::filesystem::path& ini);
std::wstring Fetch(const std::wstring& url, const NetworkOptions& options, const std::atomic<bool>& cancelled);
}
