// rr-netd's /net/health responder never blocks the race loop: a client that
// connects and stalls costs no poll time and is closed at its deadline, a
// request split across polls is answered, other paths get 404, and the body
// is built only when a request needs it.
#include "check.hpp"
#include "admin_http.hpp"
#include "health_http.hpp"
#include "room.hpp"

#include <chrono>
#include <csignal>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32 // built only where rr-netd-health exists (POSIX)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using rr::netd::HealthHttp;

namespace {

int connect_to(int port) {
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    ::inet_pton(AF_INET, "127.0.0.1", &a.sin_addr);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

void put(int fd, const std::string& s) {
    (void)::send(fd, s.data(), s.size(), 0);
}

// Reads until the server closes (bounded by a receive timeout).
std::string read_all(int fd) {
    timeval tv{1, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    std::string out;
    char buf[512];
    for (ssize_t got; (got = ::recv(fd, buf, sizeof buf, 0)) > 0;) out.append(buf, size_t(got));
    return out;
}

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

void settle() {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
}

} // namespace

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    rr::test::Checks check;
    HealthHttp http("127.0.0.1", 0);
    check(http.ok() && http.port() > 0, "listens on a free loopback port");
    int bodies = 0;
    auto body = [&] {
        ++bodies;
        return std::string("{\"ok\":true}");
    };
    uint64_t now = 1000;

    http.poll(now, body);
    check.eq(bodies, 0, "no body built without a request");

    // Eight stalled clients: the old responder waited 200 ms on each.
    int stalled[8];
    for (int& fd : stalled) fd = connect_to(http.port());
    settle();
    auto t = std::chrono::steady_clock::now();
    http.poll(now, body);
    double took = ms_since(t);
    check(took < 50, "stalled clients do not block a poll (" + std::to_string(took) + " ms)");
    check.eq(http.pending(), 8, "stalled clients held open");

    // A request split across polls is answered while the stalled ones wait.
    const int split = connect_to(http.port());
    put(split, "GET /net/he");
    settle();
    http.poll(now, body);
    check.eq(bodies, 0, "partial request not answered yet");
    put(split, "alth HTTP/1.0\r\nHost: x\r\n\r\n");
    settle();
    t = std::chrono::steady_clock::now();
    http.poll(now + 10, body);
    took = ms_since(t);
    check(took < 50, "answering poll does not block (" + std::to_string(took) + " ms)");
    const std::string reply = read_all(split);
    check(reply.rfind("HTTP/1.0 200 OK", 0) == 0, "health request gets 200");
    check(reply.size() > 11 && reply.compare(reply.size() - 11, 11, "{\"ok\":true}") == 0,
          "health body sent");
    check.eq(bodies, 1, "body built once for one request");
    ::close(split);

    // Other paths get 404 without building the body.
    const int other = connect_to(http.port());
    put(other, "GET /secret HTTP/1.0\r\n\r\n");
    settle();
    http.poll(now + 20, body);
    check(read_all(other).rfind("HTTP/1.0 404 Not Found", 0) == 0, "other path gets 404");
    check.eq(bodies, 1, "404 does not build the body");
    ::close(other);

    // Stalled clients are closed at their deadline.
    http.poll(now + HealthHttp::request_timeout_ms - 1, body);
    check.eq(http.pending(), 8, "stalled clients kept before the deadline");
    http.poll(now + HealthHttp::request_timeout_ms, body);
    check.eq(http.pending(), 0, "stalled clients closed at the deadline");
    char c;
    check(::recv(stalled[0], &c, 1, 0) == 0, "stalled client sees the close");
    for (int fd : stalled) ::close(fd);

    // Pending sockets are capped; extra connections are shed.
    std::vector<int> flood;
    for (int i = 0; i < HealthHttp::max_pending + 8; ++i) { // poll as we go: the backlog is 16
        flood.push_back(connect_to(http.port()));
        if (i % 4 == 3) {
            settle();
            http.poll(now + 5000, body);
        }
    }
    settle();
    http.poll(now + 5000, body);
    check.eq(http.pending(), HealthHttp::max_pending, "pending sockets capped");
    for (int fd : flood) ::close(fd);

    // Admin HTTP router & Web Console auth checks.
    {
        rr::netd::Server srv({});
        rr::netd::AdminRouterConfig acfg;
        acfg.admin_token = "secret-nx-token";
        acfg.git_revision = "testrev";

        HealthHttp admin_http("127.0.0.1", 0);
        check(admin_http.ok(), "admin http listens");
        auto pump = [&](int fd, const std::string& req) {
            put(fd, req);
            settle();
            admin_http.poll_http(now + 6000, [&](const rr::netd::HttpRequest& r) {
                return rr::netd::handle_http_request(r, srv, now + 6000, acfg);
            });
            return read_all(fd);
        };

        int f1 = connect_to(admin_http.port());
        check(pump(f1, "GET /admin HTTP/1.0\r\n\r\n").find("Road Rash NX Admin") != std::string::npos,
              "GET /admin serves Web UI");
        ::close(f1);

        int f2 = connect_to(admin_http.port());
        check(pump(f2, "GET /admin/api/state HTTP/1.0\r\n\r\n").rfind("HTTP/1.0 401 Unauthorized", 0) == 0,
              "unauthenticated /admin/api/state returns 401");
        ::close(f2);

        int f3 = connect_to(admin_http.port());
        const std::string login_body = "{\"token\":\"secret-nx-token\"}";
        const std::string login_res =
            pump(f3, "POST /admin/api/login HTTP/1.0\r\nContent-Length: " + std::to_string(login_body.size()) +
                         "\r\n\r\n" + login_body);
        check(login_res.rfind("HTTP/1.0 200 OK", 0) == 0 &&
                  login_res.find("Set-Cookie: rr_admin=secret-nx-token") != std::string::npos,
              "login sets cookie");
        ::close(f3);

        int f4 = connect_to(admin_http.port());
        const std::string st_res =
            pump(f4, "GET /admin/api/state HTTP/1.0\r\nAuthorization: Bearer secret-nx-token\r\n\r\n");
        check(st_res.rfind("HTTP/1.0 200 OK", 0) == 0 &&
                  st_res.find("\"rev\":\"testrev\"") != std::string::npos,
              "authorized /admin/api/state returns JSON");
        ::close(f4);

        int f5 = connect_to(admin_http.port());
        const std::string trav = pump(
            f5, "GET /admin/api/logs/../etc/passwd HTTP/1.0\r\nAuthorization: Bearer secret-nx-token\r\n\r\n");
        check(trav.rfind("HTTP/1.0 400 Bad Request", 0) == 0, "path traversal blocked");
        ::close(f5);
    }

    return check.finish("server_health");
}
