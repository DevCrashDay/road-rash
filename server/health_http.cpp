#include "health_http.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace rr::netd {

namespace {

#ifdef MSG_NOSIGNAL
constexpr int send_flags = MSG_NOSIGNAL;
#else
constexpr int send_flags = 0;
#endif

void set_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    ::fcntl(fd, F_SETFL, (flags < 0 ? 0 : flags) | O_NONBLOCK);
#ifdef SO_NOSIGPIPE
    int one = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof one);
#endif
}

bool would_block() { return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR; }

const char* status_text(int status) {
    switch (status) {
    case 200: return "200 OK";
    case 400: return "400 Bad Request";
    case 401: return "401 Unauthorized";
    case 403: return "403 Forbidden";
    case 404: return "404 Not Found";
    case 405: return "405 Method Not Allowed";
    default: return "500 Internal Server Error";
    }
}

std::string serialize_response(const HttpResponse& res) {
    return std::string("HTTP/1.0 ") + status_text(res.status) +
           "\r\nContent-Type: " + res.content_type +
           "\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n" +
           res.extra_headers + "Content-Length: " + std::to_string(res.body.size()) +
           "\r\nConnection: close\r\n\r\n" + res.body;
}

bool ascii_iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    return true;
}

bool find_header_end(const std::string& in, size_t& hdr_end, size_t& sep_len) {
    const size_t crlf = in.find("\r\n\r\n");
    const size_t lf = in.find("\n\n");
    if (crlf != std::string::npos && (lf == std::string::npos || crlf < lf)) {
        hdr_end = crlf;
        sep_len = 4;
        return true;
    }
    if (lf != std::string::npos) {
        hdr_end = lf;
        sep_len = 2;
        return true;
    }
    return false;
}

HttpRequest parse_request(const std::string& raw, size_t hdr_end, size_t sep_len, size_t content_len) {
    HttpRequest req;
    const size_t line_end = raw.find('\n');
    std::string line = raw.substr(0, line_end == std::string::npos ? hdr_end : line_end);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    const size_t sp1 = line.find(' ');
    if (sp1 != std::string::npos) {
        req.method = line.substr(0, sp1);
        const size_t sp2 = line.find(' ', sp1 + 1);
        const std::string target =
            sp2 == std::string::npos ? line.substr(sp1 + 1) : line.substr(sp1 + 1, sp2 - sp1 - 1);
        const size_t q = target.find('?');
        req.path = q == std::string::npos ? target : target.substr(0, q);
        if (q != std::string::npos) req.query = target.substr(q + 1);
    }
    size_t pos = line_end == std::string::npos ? hdr_end : line_end + 1;
    while (pos < hdr_end) {
        size_t next = raw.find('\n', pos);
        if (next == std::string::npos || next > hdr_end) next = hdr_end;
        std::string h = raw.substr(pos, next - pos);
        if (!h.empty() && h.back() == '\r') h.pop_back();
        const size_t colon = h.find(':');
        if (colon != std::string::npos) {
            std::string key = h.substr(0, colon);
            std::string val = h.substr(colon + 1);
            size_t start = val.find_first_not_of(" \t");
            val = start == std::string::npos ? std::string() : val.substr(start);
            if (ascii_iequals(key, "Authorization")) req.authorization = val;
            else if (ascii_iequals(key, "Cookie")) req.cookie = val;
        }
        pos = next + 1;
    }
    const size_t body_start = hdr_end + sep_len;
    if (body_start < raw.size() && content_len > 0)
        req.body = raw.substr(body_start, std::min(content_len, raw.size() - body_start));
    return req;
}

size_t parse_content_length(const std::string& raw, size_t hdr_end) {
    size_t pos = 0;
    while (pos < hdr_end) {
        size_t next = raw.find('\n', pos);
        if (next == std::string::npos || next > hdr_end) next = hdr_end;
        std::string h = raw.substr(pos, next - pos);
        if (!h.empty() && h.back() == '\r') h.pop_back();
        const size_t colon = h.find(':');
        if (colon != std::string::npos && ascii_iequals(h.substr(0, colon), "Content-Length")) {
            const long v = std::strtol(h.substr(colon + 1).c_str(), nullptr, 10);
            return v > 0 ? size_t(std::min<long>(v, long(HealthHttp::max_request_bytes))) : 0;
        }
        pos = next + 1;
    }
    return 0;
}

} // namespace

HealthHttp::HealthHttp(const std::string& bind, int port) {
    fd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0) return;
    int one = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    socklen_t len = sizeof a;
    if (::inet_pton(AF_INET, bind.c_str(), &a.sin_addr) != 1 ||
        ::bind(fd_, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0 || ::listen(fd_, 16) != 0 ||
        ::getsockname(fd_, reinterpret_cast<sockaddr*>(&a), &len) != 0) {
        ::close(fd_);
        fd_ = -1;
        return;
    }
    port_ = ntohs(a.sin_port);
    set_nonblocking(fd_);
}

HealthHttp::~HealthHttp() {
    for (Client& c : clients_) ::close(c.fd);
    if (fd_ >= 0) ::close(fd_);
}

void HealthHttp::poll(uint64_t now_ms, const std::function<std::string()>& body) {
    poll_http(now_ms, [&](const HttpRequest& req) -> HttpResponse {
        const bool health = req.method == "GET" && (req.path == "/net/health" || req.path == "/health");
        return HttpResponse{health ? 200 : 404, "application/json", {}, health ? body() : "{\"error\":\"not found\"}"};
    });
}

void HealthHttp::poll_http(uint64_t now_ms, const std::function<HttpResponse(const HttpRequest&)>& handler) {
    if (fd_ < 0) return;
    for (int n = 0; n < 8; ++n) {
        const int c = ::accept(fd_, nullptr, nullptr);
        if (c < 0) break;
        if (int(clients_.size()) >= max_pending) { // shed new load; existing requests keep their slots
            ::close(c);
            continue;
        }
        set_nonblocking(c);
        clients_.push_back(Client{c, now_ms + request_timeout_ms});
    }
    for (size_t i = 0; i < clients_.size();) {
        Client& c = clients_[i];
        if (step(c, handler) && now_ms < c.deadline_ms) {
            ++i;
            continue;
        }
        ::close(c.fd);
        clients_[i] = std::move(clients_.back());
        clients_.pop_back();
    }
}

bool HealthHttp::step(Client& c, const std::function<HttpResponse(const HttpRequest&)>& handler) {
    if (!c.answered) {
        char buf[512];
        size_t hdr_end = 0, sep_len = 0, content_len = 0;
        for (;;) {
            const ssize_t got = ::recv(c.fd, buf, sizeof buf, 0);
            if (got > 0) {
                const size_t space = max_request_bytes > c.in.size() ? max_request_bytes - c.in.size() : 0;
                if (space > 0) c.in.append(buf, std::min(space, size_t(got)));
                if (c.in.size() >= max_request_bytes) break;
                continue;
            }
            if (got == 0) break; // peer finished sending; answer what we have
            if (would_block()) {
                if (!find_header_end(c.in, hdr_end, sep_len)) return true;
                content_len = parse_content_length(c.in, hdr_end);
                if (c.in.size() < hdr_end + sep_len + content_len && c.in.size() < max_request_bytes) return true;
                break;
            }
            return false;
        }
        if (c.in.empty()) return false;
        if (!find_header_end(c.in, hdr_end, sep_len)) {
            hdr_end = c.in.size();
            sep_len = 0;
        } else {
            content_len = parse_content_length(c.in, hdr_end);
        }
        c.out = serialize_response(handler(parse_request(c.in, hdr_end, sep_len, content_len)));
        c.answered = true;
    }
    while (c.sent < c.out.size()) {
        const ssize_t put = ::send(c.fd, c.out.data() + c.sent, c.out.size() - c.sent, send_flags);
        if (put > 0) {
            c.sent += size_t(put);
            continue;
        }
        return put < 0 && would_block();
    }
    return false; // fully sent
}

} // namespace rr::netd
