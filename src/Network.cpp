#include "Network.h"
#include <sstream>
#include <stdexcept>
#include <memory>
#include <climits>

namespace rainregexp {
static std::wstring Trim(std::wstring value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return value.substr(first, value.find_last_not_of(L" \t\r\n") - first + 1);
}
DWORD ParseFlags(const std::wstring& text) {
    struct Flag { const wchar_t* name; DWORD value; };
    static const Flag flags[] = {
        {L"ForceReload", INTERNET_FLAG_RELOAD}, {L"Resync", INTERNET_FLAG_RESYNCHRONIZE},
        {L"NoCookies", INTERNET_FLAG_NO_COOKIES}, {L"Hyperlink", INTERNET_FLAG_HYPERLINK},
        {L"TempFile", INTERNET_FLAG_NEED_FILE}, {L"NoAuth", INTERNET_FLAG_NO_AUTH},
        {L"NoCacheWrite", INTERNET_FLAG_NO_CACHE_WRITE}, {L"PragmaNoCache", INTERNET_FLAG_PRAGMA_NOCACHE},
        {L"Secure", INTERNET_FLAG_SECURE}, {L"IgnoreCertName", INTERNET_FLAG_IGNORE_CERT_CN_INVALID},
        {L"IgnoreCertDate", INTERNET_FLAG_IGNORE_CERT_DATE_INVALID},
        {L"IgnoreHTTPRedirect", INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP},
        {L"IgnoreHTTPSRedirect", INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS}
    };
    DWORD result = INTERNET_FLAG_RESYNCHRONIZE;
    std::wistringstream stream(text);
    std::wstring token;
    while (std::getline(stream, token, L'|')) {
        token = Trim(token);
        if (token.empty()) continue;
        bool found = false;
        for (const auto& flag : flags) {
            if (_wcsicmp(token.c_str(), flag.name) == 0) { result |= flag.value; found = true; break; }
        }
        if (!found) throw std::runtime_error("Invalid Flags value in RainRegExp.ini. See README.md for supported flags.");
    }
    return result;
}
static std::wstring Setting(const std::wstring& contents, const wchar_t* key, const wchar_t* fallback) {
    std::wistringstream stream(contents);
    std::wstring line;
    bool inSection = false;
    while (std::getline(stream, line)) {
        line = Trim(line);
        if (line.empty() || line.front() == L';') continue;
        if (line.front() == L'[' && line.back() == L']') {
            inSection = _wcsicmp(Trim(line.substr(1, line.size() - 2)).c_str(), L"WebParser") == 0;
            continue;
        }
        if (!inSection) continue;
        const auto equals = line.find(L'=');
        if (equals == std::wstring::npos || _wcsicmp(Trim(line.substr(0, equals)).c_str(), key) != 0) continue;
        auto value = Trim(line.substr(equals + 1));
        if (value.size() >= 2 && (value.front() == L'\"' || value.front() == L'\'') && value.front() == value.back())
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return fallback;
}
NetworkOptions ReadNetworkOptions(const std::filesystem::path& ini) {
    // Decode explicitly: the Win32 profile API does not support UTF-8 INI files.
    const auto contents = std::filesystem::exists(ini) ? ReadText(ini) : L"";
    return ParseNetworkOptions(contents);
}
NetworkOptions ParseNetworkOptions(const std::wstring& contents) {
    NetworkOptions options;
    options.proxy = Setting(contents, L"ProxyServer", L"/auto");
    if (options.proxy.empty()) options.proxy = L"/auto";
    options.userAgent = Setting(contents, L"UserAgent", L"Rainmeter WebParser plugin");
    if (options.userAgent.empty()) options.userAgent = L"Rainmeter WebParser plugin";
    if (options.userAgent.find_first_of(L"\r\n") != std::wstring::npos) throw std::runtime_error("UserAgent must be a single line.");
    options.flags = ParseFlags(Setting(contents, L"Flags", L"Resync"));
    for (unsigned index = 1;; ++index) {
        auto key = index == 1 ? L"Header" : L"Header" + std::to_wstring(index);
        auto value = Setting(contents, key.c_str(), L"");
        if (value.empty()) break;
        options.headers += value + L"\r\n";
    }
    if (!options.headers.empty()) options.headers += L"\r\n";
    return options;
}
static void InternetError(const char* operation) {
    const auto code = GetLastError();
    throw std::runtime_error(std::string(operation) + " failed (WinINet error " + std::to_string(code) + ").");
}
struct InternetDeleter { void operator()(void* handle) const { if (handle) InternetCloseHandle(handle); } };
std::wstring Fetch(const std::wstring& url, const NetworkOptions& options, const std::atomic<bool>& cancelled) {
    if (url.compare(0, 7, L"http://") != 0 && url.compare(0, 8, L"https://") != 0)
        throw std::runtime_error("Enter an http:// or https:// URL, or use Browse for a local file.");
    DWORD type = INTERNET_OPEN_TYPE_PROXY;
    const wchar_t* proxy = options.proxy.c_str();
    if (_wcsicmp(proxy, L"/auto") == 0) { type = INTERNET_OPEN_TYPE_PRECONFIG; proxy = nullptr; }
    else if (_wcsicmp(proxy, L"/none") == 0) { type = INTERNET_OPEN_TYPE_DIRECT; proxy = nullptr; }
    std::unique_ptr<void, InternetDeleter> session(InternetOpenW(options.userAgent.c_str(), type, proxy, nullptr, 0));
    if (!session) InternetError("InternetOpen");
    DWORD timeout = 15000;
    InternetSetOptionW(session.get(), INTERNET_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session.get(), INTERNET_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    InternetSetOptionW(session.get(), INTERNET_OPTION_SEND_TIMEOUT, &timeout, sizeof(timeout));
    if (cancelled) return {};
    std::unique_ptr<void, InternetDeleter> request(InternetOpenUrlW(session.get(), url.c_str(),
        options.headers.empty() ? nullptr : options.headers.c_str(), static_cast<DWORD>(options.headers.size()), options.flags, 0));
    if (!request) InternetError("InternetOpenUrl");
    std::string bytes;
    char buffer[32768];
    while (!cancelled) {
        DWORD count = 0;
        if (!InternetReadFile(request.get(), buffer, sizeof(buffer), &count)) InternetError("InternetReadFile");
        if (!count) break;
        if (bytes.size() + count > INT_MAX) throw std::runtime_error("Response exceeds PCRE's size limit.");
        bytes.append(buffer, count);
    }
    // WebParser's default CodePage=0 maps to UTF-8, without UCP or UTF compile flags.
    int count = MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring text(count, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, bytes.data(), static_cast<int>(bytes.size()), text.data(), count);
    return text;
}
}
