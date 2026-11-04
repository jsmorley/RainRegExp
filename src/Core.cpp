#include "Core.h"
#include <windows.h>
#include "config.h"
#include "pcre.h"
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <sstream>
#include <memory>

namespace rainregexp {
MatchResult Match(const std::wstring& subject, const std::wstring& pattern) {
    static_assert(sizeof(wchar_t) == 2, "WebParser uses UTF-16 code units");
    MatchResult result;
    if (subject.size() > INT_MAX) throw std::runtime_error("Input exceeds PCRE's size limit.");
    const char* error = nullptr;
    pcre16* re = pcre16_compile(reinterpret_cast<PCRE_SPTR16>(pattern.c_str()), 0,
        &error, &result.errorOffset, nullptr);
    if (!re) {
        result.code = PCRE_ERROR_BADMAGIC;
        result.error = L"Error = 2: There is an error in your RegExp.\r\n\r\n";
        if (error) while (*error) result.error += static_cast<unsigned char>(*error++);
        result.error += L"\r\nAt character offset " + std::to_wstring(result.errorOffset) + L" (zero-based).";
        return result;
    }
    int offsets[300] = {};
    result.code = pcre16_exec(re, nullptr, reinterpret_cast<PCRE_SPTR16>(subject.c_str()),
        static_cast<int>(subject.size()), 0, 0, offsets, 300);
    pcre16_free(re);
    if (result.code > 0) {
        for (int i = 0; i < result.code; ++i) {
            const int start = offsets[2 * i], end = offsets[2 * i + 1];
            result.captures.push_back(start < 0 ? L"" : subject.substr(start, end - start));
        }
    } else if (result.code == PCRE_ERROR_NOMATCH) {
        result.error = L"Error = 1: Could not match all searches.\r\n\r\nTry removing search strings one at a time to find the failing part.";
    } else if (result.code == 0) {
        result.error = L"Too many substrings (WebParser's 300-integer capture buffer was exceeded).";
    } else {
        result.error = L"PCRE matching error: " + std::to_wstring(result.code);
    }
    return result;
}

std::wstring DecodeText(const std::string& bytes) {
    if (bytes.size() >= 2 && ((static_cast<unsigned char>(bytes[0]) == 0xff && static_cast<unsigned char>(bytes[1]) == 0xfe) ||
        (static_cast<unsigned char>(bytes[0]) == 0xfe && static_cast<unsigned char>(bytes[1]) == 0xff))) {
        std::wstring text;
        const bool little = static_cast<unsigned char>(bytes[0]) == 0xff;
        for (size_t i = 2; i + 1 < bytes.size(); i += 2) {
            auto a = static_cast<unsigned char>(bytes[i]), b = static_cast<unsigned char>(bytes[i + 1]);
            text += static_cast<wchar_t>(little ? a | (b << 8) : (a << 8) | b);
        }
        return text;
    }
    const size_t skip = bytes.compare(0, 3, "\xef\xbb\xbf") == 0 ? 3 : 0;
    if (bytes.size() - skip > INT_MAX) throw std::runtime_error("Text file is too large.");
    const char* data = bytes.data() + skip;
    int length = static_cast<int>(bytes.size() - skip);
    if (!length) return {};
    UINT page = CP_UTF8;
    int count = MultiByteToWideChar(page, MB_ERR_INVALID_CHARS, data, length, nullptr, 0);
    if (!count) { page = CP_ACP; count = MultiByteToWideChar(page, 0, data, length, nullptr, 0); }
    if (!count) throw std::runtime_error("Unable to decode text.");
    std::wstring text(count, L'\0');
    MultiByteToWideChar(page, 0, data, length, text.data(), count);
    return text;
}

std::wstring ReadText(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("Unable to open the selected file.");
    std::string bytes((std::istreambuf_iterator<char>(stream)), {});
    if (stream.bad()) throw std::runtime_error("Unable to read the selected file.");
    return DecodeText(bytes);
}

void WriteText(const std::filesystem::path& path, const std::wstring& text) {
    if (text.size() > INT_MAX) throw std::runtime_error("Text is too large.");
    int count = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string bytes(count, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), bytes.data(), count, nullptr, nullptr);
    auto temporary = path;
    temporary += L".rainregexp.tmp";
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        if (!stream) throw std::runtime_error("Unable to create the selected file.");
        stream.write("\xef\xbb\xbf", 3);
        stream.write(bytes.data(), bytes.size());
        stream.close();
        if (!stream) { DeleteFileW(temporary.c_str()); throw std::runtime_error("Unable to write the selected file."); }
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("Unable to replace the selected file.");
    }
}

static std::wstring Escape(const std::wstring& text) {
    std::wstring result;
    for (auto c : text) {
        switch(c) {
        case L'\\': result += L"\\\\"; break;
        case L'\r': result += L"\\r"; break;
        case L'\n': result += L"\\n"; break;
        default: result += c;
        }
    }
    return result;
}
static std::wstring Unescape(const std::wstring& text) {
    std::wstring result;
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == L'\\' && i + 1 < text.size()) {
            if (text[i + 1] == L'r') { result += L'\r'; ++i; continue; }
            if (text[i + 1] == L'n') { result += L'\n'; ++i; continue; }
            if (text[i + 1] == L'\\') { result += L'\\'; ++i; continue; }
        }
        result += text[i];
    }
    return result;
}
void SaveSession(const std::filesystem::path& path, const Session& s) {
    WriteText(path, L"[RainRegExpSession]\r\nEncoding=EscapedV1\r\n[URL]\r\nURL=" + Escape(s.url) +
        L"\r\n[RegExp]\r\nRegExp=" + Escape(s.pattern) + L"\r\n[Notes]\r\nNotes=" + Escape(s.notes) + L"\r\n");
}
Session LoadSession(const std::filesystem::path& path) {
    const auto text = ReadText(path);
    bool escaped = text.find(L"[RainRegExpSession]\r\nEncoding=EscapedV1") != std::wstring::npos ||
        text.find(L"[RainRegExpSession]\nEncoding=EscapedV1") != std::wstring::npos;
    Session s;
    std::wistringstream stream(text);
    std::wstring line, section;
    std::wstring* active = nullptr;
    bool found = false;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.size() >= 2 && line.front() == L'[' && line.back() == L']') {
            section = line; active = nullptr; continue;
        }
        std::wstring key;
        std::wstring* target = nullptr;
        if (section == L"[URL]") { key = L"URL="; target = &s.url; }
        if (section == L"[RegExp]") { key = L"RegExp="; target = &s.pattern; }
        if (section == L"[Notes]") { key = L"Notes="; target = &s.notes; }
        if (target && line.compare(0, key.size(), key) == 0 && !active) {
            *target = line.substr(key.size()); active = target; found = true;
        } else if (active && !escaped) { *active += L"\r\n" + line; }
    }
    if (!found) throw std::runtime_error("This file is not a RainRegExp session.");
    if (escaped) { s.url = Unescape(s.url); s.pattern = Unescape(s.pattern); s.notes = Unescape(s.notes); }
    else {
        for (auto target : {&s.url, &s.pattern, &s.notes})
            while (target->size() >= 2 && target->compare(target->size() - 2, 2, L"\r\n") == 0) target->resize(target->size() - 2);
    }
    return s;
}
}
