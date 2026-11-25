// A tiny HTTP server on 127.0.0.1 for tests: answers each request with the next queued reply and keeps what it
// received (method, path, headers, body). One connection per request.
#pragma once
#include <memory>
#include <string>
#include <vector>

namespace TestNet {
struct MockRequest { std::string method, path, headers, body; };
class MockHttpServer {
public:
    MockHttpServer();
    ~MockHttpServer();
    int Port() const;
    void Reply(int status, const std::string& body);       // queued; requests with nothing queued get a 500
    std::vector<MockRequest> Requests();                     // received so far
    void ClearRequests();
private:
    struct State;
    std::shared_ptr<State> m;
};
}
