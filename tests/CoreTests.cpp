#include "Core.h"
#include "Network.h"
#include "config.h"
#include "pcre.h"
#include <iostream>
#include <stdexcept>
#include <fstream>

using namespace rainregexp;
static int checks = 0;
void Check(bool condition, const char* description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() / (L"RainRegExp-tests-" + std::to_wstring(GetCurrentProcessId()));
    try {
        std::filesystem::create_directories(directory);
        Check(std::string(pcre16_version()).find("8.45") == 0, "Exact PCRE version");
        auto result = Match(L"<TITLE>Hello\nWorld</TITLE><item>First</item><item>Second</item>", L"(?siU)<title>(.*)</title>.*<item>(.*)</item>");
        Check(result.code == 3 && result.captures[1] == L"Hello\nWorld" && result.captures[2] == L"First", "Inline siU flags and capture indices");
        result = Match(L"one two", L"(\\w+)");
        Check(result.code == 2 && result.captures[0] == L"one" && result.captures[1] == L"one", "One match only and no appended .*$");
        result = Match(L"b", L"(a)?(b)(c)?");
        Check(result.code == 3 && result.captures[1].empty() && result.captures[2] == L"b", "Optional and trailing unmatched groups");
        Check(Match(L"a", L"(").code < 0, "Compile error");
        Check(Match(L"a", L"z").code == PCRE_ERROR_NOMATCH, "No match");
        Check(Match(L"", L"()").code == 2, "Empty capture");
        Check(Match(L"foo foo", L"(?<word>\\w+) \\k<word>").captures[1] == L"foo", "Named backreference");
        Check(Match(L"ab", L"(?<=a)(b)").captures[1] == L"b", "Lookbehind");
        Check(Match(L"é", L"\\w+").code == PCRE_ERROR_NOMATCH, "UCP disabled by default");
        Check(Match(L"é", L"(*UCP)\\w+").code > 0, "Explicit UCP supported");
        std::wstring groups;
        for (int i = 0; i < 99; ++i) groups += L"(a)";
        Check(Match(std::wstring(99, L'a'), groups).code == 100, "WebParser supports 99 capture indices");
        groups += L"(a)";
        Check(Match(std::wstring(100, L'a'), groups).code == 0, "WebParser capture vector limit");
        // Compare the adapter to WebParser's literal compile/execute calls.
        for (const auto& pattern : {L"(?siU)<a>(.*)</a>", L"(?m)^(.+)$", L"(a)?(b)", L"(?|(a)|(b))", L"a\\Kb", L"()", L"(*UTF)(.)", L"(x)+"}) {
            for (const auto& input : {L"<a>é</a>tail", L"a\r\nb\n", L"ab", L"", L"xx", L"🙂"}) {
                const char* error = nullptr; int errorOffset = 0, offsets[300] = {};
                auto re = pcre16_compile(reinterpret_cast<PCRE_SPTR16>(pattern), 0, &error, &errorOffset, nullptr);
                Check(re != nullptr, "Reference compile");
                int rc = pcre16_exec(re, nullptr, reinterpret_cast<PCRE_SPTR16>(input), static_cast<int>(wcslen(input)), 0, 0, offsets, 300);
                auto actual = Match(input, pattern);
                Check(actual.code == rc, "Reference execution code");
                for (int i = 0; i < rc; ++i) {
                    auto expected = offsets[2 * i] < 0 ? L"" : std::wstring(input + offsets[2 * i], input + offsets[2 * i + 1]);
                    Check(actual.captures[i] == expected, "Reference capture");
                }
                pcre16_free(re);
            }
        }
        const Session original{L"https://example.com/?a=b", L"(?siU)\\d+\r\n([a-z=]+)\\n", L"Notes é 🙂\r\n[Notes]\r\nNotes=x\\r\\n\n\n"};
        SaveSession(directory / L"session.txt", original);
        auto loaded = LoadSession(directory / L"session.txt");
        Check(original.url == loaded.url && original.pattern == loaded.pattern && original.notes == loaded.notes, "Lossless multiline session round trip");
        WriteText(directory / L"legacy.txt", L"[URL]\r\nURL=https://example.com/\r\n[RegExp]\r\nRegExp=(?siU)(.*)\r\n[Notes]\r\nNotes=First\r\nSecond\r\n");
        loaded = LoadSession(directory / L"legacy.txt");
        Check(loaded.pattern == L"(?siU)(.*)" && loaded.notes == L"First\r\nSecond", "AutoIt session compatibility");
        Check(ReadText(directory / L"session.txt").find(L"Notes é 🙂") != std::wstring::npos, "UTF-8 text I/O");
        Check(DecodeText(std::string("\xff\xfe\x41\x00", 4)) == L"A", "UTF-16 LE input");
        Check(DecodeText(std::string("\xfe\xff\x00\x41", 4)) == L"A", "UTF-16 BE input");
        auto options = ReadNetworkOptions(directory / L"missing.ini");
        Check(options.proxy == L"/auto" && options.userAgent == L"Rainmeter WebParser plugin" && options.flags == INTERNET_FLAG_RESYNCHRONIZE, "WebParser network defaults");
        WriteText(directory / L"options.ini", L"[WebParser]\r\nProxyServer=/none\r\nUserAgent=Test/5.0\r\nHeader=Accept: text/html\r\nHeader2=X-Test: yes\r\nHeader4=Ignored: yes\r\nFlags=ForceReload|NoCookies|NoCacheWrite\r\n");
        options = ReadNetworkOptions(directory / L"options.ini");
        Check(options.proxy == L"/none" && options.userAgent == L"Test/5.0", "Configured proxy and user agent");
        Check(options.headers == L"Accept: text/html\r\nX-Test: yes\r\n\r\n", "Consecutive numbered headers");
        Check(options.flags == (INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_NO_CACHE_WRITE), "Combined flags retain Resync like WebParser");
        Check(ParseFlags(L"Hyperlink|TempFile|NoAuth|PragmaNoCache|Secure|IgnoreCertName|IgnoreCertDate|IgnoreHTTPRedirect|IgnoreHTTPSRedirect") ==
            (INTERNET_FLAG_RESYNCHRONIZE | INTERNET_FLAG_HYPERLINK | INTERNET_FLAG_NEED_FILE | INTERNET_FLAG_NO_AUTH | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_SECURE |
             INTERNET_FLAG_IGNORE_CERT_CN_INVALID | INTERNET_FLAG_IGNORE_CERT_DATE_INVALID | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTP | INTERNET_FLAG_IGNORE_REDIRECT_TO_HTTPS), "All remaining WebParser flags");
        Check(ParseFlags(L" nocookies | resync ") == (INTERNET_FLAG_NO_COOKIES | INTERNET_FLAG_RESYNCHRONIZE), "Case-insensitive flags");
        bool rejected = false;
        try { ParseFlags(L"Unknown"); } catch (const std::exception&) { rejected = true; }
        Check(rejected, "Invalid flag diagnosed");
        WriteText(directory / L"options.ini", L"[WebParser]\r\nUserAgent=Changed/5.0\r\n");
        Check(ReadNetworkOptions(directory / L"options.ini").userAgent == L"Changed/5.0", "Options reread without restart");
        const std::wstring iniText = L"[webparser]\r\nUserAgent=EncodingTest/5.0\r\nProxyServer=/none\r\nHeader=X-Name: café\r\n";
        for (bool little : {true, false}) {
            auto file = directory / (little ? L"utf16le.ini" : L"utf16be.ini");
            {
                std::ofstream stream(file, std::ios::binary);
                stream.put(little ? '\xff' : '\xfe'); stream.put(little ? '\xfe' : '\xff');
                for (auto c : iniText) { stream.put(static_cast<char>(little ? c & 255 : c >> 8)); stream.put(static_cast<char>(little ? c >> 8 : c & 255)); }
            }
            options = ReadNetworkOptions(file);
            Check(options.userAgent == L"EncodingTest/5.0" && options.proxy == L"/none" && options.headers == L"X-Name: café\r\n\r\n", "UTF-16 INI decoding");
        }
        {
            std::ofstream stream(directory / L"utf8.ini", std::ios::binary);
            stream << "[WebParser]\nUserAgent=Utf8/5.0\nHeader=X-Name: caf\xc3\xa9\n";
        }
        options = ReadNetworkOptions(directory / L"utf8.ini");
        Check(options.userAgent == L"Utf8/5.0" && options.headers == L"X-Name: café\r\n\r\n", "UTF-8 without BOM");
        std::filesystem::remove_all(directory);
        std::cout << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        std::filesystem::remove_all(directory);
        return 1;
    }
}
