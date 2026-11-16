#include "assets.hpp"
#include <SDL3/SDL.h>
#include <stdexcept>
#ifdef RR_WEB_BROWSER
#include "platform_web.hpp"
#endif

namespace rr {
Assets::Assets(std::string root) : root_(std::move(root)) {}

std::vector<uint8_t> Assets::read(const std::string& relative) const {
#ifdef RR_WEB_BROWSER
    if (root_ == "/assets") return web_read_asset(relative);
#endif
    std::string path = root_.empty() ? relative : (root_.back() == '/' || root_.back() == ':' ? root_ + relative : root_ + "/" + relative);
    size_t size = 0;
    void* bytes = SDL_LoadFile(path.c_str(), &size);
    if (!bytes) {
        // Fallback try with romfs:/ directly
        std::string alt = "romfs:/" + relative;
        bytes = SDL_LoadFile(alt.c_str(), &size);
        if (bytes) path = alt;
    }
    if (!bytes) {
        printf("[Assets::read] FAILED to read '%s' (root='%s', relative='%s', SDL_err='%s')\n",
               path.c_str(), root_.c_str(), relative.c_str(), SDL_GetError());
        throw std::runtime_error(path + ": " + SDL_GetError());
    }
    printf("[Assets::read] Loaded '%s' (%zu bytes)\n", path.c_str(), size);
    auto start = static_cast<const uint8_t*>(bytes);
    std::vector<uint8_t> result(start, start + size);
    SDL_free(bytes);
    return result;
}

bool Assets::exists(const std::string& relative) const {
#ifdef RR_WEB_BROWSER
    if (root_ == "/assets") return web_asset_exists(relative);
#endif
    std::string path = root_.empty() ? relative : (root_.back() == '/' || root_.back() == ':' ? root_ + relative : root_ + "/" + relative);
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "rb");
    if (!io) {
        std::string alt = "romfs:/" + relative;
        io = SDL_IOFromFile(alt.c_str(), "rb");
    }
    if (!io) {
        printf("[Assets::exists] Check '%s': NOT FOUND\n", path.c_str());
        return false;
    }
    printf("[Assets::exists] Check '%s': FOUND\n", path.c_str());
    SDL_CloseIO(io);
    return true;
}
} // namespace rr
