#include "admin_http.hpp"
#include "admin_ui.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <vector>

namespace rr::netd {
namespace {

bool constant_time_equal(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char diff = 0;
    for (size_t i = 0; i < a.size(); ++i) diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    return diff == 0;
}

std::string extract_cookie(const std::string& header, const std::string& name) {
    const std::string prefix = name + "=";
    size_t pos = 0;
    while (pos < header.size()) {
        while (pos < header.size() && (header[pos] == ' ' || header[pos] == ';')) ++pos;
        size_t semi = header.find(';', pos);
        const std::string part = semi == std::string::npos ? header.substr(pos) : header.substr(pos, semi - pos);
        if (part.rfind(prefix, 0) == 0) return part.substr(prefix.size());
        if (semi == std::string::npos) break;
        pos = semi + 1;
    }
    return {};
}

bool authorized(const HttpRequest& req, const std::string& token) {
    if (token.empty()) return false;
    const std::string bearer = "Bearer ";
    if (req.authorization.rfind(bearer, 0) == 0 && constant_time_equal(req.authorization.substr(bearer.size()), token))
        return true;
    return constant_time_equal(extract_cookie(req.cookie, "rr_admin"), token);
}

std::optional<std::string> json_string(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return std::nullopt;
    p = body.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || body[p] != '"') return std::nullopt;
    ++p;
    std::string out;
    while (p < body.size() && body[p] != '"') {
        if (body[p] == '\\' && p + 1 < body.size()) ++p;
        out.push_back(body[p++]);
    }
    return p < body.size() ? std::optional<std::string>(std::move(out)) : std::nullopt;
}

std::optional<int64_t> json_int(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return std::nullopt;
    p = body.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos || (!std::isdigit(static_cast<unsigned char>(body[p])) && body[p] != '-'))
        return std::nullopt;
    char* end = nullptr;
    const long long val = std::strtoll(body.c_str() + p, &end, 10);
    return end != body.c_str() + p ? std::optional<int64_t>(val) : std::nullopt;
}

std::optional<bool> json_bool(const std::string& body, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t p = body.find(needle);
    if (p == std::string::npos) return std::nullopt;
    p = body.find(':', p + needle.size());
    if (p == std::string::npos) return std::nullopt;
    p = body.find_first_not_of(" \t\r\n", p + 1);
    if (p == std::string::npos) return std::nullopt;
    if (body.compare(p, 4, "true") == 0) return true;
    if (body.compare(p, 5, "false") == 0) return false;
    return std::nullopt;
}

bool safe_rrlog_name(const std::string& name) {
    if (name.empty() || name.size() > 128 || name.find("..") != std::string::npos ||
        name.find('/') != std::string::npos || name.find('\\') != std::string::npos)
        return false;
    for (unsigned char c : name) {
        if (!std::isalnum(c) && c != '.' && c != '-' && c != '_') return false;
    }
    return name.size() > 6 && name.compare(name.size() - 6, 6, ".rrlog") == 0;
}

std::string json_err(const char* msg) {
    return std::string("{\"ok\":false,\"error\":\"") + msg + "\"}";
}

} // namespace

HttpResponse handle_http_request(const HttpRequest& req, Server& server, uint64_t now_ms,
                                 const AdminRouterConfig& cfg) {
    if (req.method == "GET" && (req.path == "/net/health" || req.path == "/health")) {
        std::string body = server.health_json(now_ms);
        body.insert(body.size() - 1, std::string(",\"rev\":\"") + cfg.git_revision + "\"");
        return HttpResponse{200, "application/json", {}, body};
    }
    if (req.method == "GET" && (req.path == "/" || req.path == "/admin" || req.path == "/admin/")) {
        return HttpResponse{200, "text/html; charset=utf-8", {}, kAdminUiHtml};
    }
    if (req.path.rfind("/admin/api/", 0) != 0) {
        return HttpResponse{404, "application/json", {}, json_err("not found")};
    }
    if (cfg.admin_token.empty()) {
        return HttpResponse{403, "application/json", {}, json_err("admin disabled: set RR_NETD_ADMIN_TOKEN")};
    }
    if (req.path == "/admin/api/login") {
        if (req.method != "POST") return HttpResponse{405, "application/json", {}, json_err("method not allowed")};
        const auto tok = json_string(req.body, "token");
        if (!tok || !constant_time_equal(*tok, cfg.admin_token))
            return HttpResponse{401, "application/json", {}, json_err("invalid token")};
        return HttpResponse{200, "application/json",
                            "Set-Cookie: rr_admin=" + cfg.admin_token + "; Path=/; HttpOnly; SameSite=Strict\r\n",
                            "{\"ok\":true}"};
    }
    if (req.path == "/admin/api/logout") {
        if (req.method != "POST") return HttpResponse{405, "application/json", {}, json_err("method not allowed")};
        return HttpResponse{200, "application/json",
                            "Set-Cookie: rr_admin=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict\r\n",
                            "{\"ok\":true}"};
    }
    if (!authorized(req, cfg.admin_token)) {
        return HttpResponse{401, "application/json", {}, json_err("unauthorized")};
    }

    if (req.method == "GET" && req.path == "/admin/api/state") {
        std::string body = server.admin_state_json(now_ms);
        body.insert(body.size() - 1, std::string(",\"rev\":\"") + cfg.git_revision + "\"");
        return HttpResponse{200, "application/json", {}, body};
    }
    if (req.method == "POST" && req.path == "/admin/api/kick") {
        const auto id = json_int(req.body, "conn_id");
        if (!id || *id <= 0) return HttpResponse{400, "application/json", {}, json_err("invalid conn_id")};
        const bool ok = server.admin_kick_connection(uint32_t(*id), now_ms);
        return HttpResponse{ok ? 200 : 404, "application/json", {}, ok ? "{\"ok\":true}" : json_err("not found")};
    }
    if (req.method == "POST" && req.path == "/admin/api/room/close") {
        const auto code = json_string(req.body, "code");
        if (!code) return HttpResponse{400, "application/json", {}, json_err("missing code")};
        const bool ok = server.admin_close_room(*code, now_ms);
        return HttpResponse{ok ? 200 : 404, "application/json", {}, ok ? "{\"ok\":true}" : json_err("room not found")};
    }
    if (req.method == "POST" && req.path == "/admin/api/room/broadcast") {
        const auto code = json_string(req.body, "code");
        const auto text = json_string(req.body, "text");
        if (!code || !text) return HttpResponse{400, "application/json", {}, json_err("missing code or text")};
        const bool ok = server.admin_broadcast_room(*code, *text, now_ms);
        return HttpResponse{ok ? 200 : 404, "application/json", {}, ok ? "{\"ok\":true}" : json_err("room not found")};
    }
    if (req.method == "POST" && req.path == "/admin/api/config") {
        AdminConfigPatch patch;
        patch.switch_only = json_bool(req.body, "switch_only");
        patch.maintenance_mode = json_bool(req.body, "maintenance_mode");
        if (auto mh = json_int(req.body, "min_humans")) patch.min_humans = int(*mh);
        if (auto mr = json_int(req.body, "max_rooms")) patch.max_rooms = int(*mr);
        server.admin_update_config(patch, now_ms);
        return HttpResponse{200, "application/json", {}, "{\"ok\":true}"};
    }
    if (req.method == "GET" && req.path == "/admin/api/logs") {
        std::vector<std::pair<std::string, uintmax_t>> items;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(cfg.log_dir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            const std::string name = entry.path().filename().string();
            if (safe_rrlog_name(name)) items.emplace_back(name, entry.file_size(ec));
        }
        std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
        if (items.size() > 100) items.resize(100);
        std::ostringstream o;
        o << "{\"ok\":true,\"logs\":[";
        for (size_t i = 0; i < items.size(); ++i)
            o << (i ? "," : "") << "{\"name\":\"" << items[i].first << "\",\"size\":" << items[i].second << "}";
        o << "]}";
        return HttpResponse{200, "application/json", {}, o.str()};
    }
    const std::string log_prefix = "/admin/api/logs/";
    if (req.method == "GET" && req.path.rfind(log_prefix, 0) == 0) {
        const std::string file = req.path.substr(log_prefix.size());
        if (!safe_rrlog_name(file)) return HttpResponse{400, "application/json", {}, json_err("invalid filename")};
        const auto p = cfg.log_dir / file;
        std::ifstream in(p, std::ios::binary);
        if (!in) return HttpResponse{404, "application/json", {}, json_err("log not found")};
        std::ostringstream ss;
        ss << in.rdbuf();
        return HttpResponse{200, "application/octet-stream",
                            "Content-Disposition: attachment; filename=\"" + file + "\"\r\n", ss.str()};
    }
    return HttpResponse{404, "application/json", {}, json_err("not found")};
}

} // namespace rr::netd
