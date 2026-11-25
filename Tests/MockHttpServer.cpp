#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "MockHttpServer.h"
#include <atomic>
#include <deque>
#include <mutex>
#include <thread>
#pragma comment(lib, "ws2_32.lib")

namespace TestNet {
struct MockHttpServer::State {
    SOCKET listener = INVALID_SOCKET;
    int port = 0;
    std::thread thread;
    std::mutex mutex;
    std::deque<std::pair<int, std::string>> replies;
    std::vector<MockRequest> requests;
    std::atomic<bool> stop{ false };
};

static std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }

MockHttpServer::MockHttpServer() : m(std::make_shared<State>()) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    m->listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    bind(m->listener, (sockaddr*)&address, sizeof(address));
    int size = sizeof(address);
    getsockname(m->listener, (sockaddr*)&address, &size);
    m->port = ntohs(address.sin_port);
    listen(m->listener, 16);
    auto state = m;
    m->thread = std::thread([state] {
        while (!state->stop) {
            SOCKET client = accept(state->listener, nullptr, nullptr);
            if (client == INVALID_SOCKET) break;
            std::string data;
            char buffer[8192];
            size_t headerEnd = std::string::npos, length = 0;
            while (true) {
                int n = recv(client, buffer, sizeof(buffer), 0);
                if (n <= 0) break;
                data.append(buffer, n);
                if (headerEnd == std::string::npos && (headerEnd = data.find("\r\n\r\n")) != std::string::npos) {
                    const std::string head = Lower(data.substr(0, headerEnd));
                    const size_t at = head.find("content-length:");
                    if (at != std::string::npos) length = std::stoul(head.substr(at + 15));
                }
                if (headerEnd != std::string::npos && data.size() >= headerEnd + 4 + length) break;
            }
            MockRequest request;
            if (headerEnd != std::string::npos) {
                const std::string first = data.substr(0, data.find("\r\n"));
                const size_t a = first.find(' '), b = first.find(' ', a + 1);
                request.method = first.substr(0, a);
                request.path = first.substr(a + 1, b - a - 1);
                request.headers = data.substr(0, headerEnd);
                request.body = data.substr(headerEnd + 4);
            }
            std::pair<int, std::string> reply{ 500, R"({"error":{"message":"no reply queued"}})" };
            {
                std::lock_guard lock(state->mutex);
                state->requests.push_back(request);
                if (!state->replies.empty()) { reply = state->replies.front(); state->replies.pop_front(); }
            }
            const std::string response = "HTTP/1.1 " + std::to_string(reply.first) + (reply.first == 200 ? " OK" : " Error") +
                "\r\nContent-Type: application/json\r\nConnection: close\r\nContent-Length: " + std::to_string(reply.second.size()) + "\r\n\r\n" + reply.second;
            send(client, response.data(), (int)response.size(), 0);
            shutdown(client, SD_SEND);
            closesocket(client);
        }
    });
}

MockHttpServer::~MockHttpServer() {
    m->stop = true;
    closesocket(m->listener);
    if (m->thread.joinable()) m->thread.join();
    WSACleanup();
}
int MockHttpServer::Port() const { return m->port; }
void MockHttpServer::Reply(int status, const std::string& body) { std::lock_guard lock(m->mutex); m->replies.emplace_back(status, body); }
std::vector<MockRequest> MockHttpServer::Requests() { std::lock_guard lock(m->mutex); return m->requests; }
void MockHttpServer::ClearRequests() { std::lock_guard lock(m->mutex); m->requests.clear(); }
}
