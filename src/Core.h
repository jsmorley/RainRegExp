#pragma once
#include <string>
#include <vector>
#include <filesystem>

namespace rainregexp {
struct MatchResult {
    int code = 0;
    int errorOffset = 0;
    std::wstring error;
    std::vector<std::wstring> captures;
};
// Same PCRE source, configuration, flags and vector size as WebParser.
MatchResult Match(const std::wstring& subject, const std::wstring& pattern);
std::wstring DecodeText(const std::string& bytes);
std::wstring ReadText(const std::filesystem::path& path);
void WriteText(const std::filesystem::path& path, const std::wstring& text);
std::wstring IniValue(const std::wstring& contents, const wchar_t* section, const wchar_t* key, const wchar_t* fallback);
struct Session { std::wstring url, pattern, notes; };
void SaveSession(const std::filesystem::path& path, const Session& session);
Session LoadSession(const std::filesystem::path& path);
}
