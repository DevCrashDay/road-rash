#include "net/ws_channel.hpp"
#include "net/protocol.hpp"
#include "net/rtc_channel.hpp"

#include <switch.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

namespace rr::net {
namespace {

constexpr size_t kMaxQueuedFrames = 4096;
constexpr size_t kMaxHeaderBytes = 16 * 1024;
constexpr uint64_t kConnectTimeoutMs = 10000;
constexpr uint64_t kPingIntervalMs = 10000;

bool g_sock_ready = false;
bool g_sock_usable = false;
bool g_ssl_ready = false;
bool g_ssl_usable = false;
bool g_csrng_ready = false;
std::once_flag g_net_init_once;

void cleanup_switch_net_services() {
    if (g_ssl_ready) {
        sslExit();
        g_ssl_ready = false;
    }
    if (g_csrng_ready) {
        csrngExit();
        g_csrng_ready = false;
    }
    if (g_sock_ready) {
        socketExit();
        g_sock_ready = false;
    }
}

bool ensure_switch_net_services() {
    std::call_once(g_net_init_once, [] {
        ::Result rc = socketInitializeDefault();
        if (R_SUCCEEDED(rc) || rc == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
            g_sock_ready = R_SUCCEEDED(rc);
            g_sock_usable = true;
        } else {
            printf("[SwitchNet] socketInitializeDefault failed: 0x%08X\n", rc);
        }
        rc = sslInitialize(4);
        if (R_SUCCEEDED(rc) || rc == MAKERESULT(Module_Libnx, LibnxError_AlreadyInitialized)) {
            g_ssl_ready = R_SUCCEEDED(rc);
            g_ssl_usable = true;
        } else {
            printf("[SwitchNet] sslInitialize failed: 0x%08X\n", rc);
        }
        rc = csrngInitialize();
        if (R_SUCCEEDED(rc)) {
            g_csrng_ready = true;
        }
        std::atexit(cleanup_switch_net_services);
    });
    return g_sock_usable;
}

void fill_random(uint8_t* dst, size_t n) {
    if (g_csrng_ready && R_SUCCEEDED(csrngGetRandomBytes(dst, n))) return;
    std::random_device rd;
    for (size_t i = 0; i < n; ++i) dst[i] = uint8_t(rd() & 0xffu);
}

std::array<uint8_t, 20> sha1_digest(const std::string& input) {
    std::vector<uint8_t> msg(input.begin(), input.end());
    const uint64_t bit_len = uint64_t(msg.size()) * 8ull;
    msg.push_back(0x80);
    while ((msg.size() % 64) != 56) msg.push_back(0);
    for (int i = 7; i >= 0; --i) msg.push_back(uint8_t((bit_len >> (i * 8)) & 0xffu));

    uint32_t h0 = 0x67452301u;
    uint32_t h1 = 0xEFCDAB89u;
    uint32_t h2 = 0x98BADCFEu;
    uint32_t h3 = 0x10325476u;
    uint32_t h4 = 0xC3D2E1F0u;
    auto rotl = [](uint32_t v, int bits) { return (v << bits) | (v >> (32 - bits)); };

    for (size_t off = 0; off < msg.size(); off += 64) {
        uint32_t w[80]{};
        for (int i = 0; i < 16; ++i) {
            w[i] = (uint32_t(msg[off + i * 4]) << 24) | (uint32_t(msg[off + i * 4 + 1]) << 16) |
                   (uint32_t(msg[off + i * 4 + 2]) << 8) | uint32_t(msg[off + i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) w[i] = rotl(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

        uint32_t a = h0, b = h1, c = h2, d = h3, e = h4;
        for (int i = 0; i < 80; ++i) {
            uint32_t f = 0, k = 0;
            if (i < 20) {
                f = (b & c) | ((~b) & d);
                k = 0x5A827999u;
            } else if (i < 40) {
                f = b ^ c ^ d;
                k = 0x6ED9EBA1u;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d);
                k = 0x8F1BBCDCu;
            } else {
                f = b ^ c ^ d;
                k = 0xCA62C1D6u;
            }
            const uint32_t tmp = rotl(a, 5) + f + e + k + w[i];
            e = d;
            d = c;
            c = rotl(b, 30);
            b = a;
            a = tmp;
        }
        h0 += a;
        h1 += b;
        h2 += c;
        h3 += d;
        h4 += e;
    }

    std::array<uint8_t, 20> out{};
    const uint32_t words[5] = {h0, h1, h2, h3, h4};
    for (int i = 0; i < 5; ++i) {
        out[i * 4 + 0] = uint8_t((words[i] >> 24) & 0xffu);
        out[i * 4 + 1] = uint8_t((words[i] >> 16) & 0xffu);
        out[i * 4 + 2] = uint8_t((words[i] >> 8) & 0xffu);
        out[i * 4 + 3] = uint8_t(words[i] & 0xffu);
    }
    return out;
}

std::string base64_encode(const uint8_t* data, size_t len) {
    static constexpr char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((len + 2) / 3) * 4);
    for (size_t i = 0; i < len; i += 3) {
        const uint32_t b0 = data[i];
        const uint32_t b1 = (i + 1 < len) ? data[i + 1] : 0;
        const uint32_t b2 = (i + 2 < len) ? data[i + 2] : 0;
        const uint32_t tri = (b0 << 16) | (b1 << 8) | b2;
        out.push_back(kTable[(tri >> 18) & 63]);
        out.push_back(kTable[(tri >> 12) & 63]);
        out.push_back((i + 1 < len) ? kTable[(tri >> 6) & 63] : '=');
        out.push_back((i + 2 < len) ? kTable[tri & 63] : '=');
    }
    return out;
}

struct ParsedWsUrl {
    bool tls = false;
    std::string host;
    std::string port;
    std::string host_header;
    std::string path = "/";
};

bool parse_ws_url(const std::string& url, ParsedWsUrl& out) {
    if (url.find_first_of(" \t\r\n") != std::string::npos) return false;
    size_t pos = 0;
    if (url.rfind("wss://", 0) == 0) {
        out.tls = true;
        out.port = "443";
        pos = 6;
    } else if (url.rfind("ws://", 0) == 0) {
        out.tls = false;
        out.port = "80";
        pos = 5;
    } else {
        return false;
    }

    const size_t slash = url.find('/', pos);
    const std::string authority = slash == std::string::npos ? url.substr(pos) : url.substr(pos, slash - pos);
    out.path = slash == std::string::npos ? "/" : url.substr(slash);
    if (authority.empty()) return false;
    out.host_header = authority;

    if (authority.front() == '[') {
        const size_t rb = authority.find(']');
        if (rb == std::string::npos || rb <= 1) return false;
        out.host = authority.substr(1, rb - 1);
        if (rb + 1 < authority.size()) {
            if (authority[rb + 1] != ':' || rb + 2 >= authority.size()) return false;
            out.port = authority.substr(rb + 2);
        }
    } else {
        const size_t colon = authority.rfind(':');
        if (colon != std::string::npos) {
            out.host = authority.substr(0, colon);
            out.port = authority.substr(colon + 1);
            if (out.port.empty()) return false;
        } else {
            out.host = authority;
        }
    }
    if (out.host.empty() || out.host.size() > 255) return false;
    for (char c : out.port)
        if (!std::isdigit(static_cast<unsigned char>(c))) return false;
    return true;
}

bool is_signal_packet(const std::vector<uint8_t>& payload) {
    const auto m = decode(payload);
    if (!m) return false;
    return std::holds_alternative<RtcOffer>(*m) || std::holds_alternative<RtcAnswer>(*m) ||
           std::holds_alternative<RtcCandidate>(*m) || std::holds_alternative<RtcClose>(*m);
}

bool is_ssl_would_block(::Result rc) {
    return R_MODULE(rc) == 123 && (R_DESCRIPTION(rc) == 204 || R_DESCRIPTION(rc) == 205);
}

std::string ascii_lower(std::string s) {
    for (char& c : s) c = char(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim_ascii(std::string s) {
    size_t a = 0;
    while (a < s.size() && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r')) ++a;
    size_t b = s.size();
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r')) --b;
    return s.substr(a, b - a);
}

class WsSwitchChannel final : public Channel {
  public:
    explicit WsSwitchChannel(std::string url) : url_(std::move(url)) {
        io_thread_ = std::thread([this] { run(); });
    }

    ~WsSwitchChannel() override {
        close();
        if (io_thread_.joinable()) io_thread_.join();
    }

    bool reliable() const override { return true; }

    void send(std::span<const uint8_t> bytes) override {
        if (status_.load(std::memory_order_acquire) != ChannelStatus::open || bytes.empty()) return;
        if (bytes.size() > max_message_bytes) return;
        std::lock_guard lock(tx_mu_);
        if (tx_queue_.size() >= kMaxQueuedFrames) return;
        tx_queue_.emplace_back(bytes.begin(), bytes.end());
    }

    std::optional<std::vector<uint8_t>> poll() override {
        for (;;) {
            std::vector<uint8_t> out;
            {
                std::lock_guard lock(rx_mu_);
                if (rx_queue_.empty()) return std::nullopt;
                out = std::move(rx_queue_.front());
                rx_queue_.pop_front();
            }
            if (!is_signal_packet(out)) return out;
        }
    }

    ChannelStatus status() const override { return status_.load(std::memory_order_acquire); }

    void close() override {
        close_requested_.store(true, std::memory_order_release);
        const ChannelStatus cur = status_.load(std::memory_order_acquire);
        if (cur == ChannelStatus::open || cur == ChannelStatus::connecting) {
            status_.store(ChannelStatus::closed, std::memory_order_release);
        }
    }

  private:
    enum class IoOutcome { ok, would_block, eof, error };

    IoOutcome io_read(uint8_t* dst, size_t cap, size_t& got) {
        got = 0;
        if (tls_) {
            u32 out_sz = 0;
            const ::Result rc = sslConnectionRead(&ssl_conn_, dst, u32(cap), &out_sz);
            if (R_SUCCEEDED(rc)) {
                if (out_sz == 0) return IoOutcome::eof;
                got = size_t(out_sz);
                return IoOutcome::ok;
            }
            if (is_ssl_would_block(rc)) return IoOutcome::would_block;
            return IoOutcome::error;
        }
        const ssize_t n = ::recv(fd_, dst, cap, 0);
        if (n > 0) {
            got = size_t(n);
            return IoOutcome::ok;
        }
        if (n == 0) return IoOutcome::eof;
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return IoOutcome::would_block;
        return IoOutcome::error;
    }

    IoOutcome io_write(const uint8_t* src, size_t len, size_t& wrote) {
        wrote = 0;
        if (tls_) {
            u32 out_sz = 0;
            const ::Result rc = sslConnectionWrite(&ssl_conn_, src, u32(len), &out_sz);
            if (R_SUCCEEDED(rc)) {
                wrote = size_t(out_sz);
                return wrote > 0 ? IoOutcome::ok : IoOutcome::would_block;
            }
            if (is_ssl_would_block(rc)) return IoOutcome::would_block;
            return IoOutcome::error;
        }
        const ssize_t n = ::send(fd_, src, len, 0);
        if (n > 0) {
            wrote = size_t(n);
            return IoOutcome::ok;
        }
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) return IoOutcome::would_block;
        return IoOutcome::error;
    }

    bool write_all(const uint8_t* data, size_t len, uint64_t deadline_ms) {
        size_t sent = 0;
        while (sent < len) {
            if (close_requested_.load(std::memory_order_acquire) && sent == 0) return false;
            size_t wrote = 0;
            const IoOutcome res = io_write(data + sent, len - sent, wrote);
            if (res == IoOutcome::ok) {
                sent += wrote;
                continue;
            }
            if (res != IoOutcome::would_block) return false;
            if (steady_ms() >= deadline_ms) return false;
            pollfd pfd{fd_, POLLOUT, 0};
            ::poll(&pfd, 1, 20);
        }
        return true;
    }

    bool send_ws_frame(uint8_t opcode, const uint8_t* payload, size_t len) {
        std::vector<uint8_t> frame;
        frame.reserve(14 + len);
        frame.push_back(uint8_t(0x80u | (opcode & 0x0fu))); // FIN=1

        if (len < 126) {
            frame.push_back(uint8_t(0x80u | len)); // MASK=1
        } else if (len <= 0xffffu) {
            frame.push_back(uint8_t(0x80u | 126u));
            frame.push_back(uint8_t((len >> 8) & 0xffu));
            frame.push_back(uint8_t(len & 0xffu));
        } else {
            frame.push_back(uint8_t(0x80u | 127u));
            const uint64_t len64 = uint64_t(len);
            for (int shift = 56; shift >= 0; shift -= 8) frame.push_back(uint8_t((len64 >> shift) & 0xffu));
        }

        uint8_t mask[4]{};
        fill_random(mask, sizeof(mask));
        frame.insert(frame.end(), mask, mask + 4);

        const size_t base = frame.size();
        frame.resize(base + len);
        for (size_t i = 0; i < len; ++i) frame[base + i] = payload[i] ^ mask[i & 3];

        return write_all(frame.data(), frame.size(), steady_ms() + 5000);
    }

    bool connect_tcp(const ParsedWsUrl& parsed) {
        addrinfo hints{};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;

        addrinfo* res = nullptr;
        const int gai = ::getaddrinfo(parsed.host.c_str(), parsed.port.c_str(), &hints, &res);
        if (gai != 0 || !res) {
            printf("[SwitchNet] getaddrinfo(%s:%s) failed: %d\n", parsed.host.c_str(), parsed.port.c_str(), gai);
            return false;
        }

        const uint64_t deadline = steady_ms() + kConnectTimeoutMs;
        int connected_fd = -1;
        for (addrinfo* rp = res; rp != nullptr && connected_fd < 0; rp = rp->ai_next) {
            if (close_requested_.load(std::memory_order_acquire)) break;
            const int s = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
            if (s < 0) continue;

            const int flags = ::fcntl(s, F_GETFL, 0);
            if (flags >= 0) ::fcntl(s, F_SETFL, flags | O_NONBLOCK);
            int one = 1;
            ::setsockopt(s, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

            const int rc = ::connect(s, rp->ai_addr, rp->ai_addrlen);
            if (rc == 0) {
                connected_fd = s;
                break;
            }
            if (errno != EINPROGRESS) {
                ::close(s);
                continue;
            }
            while (!close_requested_.load(std::memory_order_acquire)) {
                const uint64_t now = steady_ms();
                if (now >= deadline) break;
                const int wait_ms = int(std::min<uint64_t>(50, deadline - now));
                pollfd pfd{s, POLLOUT, 0};
                const int pr = ::poll(&pfd, 1, wait_ms);
                if (pr > 0) {
                    int so_err = 0;
                    socklen_t slen = sizeof(so_err);
                    if (::getsockopt(s, SOL_SOCKET, SO_ERROR, &so_err, &slen) == 0 && so_err == 0) {
                        connected_fd = s;
                    }
                    break;
                }
                if (pr < 0 && errno != EINTR) break;
            }
            if (connected_fd < 0) ::close(s);
        }
        ::freeaddrinfo(res);
        fd_ = connected_fd;
        return fd_ >= 0;
    }

    bool setup_tls(const ParsedWsUrl& parsed) {
        tls_ = true;
        if (!g_ssl_usable) {
            printf("[SwitchNet] TLS requested but ssl service is unavailable\n");
            return false;
        }
        ::Result rc = sslCreateContext(&ssl_ctx_, SslVersion_Auto | SslVersion_TlsV13);
        if (R_FAILED(rc)) rc = sslCreateContext(&ssl_ctx_, SslVersion_Auto);
        if (R_FAILED(rc)) {
            printf("[SwitchNet] sslCreateContext failed: 0x%08X\n", rc);
            return false;
        }
        ssl_ctx_open_ = true;

        rc = sslContextCreateConnection(&ssl_ctx_, &ssl_conn_);
        if (R_FAILED(rc)) {
            printf("[SwitchNet] sslContextCreateConnection failed: 0x%08X\n", rc);
            return false;
        }
        ssl_conn_open_ = true;

        // Keep app ownership of fd_ so poll() and close() behave consistently.
        sslConnectionSetOption(&ssl_conn_, SslOptionType_DoNotCloseSocket, true);
        if (socketSslConnectionSetSocketDescriptor(&ssl_conn_, fd_) < 0 && errno != ENOENT) {
            printf("[SwitchNet] socketSslConnectionSetSocketDescriptor failed: errno=%d\n", errno);
            return false;
        }

        rc = sslConnectionSetHostName(&ssl_conn_, parsed.host.c_str(), u32(parsed.host.size()));
        if (R_FAILED(rc)) {
            printf("[SwitchNet] sslConnectionSetHostName failed: 0x%08X\n", rc);
            return false;
        }
        rc = sslConnectionSetVerifyOption(&ssl_conn_, SslVerifyOption_PeerCa | SslVerifyOption_HostName);
        if (R_FAILED(rc)) {
            printf("[SwitchNet] sslConnectionSetVerifyOption failed: 0x%08X\n", rc);
            return false;
        }
        sslConnectionSetIoMode(&ssl_conn_, SslIoMode_NonBlocking);

        const uint64_t deadline = steady_ms() + kConnectTimeoutMs;
        while (!close_requested_.load(std::memory_order_acquire)) {
            rc = sslConnectionDoHandshake(&ssl_conn_, nullptr, nullptr, nullptr, 0);
            if (R_SUCCEEDED(rc)) return true;
            if (!is_ssl_would_block(rc)) {
                printf("[SwitchNet] TLS handshake failed: 0x%08X\n", rc);
                return false;
            }
            if (steady_ms() >= deadline) {
                printf("[SwitchNet] TLS handshake timeout\n");
                return false;
            }
            u32 out_ev = 0;
            sslConnectionPoll(&ssl_conn_, SslPollEvent_Read | SslPollEvent_Write, &out_ev, 50);
        }
        return false;
    }

    bool perform_ws_handshake(const ParsedWsUrl& parsed) {
        uint8_t nonce[16]{};
        fill_random(nonce, sizeof(nonce));
        const std::string key = base64_encode(nonce, sizeof(nonce));
        const auto accept_sha = sha1_digest(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
        const std::string expected_accept = base64_encode(accept_sha.data(), accept_sha.size());

        const std::string req = "GET " + parsed.path + " HTTP/1.1\r\n" +
                                "Host: " + parsed.host_header + "\r\n" +
                                "Upgrade: websocket\r\n" +
                                "Connection: Upgrade\r\n" +
                                "Sec-WebSocket-Key: " + key + "\r\n" +
                                "Sec-WebSocket-Version: 13\r\n\r\n";

        const uint64_t deadline = steady_ms() + kConnectTimeoutMs;
        if (!write_all(reinterpret_cast<const uint8_t*>(req.data()), req.size(), deadline)) {
            printf("[SwitchNet] WebSocket HTTP upgrade write failed\n");
            return false;
        }

        std::string raw;
        uint8_t tmp[2048];
        size_t hdr_end = std::string::npos;
        while (!close_requested_.load(std::memory_order_acquire)) {
            size_t got = 0;
            const IoOutcome res = io_read(tmp, sizeof(tmp), got);
            if (res == IoOutcome::ok) {
                raw.append(reinterpret_cast<const char*>(tmp), got);
                hdr_end = raw.find("\r\n\r\n");
                if (hdr_end != std::string::npos) break;
                if (raw.size() > kMaxHeaderBytes) return false;
                continue;
            }
            if (res != IoOutcome::would_block) return false;
            if (steady_ms() >= deadline) return false;
            pollfd pfd{fd_, POLLIN, 0};
            ::poll(&pfd, 1, 20);
        }
        if (hdr_end == std::string::npos) return false;

        const std::string headers = raw.substr(0, hdr_end);
        const size_t body_start = hdr_end + 4;
        if (body_start < raw.size()) {
            rx_buf_.insert(rx_buf_.end(), raw.begin() + ptrdiff_t(body_start), raw.end());
        }

        const size_t line_end = headers.find("\r\n");
        const std::string status_line = line_end == std::string::npos ? headers : headers.substr(0, line_end);
        if (status_line.find(" 101") == std::string::npos) {
            printf("[SwitchNet] WebSocket upgrade rejected: %s\n", status_line.c_str());
            return false;
        }

        std::string server_accept;
        size_t cur = line_end == std::string::npos ? headers.size() : line_end + 2;
        while (cur < headers.size()) {
            const size_t next = headers.find("\r\n", cur);
            const std::string line =
                next == std::string::npos ? headers.substr(cur) : headers.substr(cur, next - cur);
            cur = next == std::string::npos ? headers.size() : next + 2;
            const size_t colon = line.find(':');
            if (colon == std::string::npos) continue;
            if (ascii_lower(trim_ascii(line.substr(0, colon))) == "sec-websocket-accept") {
                server_accept = trim_ascii(line.substr(colon + 1));
                break;
            }
        }
        if (server_accept != expected_accept) {
            printf("[SwitchNet] Invalid Sec-WebSocket-Accept header\n");
            return false;
        }
        return true;
    }

    bool drain_rx_frames() {
        size_t offset = 0;
        bool fatal = false;
        while (rx_buf_.size() - offset >= 2) {
            const uint8_t b0 = rx_buf_[offset];
            const uint8_t b1 = rx_buf_[offset + 1];
            const bool fin = (b0 & 0x80u) != 0;
            const uint8_t rsv = uint8_t(b0 & 0x70u);
            const uint8_t opcode = uint8_t(b0 & 0x0fu);
            const bool masked = (b1 & 0x80u) != 0;
            uint64_t plen = uint64_t(b1 & 0x7fu);
            size_t hdr = 2;

            if (rsv != 0 || masked) {
                fatal = true;
                break;
            }
            if (plen == 126) {
                if (rx_buf_.size() - offset < 4) break;
                plen = (uint64_t(rx_buf_[offset + 2]) << 8) | uint64_t(rx_buf_[offset + 3]);
                hdr = 4;
            } else if (plen == 127) {
                if (rx_buf_.size() - offset < 10) break;
                plen = 0;
                for (int i = 0; i < 8; ++i) plen = (plen << 8) | uint64_t(rx_buf_[offset + 2 + i]);
                hdr = 10;
            }

            if ((opcode & 0x08u) != 0) {
                if (!fin || plen > 125) {
                    fatal = true;
                    break;
                }
            } else if (plen > max_message_bytes) {
                fatal = true;
                break;
            }

            if (rx_buf_.size() - offset < hdr + plen) break;

            const uint8_t* payload = rx_buf_.data() + offset + hdr;
            if (opcode == 0x8u) {
                // Peer closed cleanly.
                send_ws_frame(0x8u, nullptr, 0);
                offset += hdr + size_t(plen);
                rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + ptrdiff_t(offset));
                status_.store(ChannelStatus::closed, std::memory_order_release);
                return false;
            }
            if (opcode == 0x9u) {
                if (!send_ws_frame(0xAu, payload, size_t(plen))) {
                    fatal = true;
                    break;
                }
            } else if (opcode == 0xAu) {
                // Pong ignored.
            } else if (opcode == 0x1u || opcode == 0x2u || opcode == 0x0u) {
                if (opcode != 0x0u) {
                    if (frag_active_) {
                        fatal = true;
                        break;
                    }
                    frag_opcode_ = opcode;
                    frag_buf_.clear();
                    frag_active_ = true;
                } else if (!frag_active_) {
                    fatal = true;
                    break;
                }

                if (frag_buf_.size() + size_t(plen) > max_message_bytes) {
                    fatal = true;
                    break;
                }
                if (frag_opcode_ == 0x2u) {
                    frag_buf_.insert(frag_buf_.end(), payload, payload + size_t(plen));
                }
                if (fin) {
                    if (frag_opcode_ == 0x2u) {
                        std::lock_guard lock(rx_mu_);
                        if (rx_queue_.size() >= kMaxQueuedFrames) {
                            fatal = true;
                            break;
                        }
                        rx_queue_.push_back(std::move(frag_buf_));
                    }
                    frag_buf_.clear();
                    frag_active_ = false;
                }
            } else {
                fatal = true;
                break;
            }

            offset += hdr + size_t(plen);
        }

        if (offset > 0) rx_buf_.erase(rx_buf_.begin(), rx_buf_.begin() + ptrdiff_t(offset));
        if (fatal) {
            status_.store(ChannelStatus::failed, std::memory_order_release);
            return false;
        }
        return true;
    }

    void close_transport() {
        if (ssl_conn_open_) {
            sslConnectionClose(&ssl_conn_);
            ssl_conn_open_ = false;
        }
        if (ssl_ctx_open_) {
            sslContextClose(&ssl_ctx_);
            ssl_ctx_open_ = false;
        }
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    void run() {
        ParsedWsUrl parsed;
        if (!parse_ws_url(url_, parsed) || !ensure_switch_net_services()) {
            status_.store(ChannelStatus::failed, std::memory_order_release);
            return;
        }

        if (!connect_tcp(parsed)) {
            status_.store(ChannelStatus::failed, std::memory_order_release);
            close_transport();
            return;
        }

        if (parsed.tls && !setup_tls(parsed)) {
            status_.store(ChannelStatus::failed, std::memory_order_release);
            close_transport();
            return;
        }

        if (!perform_ws_handshake(parsed)) {
            status_.store(ChannelStatus::failed, std::memory_order_release);
            close_transport();
            return;
        }

        if (close_requested_.load(std::memory_order_acquire)) {
            status_.store(ChannelStatus::closed, std::memory_order_release);
            close_transport();
            return;
        }

        status_.store(ChannelStatus::open, std::memory_order_release);
        uint64_t last_ping = steady_ms();
        uint8_t buf[16 * 1024];

        while (!close_requested_.load(std::memory_order_acquire) &&
               status_.load(std::memory_order_acquire) == ChannelStatus::open) {
            bool did_work = false;

            std::deque<std::vector<uint8_t>> batch;
            {
                std::lock_guard lock(tx_mu_);
                batch.swap(tx_queue_);
            }
            for (const auto& pkt : batch) {
                did_work = true;
                if (!send_ws_frame(0x2u, pkt.data(), pkt.size())) {
                    status_.store(ChannelStatus::failed, std::memory_order_release);
                    break;
                }
            }
            if (status_.load(std::memory_order_acquire) != ChannelStatus::open) break;

            const uint64_t now = steady_ms();
            if (now - last_ping >= kPingIntervalMs) {
                last_ping = now;
                if (!send_ws_frame(0x9u, nullptr, 0)) {
                    status_.store(ChannelStatus::failed, std::memory_order_release);
                    break;
                }
            }

            for (;;) {
                size_t got = 0;
                const IoOutcome res = io_read(buf, sizeof(buf), got);
                if (res == IoOutcome::ok) {
                    did_work = true;
                    rx_buf_.insert(rx_buf_.end(), buf, buf + got);
                    if (!drain_rx_frames()) break;
                    continue;
                }
                if (res == IoOutcome::would_block) break;
                status_.store(res == IoOutcome::eof ? ChannelStatus::closed : ChannelStatus::failed,
                              std::memory_order_release);
                break;
            }

            if (status_.load(std::memory_order_acquire) != ChannelStatus::open) break;
            if (!drain_rx_frames()) break;

            if (!did_work) {
                s32 pending = 0;
                if (!tls_ || R_FAILED(sslConnectionPending(&ssl_conn_, &pending)) || pending <= 0) {
                    pollfd pfd{fd_, POLLIN, 0};
                    ::poll(&pfd, 1, 2);
                }
            }
        }

        if (close_requested_.load(std::memory_order_acquire)) {
            if (fd_ >= 0) send_ws_frame(0x8u, nullptr, 0);
            const ChannelStatus cur = status_.load(std::memory_order_acquire);
            if (cur != ChannelStatus::failed) status_.store(ChannelStatus::closed, std::memory_order_release);
        }
        close_transport();
    }

    std::string url_;
    int fd_ = -1;
    bool tls_ = false;
    SslContext ssl_ctx_{};
    SslConnection ssl_conn_{};
    bool ssl_ctx_open_ = false;
    bool ssl_conn_open_ = false;

    std::atomic<ChannelStatus> status_{ChannelStatus::connecting};
    std::atomic<bool> close_requested_{false};

    std::mutex tx_mu_;
    std::deque<std::vector<uint8_t>> tx_queue_;

    std::mutex rx_mu_;
    std::deque<std::vector<uint8_t>> rx_queue_;

    std::vector<uint8_t> rx_buf_;
    std::vector<uint8_t> frag_buf_;
    uint8_t frag_opcode_ = 0;
    bool frag_active_ = false;

    std::thread io_thread_;
};

} // namespace

std::unique_ptr<Channel> ws_connect(const std::string& url) {
    return ws_connect(url, {});
}

std::unique_ptr<Channel> ws_connect(const std::string& url, RtcOptions) {
    return std::make_unique<WsSwitchChannel>(url);
}

} // namespace rr::net
