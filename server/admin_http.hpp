#pragma once
#include "health_http.hpp"
#include "room.hpp"

#include <filesystem>
#include <string>

namespace rr::netd {

struct AdminRouterConfig {
    std::string admin_token; // empty disables all /admin routes cleanly
    std::filesystem::path log_dir;
    std::string git_revision = "unknown";
};

// Pure HTTP route dispatcher for /net/health, /admin UI, and /admin/api/*.
HttpResponse handle_http_request(const HttpRequest& req, Server& server, uint64_t now_ms,
                                 const AdminRouterConfig& cfg);

} // namespace rr::netd
