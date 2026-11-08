#include <winsock2.h>
#include <ws2tcpip.h>
#include "Network.h"
#include <thread>
#include <iostream>
#include <stdexcept>

using namespace rainregexp;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
// Local fixture: captures the actual request sent by WinINet, not a mock.
std::string Request(NetworkOptions options, bool proxy) {
    SOCKET listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    Check(listener != INVALID_SOCKET, "Create loopback listener");
    sockaddr_in address{};
    address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) || listen(listener, 1)) {
        closesocket(listener); throw std::runtime_error("Bind/listen failed");
    }
    int size = sizeof(address);
    getsockname(listener, reinterpret_cast<sockaddr*>(&address), &size);
    const auto port = std::to_wstring(ntohs(address.sin_port));
    std::string captured;
    std::thread server([&] {
        fd_set set; FD_ZERO(&set); FD_SET(listener, &set);
        timeval wait{10, 0};
        if (select(0, &set, nullptr, nullptr, &wait) <= 0) return;
        SOCKET client = accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) return;
        DWORD timeout = 10000;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
        char buffer[2048];
        while (captured.find("\r\n\r\n") == std::string::npos && captured.size() < 65536) {
            int count = recv(client, buffer, sizeof(buffer), 0);
            if (count <= 0) break;
            captured.append(buffer, count);
        }
        const std::string body = "<title>caf\xc3\xa9</title>";
        const std::string response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        size_t sent = 0;
        while (sent < response.size()) {
            int count = send(client, response.data() + sent, static_cast<int>(response.size() - sent), 0);
            if (count <= 0) break;
            sent += count;
        }
        shutdown(client, SD_SEND); closesocket(client);
    });
    std::atomic<bool> cancelled{false};
    try {
        std::wstring url = L"http://127.0.0.1:" + port + L"/sample";
        if (proxy) { options.proxy = L"127.0.0.1:" + port; url = L"http://rainregexp-test.invalid/sample"; }
        const auto text = Fetch(url, options, cancelled);
        server.join(); closesocket(listener);
        Check(text == L"<title>café</title>", "UTF-8 network response decoded correctly");
        Check(Match(text, L"(?siU)<title>(.*)</title>").captures[1] == L"café", "Fetched text parsed with WebParser PCRE");
    } catch (...) { if (server.joinable()) server.join(); closesocket(listener); throw; }
    return captured;
}
int main() {
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data)) return 1;
    try {
        NetworkOptions options;
        options.proxy = L"/none";
        options.userAgent = L"RainRegExp-NetworkTest/5.3";
        options.headers = L"X-RainRegExp-One: first\r\nX-RainRegExp-Two: second\r\n\r\n";
        options.flags = ParseFlags(L"ForceReload|NoCookies|NoCacheWrite");
        auto request = Request(options, false);
        Check(request.find("User-Agent: RainRegExp-NetworkTest/5.3\r\n") != std::string::npos, "UserAgent transmitted");
        Check(request.find("X-RainRegExp-One: first\r\n") != std::string::npos && request.find("X-RainRegExp-Two: second\r\n") != std::string::npos, "Custom headers transmitted");
        request = Request(options, true);
        Check(request.find("GET http://rainregexp-test.invalid/sample HTTP/") == 0, "Configured proxy receives absolute request URL");
        std::cout << "Loopback network checks passed (UserAgent, headers, explicit proxy, UTF-8 and PCRE).\n";
        WSACleanup(); return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; WSACleanup(); return 1; }
}
