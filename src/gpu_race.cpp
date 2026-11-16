#ifdef __SWITCH__
#include "gpu_race.hpp"
#include "bike.hpp"
#include "course_scene.hpp"
#include "fog.hpp"
#include "hud.hpp"
#include "lifecycle.hpp"
#include "post_chain.hpp"
#include "raster.hpp"
#include "raster_rgb.hpp"
#include "view.hpp"
#include <GLES2/gl2.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace rr {
namespace {

constexpr uint32_t kClearPx = 0x01000000u;

struct IndexedVert {
    float x, y;
    float u, v, q;
    float mode; // >= -0.25: flat palette index/255; -1.0: opaque tex; -2.0: keyed tex
    float fog;
};

struct RgbaVert {
    float x, y;
    float u, v;
    float r, g, b, a;
};

struct GlStateGuard {
    GLint fbo = 0;
    GLint prog = 0;
    GLint active_tex = GL_TEXTURE0;
    GLint tex0 = 0, tex1 = 0;
    GLint vbo = 0, ibo = 0;
    GLint unpack_align = 4;
    GLint viewport[4] = {};
    GLboolean blend = GL_FALSE;
    GLboolean scissor = GL_FALSE;
    GLboolean cull = GL_FALSE;
    GLboolean depth = GL_FALSE;
    GLint blend_src_rgb = GL_ONE, blend_dst_rgb = GL_ZERO;
    GLint blend_src_a = GL_ONE, blend_dst_a = GL_ZERO;
    GLint blend_eq_rgb = GL_FUNC_ADD, blend_eq_a = GL_FUNC_ADD;

    struct AttribState {
        GLint enabled = 0;
        GLint size = 4;
        GLint type = GL_FLOAT;
        GLint normalized = GL_FALSE;
        GLint stride = 0;
        GLint buf = 0;
        void* ptr = nullptr;
    };
    std::array<AttribState, 3> attribs{};

    GlStateGuard() {
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &active_tex);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex0);
        glActiveTexture(GL_TEXTURE1);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &tex1);
        glActiveTexture(active_tex);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &vbo);
        glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &ibo);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpack_align);
        glGetIntegerv(GL_VIEWPORT, viewport);
        blend = glIsEnabled(GL_BLEND);
        scissor = glIsEnabled(GL_SCISSOR_TEST);
        cull = glIsEnabled(GL_CULL_FACE);
        depth = glIsEnabled(GL_DEPTH_TEST);
        glGetIntegerv(GL_BLEND_SRC_RGB, &blend_src_rgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &blend_dst_rgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &blend_src_a);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &blend_dst_a);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &blend_eq_rgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &blend_eq_a);
        for (GLuint i = 0; i < 3; ++i) {
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &attribs[i].enabled);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_SIZE, &attribs[i].size);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_TYPE, &attribs[i].type);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &attribs[i].normalized);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &attribs[i].stride);
            glGetVertexAttribiv(i, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &attribs[i].buf);
            glGetVertexAttribPointerv(i, GL_VERTEX_ATTRIB_ARRAY_POINTER, &attribs[i].ptr);
        }
    }

    ~GlStateGuard() {
        for (GLuint i = 0; i < 3; ++i) {
            if (attribs[i].enabled)
                glEnableVertexAttribArray(i);
            else
                glDisableVertexAttribArray(i);
            glBindBuffer(GL_ARRAY_BUFFER, GLuint(attribs[i].buf));
            glVertexAttribPointer(i, attribs[i].size, GLenum(attribs[i].type),
                                  GLboolean(attribs[i].normalized), attribs[i].stride, attribs[i].ptr);
        }
        glBindBuffer(GL_ARRAY_BUFFER, GLuint(vbo));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, GLuint(ibo));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpack_align);
        glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
        if (blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (scissor) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        if (cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glBlendFuncSeparate(GLenum(blend_src_rgb), GLenum(blend_dst_rgb),
                            GLenum(blend_src_a), GLenum(blend_dst_a));
        glBlendEquationSeparate(GLenum(blend_eq_rgb), GLenum(blend_eq_a));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, GLuint(tex1));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, GLuint(tex0));
        glActiveTexture(GLenum(active_tex));
        glUseProgram(GLuint(prog));
        glBindFramebuffer(GL_FRAMEBUFFER, GLuint(fbo));
    }
};

GLuint compile_shader(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    if (!s) return 0;
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gpu_race] shader compile error: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link_program(const char* vs_src, const char* fs_src, const char* const* attribs, int n_attribs) {
    GLuint vs = compile_shader(GL_VERTEX_SHADER, vs_src);
    GLuint fs = compile_shader(GL_FRAGMENT_SHADER, fs_src);
    if (!vs || !fs) {
        if (vs) glDeleteShader(vs);
        if (fs) glDeleteShader(fs);
        return 0;
    }
    GLuint p = glCreateProgram();
    if (!p) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        return 0;
    }
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    for (int i = 0; i < n_attribs; ++i) glBindAttribLocation(p, GLuint(i), attribs[i]);
    glLinkProgram(p);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512] = {};
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[gpu_race] program link error: %s\n", log);
        glDeleteProgram(p);
        return 0;
    }
    return p;
}

const char* kIndexedVs = R"(
attribute vec2 a_pos;
attribute vec3 a_uvq;
attribute vec2 a_meta;
uniform vec2 u_inv_half;
varying vec3 v_uvq;
varying vec2 v_meta;
void main() {
    gl_Position = vec4(a_pos * u_inv_half - 1.0, 0.0, 1.0);
    v_uvq = a_uvq;
    v_meta = a_meta;
}
)";

const char* kIndexedFs = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D u_atlas;
uniform sampler2D u_palette;
uniform vec3 u_fog_color;
uniform float u_bilinear;
uniform vec2 u_atlas_size;
uniform vec2 u_inv_atlas_size;
varying vec3 v_uvq;
varying vec2 v_meta;
vec3 pal_rgb(float idx) {
    return texture2D(u_palette, vec2((idx * 255.0 + 0.5) / 256.0, 0.5)).rgb;
}
void main() {
    vec3 col;
    if (v_meta.x >= -0.25) {
        col = pal_rgb(v_meta.x);
    } else {
        vec2 uv = v_uvq.xy / v_uvq.z;
        if (u_bilinear < 0.5) {
            float idx = texture2D(u_atlas, uv).r;
            if (v_meta.x < -1.5 && idx < (0.5 / 255.0)) discard;
            col = pal_rgb(idx);
        } else {
            vec2 texel = uv * u_atlas_size - 0.5;
            vec2 f = fract(texel);
            vec2 base = (floor(texel) + 0.5) * u_inv_atlas_size;
            vec2 dx = vec2(u_inv_atlas_size.x, 0.0);
            vec2 dy = vec2(0.0, u_inv_atlas_size.y);
            float i00 = texture2D(u_atlas, base).r;
            float i10 = texture2D(u_atlas, base + dx).r;
            float i01 = texture2D(u_atlas, base + dy).r;
            float i11 = texture2D(u_atlas, base + dx + dy).r;
            float w00 = (1.0 - f.x) * (1.0 - f.y);
            float w10 = f.x * (1.0 - f.y);
            float w01 = (1.0 - f.x) * f.y;
            float w11 = f.x * f.y;
            if (v_meta.x < -1.5) {
                float a00 = i00 >= (0.5 / 255.0) ? w00 : 0.0;
                float a10 = i10 >= (0.5 / 255.0) ? w10 : 0.0;
                float a01 = i01 >= (0.5 / 255.0) ? w01 : 0.0;
                float a11 = i11 >= (0.5 / 255.0) ? w11 : 0.0;
                float asum = a00 + a10 + a01 + a11;
                if (asum < 0.5) discard;
                col = (pal_rgb(i00) * a00 + pal_rgb(i10) * a10 +
                       pal_rgb(i01) * a01 + pal_rgb(i11) * a11) / asum;
            } else {
                col = pal_rgb(i00) * w00 + pal_rgb(i10) * w10 +
                      pal_rgb(i01) * w01 + pal_rgb(i11) * w11;
            }
        }
    }
    if (v_meta.y > 0.0) {
        col = mix(col, u_fog_color, v_meta.y);
    }
    gl_FragColor = vec4(col, 1.0);
}
)";

const char* kPostVs = R"(
attribute vec2 a_pos;
attribute vec2 a_uv;
varying vec2 v_uv;
void main() {
    gl_Position = vec4(a_pos, 0.0, 1.0);
    v_uv = a_uv;
}
)";

#define GRADE_GLSL_FN \
"vec3 apply_grade(vec3 c, float g) {\n" \
"    if (g < 0.5) return c;\n" \
"    if (g < 1.5) {\n" \
"        c.r = min(1.0, c.r + (1.0 - c.r) * 0.08);\n" \
"        c.b *= 0.92;\n" \
"        return c;\n" \
"    }\n" \
"    if (g < 2.5) {\n" \
"        float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n" \
"        return clamp(vec3(lum) + (c - vec3(lum)) * 1.30, 0.0, 1.0);\n" \
"    }\n" \
"    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n" \
"    float w = lum;\n" \
"    float r = c.r + ((1.0 - c.r) * 0.10 - c.r * 0.06) * w - c.r * 0.06 * (1.0 - w);\n" \
"    float gg = c.g + ((1.0 - c.g) * 0.04 - c.g * 0.04) * w - c.g * 0.02 * (1.0 - w);\n" \
"    float b = c.b + (40.0 / 255.0 - c.b) * 0.10 * (1.0 - w);\n" \
"    return clamp(vec3(r, gg, b), 0.0, 1.0);\n" \
"}\n"

const char* kMbFs =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
"precision highp float;\n"
"#else\n"
"precision mediump float;\n"
"#endif\n"
"uniform sampler2D u_tex0;\n"
"uniform sampler2D u_tex1;\n"
"uniform float u_grade;\n"
"uniform float u_factor;\n"
"varying vec2 v_uv;\n"
GRADE_GLSL_FN
"void main() {\n"
"    vec3 c = apply_grade(texture2D(u_tex0, v_uv).rgb, u_grade);\n"
"    if (u_factor > 0.0) {\n"
"        vec3 p = texture2D(u_tex1, v_uv).rgb;\n"
"        c = clamp(mix(c, p, u_factor), 0.0, 1.0);\n"
"    }\n"
"    gl_FragColor = vec4(c, 1.0);\n"
"}\n";

const char* kBrightFs =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
"precision highp float;\n"
"#else\n"
"precision mediump float;\n"
"#endif\n"
"uniform sampler2D u_tex0;\n"
"uniform float u_grade;\n"
"uniform vec2 u_step;\n"
"varying vec2 v_uv;\n"
GRADE_GLSL_FN
"float sample_bright(vec2 uv) {\n"
"    vec3 c = apply_grade(texture2D(u_tex0, clamp(uv, 0.0, 1.0)).rgb, u_grade);\n"
"    float lum = dot(c, vec3(0.2126, 0.7152, 0.0722));\n"
"    return max(0.0, lum - 0.6);\n"
"}\n"
"void main() {\n"
"    float b = 0.25 * (sample_bright(v_uv + vec2(-u_step.x, -u_step.y)) +\n"
"                      sample_bright(v_uv + vec2( u_step.x, -u_step.y)) +\n"
"                      sample_bright(v_uv + vec2(-u_step.x,  u_step.y)) +\n"
"                      sample_bright(v_uv + vec2( u_step.x,  u_step.y)));\n"
"    gl_FragColor = vec4(b, b, b, 1.0);\n"
"}\n";

const char* kBlurFs = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D u_tex0;
uniform vec2 u_step;
varying vec2 v_uv;
void main() {
    float acc = 0.2 * (
        texture2D(u_tex0, clamp(v_uv - 2.0 * u_step, 0.0, 1.0)).r +
        texture2D(u_tex0, clamp(v_uv - 1.0 * u_step, 0.0, 1.0)).r +
        texture2D(u_tex0, v_uv).r +
        texture2D(u_tex0, clamp(v_uv + 1.0 * u_step, 0.0, 1.0)).r +
        texture2D(u_tex0, clamp(v_uv + 2.0 * u_step, 0.0, 1.0)).r);
    gl_FragColor = vec4(acc, acc, acc, 1.0);
}
)";

const char* kPostFs =
"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
"precision highp float;\n"
"#else\n"
"precision mediump float;\n"
"#endif\n"
"uniform sampler2D u_tex0;\n"
"uniform sampler2D u_tex1;\n"
"uniform float u_grade;\n"
"uniform float u_bloom;\n"
"uniform float u_vignette;\n"
"varying vec2 v_uv;\n"
GRADE_GLSL_FN
"void main() {\n"
"    vec3 c = apply_grade(texture2D(u_tex0, v_uv).rgb, u_grade);\n"
"    if (u_bloom > 0.5) {\n"
"        float b = texture2D(u_tex1, v_uv).r;\n"
"        c = min(vec3(1.0), c + vec3(b * (400.0 / 255.0)));\n"
"    }\n"
"    if (u_vignette > 0.5) {\n"
"        vec2 ndc = v_uv * 2.0 - 1.0;\n"
"        float d2 = dot(ndc, ndc) * 0.5;\n"
"        c *= max(0.0, 1.0 - 0.45 * d2);\n"
"    }\n"
"    gl_FragColor = vec4(c, 1.0);\n"
"}\n";

const char* kRgbaVs = R"(
attribute vec2 a_pos;
attribute vec2 a_uv;
attribute vec4 a_color;
uniform vec2 u_inv_half;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    gl_Position = vec4(a_pos * u_inv_half - 1.0, 0.0, 1.0);
    v_uv = a_uv;
    v_color = a_color;
}
)";

const char* kRgbaFs = R"(
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
uniform sampler2D u_tex;
uniform sampler2D u_fade_lut;
uniform float u_use_fade;
varying vec2 v_uv;
varying vec4 v_color;
void main() {
    vec4 c = texture2D(u_tex, v_uv) * v_color;
    if (c.a < 0.01) discard;
    if (u_use_fade > 0.5) {
        c.r = texture2D(u_fade_lut, vec2((c.r * 255.0 + 0.5) / 256.0, 0.5)).r;
        c.g = texture2D(u_fade_lut, vec2((c.g * 255.0 + 0.5) / 256.0, 0.5)).r;
        c.b = texture2D(u_fade_lut, vec2((c.b * 255.0 + 0.5) / 256.0, 0.5)).r;
    }
    gl_FragColor = c;
}
)";

GLuint make_nearest_tex() {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

GLuint make_linear_tex() {
    GLuint t = 0;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}

struct FboTarget {
    GLuint tex = 0;
    GLuint fbo = 0;
    int w = 0, h = 0;

    void destroy() {
        if (fbo) { glDeleteFramebuffers(1, &fbo); fbo = 0; }
        if (tex) { glDeleteTextures(1, &tex); tex = 0; }
        w = h = 0;
    }

    bool ensure(int want_w, int want_h, GLuint restore_fbo) {
        if (want_w <= 0 || want_h <= 0) return false;
        if (fbo && tex && w == want_w && h == want_h) return true;
        if (!tex) tex = make_linear_tex();
        if (!fbo) glGenFramebuffers(1, &fbo);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, want_w, want_h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        glBindFramebuffer(GL_FRAMEBUFFER, restore_fbo);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            destroy();
            return false;
        }
        w = want_w;
        h = want_h;
        return true;
    }
};

struct SeatPostCache {
    FboTarget scene;
    FboTarget hist[2];
    FboTarget bloom[2];
    int hist_idx = 0;
    bool hist_valid = false;

    void destroy() {
        scene.destroy();
        hist[0].destroy();
        hist[1].destroy();
        bloom[0].destroy();
        bloom[1].destroy();
        hist_idx = 0;
        hist_valid = false;
    }
};

int32_t high_product(int32_t a, int32_t b) {
    return int32_t((int64_t(a) * b) >> 32);
}

struct AtlasPage {
    GLuint tex = 0;
    int width = 0;
    int height = 0;
    int cur_x = 0;
    int cur_y = 0;
    int shelf_h = 0;
};

struct AtlasEntry {
    int page = -1;
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    bool valid = false;
};

struct DrawRun {
    int page = 0;
    size_t index_offset = 0;
    size_t index_count = 0;
};

struct RgbaDrawQuad {
    GLuint tex = 0;
    bool blend = false;
    RgbaVert v[4];
};

struct LabelSlot {
    GLuint tex = 0;
    int tex_w = 0, tex_h = 0;
    int draw_x = 0, draw_y = 0, draw_w = 0, draw_h = 0;
    std::string text;
    uint32_t color = 0;
    uint32_t flags = 0;
    int height = 0;
    bool active = false;
};

struct SeatHudCache {
    std::array<LabelSlot, 7> slots{}; // 0:player 1:place 2:dist 3:target 4:gap 5:arrow 6:banner
};

} // namespace

struct GpuRaceRenderer::Impl {
    bool init_tried = false;
    bool init_ok = false;
    SDL_Renderer* bound_renderer = nullptr;

    GLuint prog_indexed = 0;
    GLint u_idx_inv_half = -1;
    GLint u_idx_atlas = -1;
    GLint u_idx_palette = -1;
    GLint u_idx_fog_color = -1;
    GLint u_idx_bilinear = -1;
    GLint u_idx_atlas_size = -1;
    GLint u_idx_inv_atlas_size = -1;

    GLuint prog_rgba = 0;
    GLint u_rgba_inv_half = -1;
    GLint u_rgba_tex = -1;
    GLint u_rgba_fade_lut = -1;
    GLint u_rgba_use_fade = -1;

    GLuint prog_mb = 0;
    GLint u_mb_tex0 = -1, u_mb_tex1 = -1, u_mb_grade = -1, u_mb_factor = -1;

    GLuint prog_bright = 0;
    GLint u_br_tex0 = -1, u_br_grade = -1, u_br_step = -1;

    GLuint prog_blur = 0;
    GLint u_bl_tex0 = -1, u_bl_step = -1;

    GLuint prog_post = 0;
    GLint u_po_tex0 = -1, u_po_tex1 = -1, u_po_grade = -1, u_po_bloom = -1, u_po_vignette = -1;

    GLuint vbo = 0;
    GLuint ibo = 0;
    GLuint quad_vbo = 0;
    GLuint quad_ibo = 0;

    GLuint palette_tex = 0;
    uint32_t last_pal_gen = 0;
    ChannelLut last_pal_lut{};
    bool last_pal_remap = false;
    bool pal_valid = false;

    GLuint fade_lut_tex = 0;
    ChannelLut last_fade_lut{};
    bool fade_lut_valid = false;

    GLuint white_tex = 0;

    // Course-scoped caches
    const CourseScene* cached_scene = nullptr;
    GLuint horizon_tex = 0;
    int page_size = 2048;
    std::vector<AtlasPage> atlas_pages;
    std::unordered_map<const CelTexture*, AtlasEntry> atlas_map;

    // HUD-scoped caches
    const Hud* cached_hud = nullptr;
    bool cached_hud_low = false;
    GLuint dash_tex = 0;
    int dash_w = 0, dash_h = 0;
    GLuint meter_tex = 0;
    int meter_w = 0, meter_h = 0;

    std::unordered_map<const void*, SeatHudCache> seat_caches;
    std::unordered_map<const void*, SeatPostCache> post_caches;

    // Scratch per-frame buffers
    std::vector<IndexedVert> idx_verts;
    std::vector<uint16_t> idx_indices;
    std::vector<DrawRun> runs;
    std::vector<uint8_t> scratch_pad;
    std::vector<uint8_t> scratch_rgba;
    std::vector<RgbaDrawQuad> rgba_quads;

    ~Impl() { destroy_gl(); }

    void destroy_scene_gl() {
        if (horizon_tex) {
            glDeleteTextures(1, &horizon_tex);
            horizon_tex = 0;
        }
        for (auto& p : atlas_pages) {
            if (p.tex) glDeleteTextures(1, &p.tex);
        }
        atlas_pages.clear();
        atlas_map.clear();
        cached_scene = nullptr;
    }

    void destroy_hud_gl() {
        if (dash_tex) { glDeleteTextures(1, &dash_tex); dash_tex = 0; }
        if (meter_tex) { glDeleteTextures(1, &meter_tex); meter_tex = 0; }
        for (auto& kv : seat_caches) {
            for (auto& sl : kv.second.slots) {
                if (sl.tex) glDeleteTextures(1, &sl.tex);
            }
        }
        seat_caches.clear();
        cached_hud = nullptr;
    }

    void destroy_post_gl() {
        for (auto& kv : post_caches) kv.second.destroy();
        post_caches.clear();
    }

    void destroy_gl() {
        if (!init_ok) return;
        destroy_scene_gl();
        destroy_hud_gl();
        destroy_post_gl();
        if (palette_tex) { glDeleteTextures(1, &palette_tex); palette_tex = 0; }
        if (fade_lut_tex) { glDeleteTextures(1, &fade_lut_tex); fade_lut_tex = 0; }
        if (white_tex) { glDeleteTextures(1, &white_tex); white_tex = 0; }
        if (vbo) { glDeleteBuffers(1, &vbo); vbo = 0; }
        if (ibo) { glDeleteBuffers(1, &ibo); ibo = 0; }
        if (quad_vbo) { glDeleteBuffers(1, &quad_vbo); quad_vbo = 0; }
        if (quad_ibo) { glDeleteBuffers(1, &quad_ibo); quad_ibo = 0; }
        if (prog_indexed) { glDeleteProgram(prog_indexed); prog_indexed = 0; }
        if (prog_rgba) { glDeleteProgram(prog_rgba); prog_rgba = 0; }
        if (prog_mb) { glDeleteProgram(prog_mb); prog_mb = 0; }
        if (prog_bright) { glDeleteProgram(prog_bright); prog_bright = 0; }
        if (prog_blur) { glDeleteProgram(prog_blur); prog_blur = 0; }
        if (prog_post) { glDeleteProgram(prog_post); prog_post = 0; }
        init_ok = false;
    }

    bool ensure_init(SDL_Renderer* renderer) {
        if (init_tried) return init_ok && bound_renderer == renderer;
        init_tried = true;
        bound_renderer = renderer;

        SDL_RendererInfo info{};
        if (SDL_GetRendererInfo(renderer, &info) != 0 || !info.name ||
            std::strstr(info.name, "opengles2") == nullptr) {
            std::fprintf(stderr, "[gpu_race] renderer '%s' is not opengles2; using CPU fallback\n",
                         info.name ? info.name : "null");
            return false;
        }
        if (!SDL_GL_GetCurrentContext()) {
            std::fprintf(stderr, "[gpu_race] no current GLES2 context; using CPU fallback\n");
            return false;
        }

        GlStateGuard guard;
        GLint max_tex = 2048;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &max_tex);
        page_size = std::clamp(int(max_tex), 512, 2048);

        const char* idx_attribs[] = {"a_pos", "a_uvq", "a_meta"};
        prog_indexed = link_program(kIndexedVs, kIndexedFs, idx_attribs, 3);
        const char* rgba_attribs[] = {"a_pos", "a_uv", "a_color"};
        prog_rgba = link_program(kRgbaVs, kRgbaFs, rgba_attribs, 3);
        const char* post_attribs[] = {"a_pos", "a_uv"};
        prog_mb = link_program(kPostVs, kMbFs, post_attribs, 2);
        prog_bright = link_program(kPostVs, kBrightFs, post_attribs, 2);
        prog_blur = link_program(kPostVs, kBlurFs, post_attribs, 2);
        prog_post = link_program(kPostVs, kPostFs, post_attribs, 2);
        if (!prog_indexed || !prog_rgba || !prog_mb || !prog_bright || !prog_blur || !prog_post) {
            destroy_gl();
            return false;
        }

        u_idx_inv_half = glGetUniformLocation(prog_indexed, "u_inv_half");
        u_idx_atlas = glGetUniformLocation(prog_indexed, "u_atlas");
        u_idx_palette = glGetUniformLocation(prog_indexed, "u_palette");
        u_idx_fog_color = glGetUniformLocation(prog_indexed, "u_fog_color");
        u_idx_bilinear = glGetUniformLocation(prog_indexed, "u_bilinear");
        u_idx_atlas_size = glGetUniformLocation(prog_indexed, "u_atlas_size");
        u_idx_inv_atlas_size = glGetUniformLocation(prog_indexed, "u_inv_atlas_size");

        u_rgba_inv_half = glGetUniformLocation(prog_rgba, "u_inv_half");
        u_rgba_tex = glGetUniformLocation(prog_rgba, "u_tex");
        u_rgba_fade_lut = glGetUniformLocation(prog_rgba, "u_fade_lut");
        u_rgba_use_fade = glGetUniformLocation(prog_rgba, "u_use_fade");

        u_mb_tex0 = glGetUniformLocation(prog_mb, "u_tex0");
        u_mb_tex1 = glGetUniformLocation(prog_mb, "u_tex1");
        u_mb_grade = glGetUniformLocation(prog_mb, "u_grade");
        u_mb_factor = glGetUniformLocation(prog_mb, "u_factor");

        u_br_tex0 = glGetUniformLocation(prog_bright, "u_tex0");
        u_br_grade = glGetUniformLocation(prog_bright, "u_grade");
        u_br_step = glGetUniformLocation(prog_bright, "u_step");

        u_bl_tex0 = glGetUniformLocation(prog_blur, "u_tex0");
        u_bl_step = glGetUniformLocation(prog_blur, "u_step");

        u_po_tex0 = glGetUniformLocation(prog_post, "u_tex0");
        u_po_tex1 = glGetUniformLocation(prog_post, "u_tex1");
        u_po_grade = glGetUniformLocation(prog_post, "u_grade");
        u_po_bloom = glGetUniformLocation(prog_post, "u_bloom");
        u_po_vignette = glGetUniformLocation(prog_post, "u_vignette");

        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        glGenBuffers(1, &quad_vbo);
        glGenBuffers(1, &quad_ibo);
        const float qverts[16] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
        };
        const uint16_t qinds[6] = {0, 1, 2, 0, 2, 3};
        glBindBuffer(GL_ARRAY_BUFFER, quad_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(qverts), qverts, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, quad_ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(qinds), qinds, GL_STATIC_DRAW);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

        palette_tex = make_nearest_tex();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        fade_lut_tex = make_nearest_tex();
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

        white_tex = make_nearest_tex();
        const uint8_t white[4] = {255, 255, 255, 255};
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);

        init_ok = true;
        std::printf("[gpu_race] GLES2 GPU race renderer active (max_tex=%d, atlas=%dx%d)\n",
                    int(max_tex), page_size, page_size);
        std::fflush(stdout);
        return true;
    }

    const AtlasEntry* get_or_upload_cel(const CelTexture* tex) {
        auto it = atlas_map.find(tex);
        if (it != atlas_map.end()) return it->second.valid ? &it->second : nullptr;

        AtlasEntry entry;
        if (!tex || tex->mips.empty()) {
            atlas_map.emplace(tex, entry);
            return nullptr;
        }
        const CelMip& mip = tex->mips[0];
        if (mip.width <= 0 || mip.height <= 0 || mip.pixels.empty()) {
            atlas_map.emplace(tex, entry);
            return nullptr;
        }
        const int w = mip.width;
        const int h = mip.height;
        const int pw = w + 2;
        const int ph = h + 2;
        if (pw > page_size || ph > page_size) {
            atlas_map.emplace(tex, entry);
            return nullptr;
        }

        int target_page = -1;
        int ax = 0, ay = 0;
        for (int p = 0; p < int(atlas_pages.size()); ++p) {
            auto& pg = atlas_pages[size_t(p)];
            if (pg.cur_x + pw <= pg.width && pg.cur_y + ph <= pg.height) {
                target_page = p;
                ax = pg.cur_x;
                ay = pg.cur_y;
                pg.cur_x += pw;
                pg.shelf_h = std::max(pg.shelf_h, ph);
                break;
            }
            if (pg.cur_y + pg.shelf_h + ph <= pg.height && pw <= pg.width) {
                target_page = p;
                pg.cur_y += pg.shelf_h;
                pg.cur_x = pw;
                pg.shelf_h = ph;
                ax = 0;
                ay = pg.cur_y;
                break;
            }
        }
        if (target_page < 0) {
            AtlasPage pg;
            pg.width = page_size;
            pg.height = page_size;
            pg.tex = make_nearest_tex();
            glBindTexture(GL_TEXTURE_2D, pg.tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, pg.width, pg.height, 0, GL_LUMINANCE,
                         GL_UNSIGNED_BYTE, nullptr);
            pg.cur_x = pw;
            pg.cur_y = 0;
            pg.shelf_h = ph;
            target_page = int(atlas_pages.size());
            atlas_pages.push_back(pg);
            ax = 0;
            ay = 0;
        }

        // Build 1-texel clamp border around mip to avoid edge leaks.
        scratch_pad.resize(size_t(pw) * size_t(ph));
        const int src_stride = 1 << mip.stride_shift;
        const auto src_row = [&](int y) -> const uint8_t* {
            const size_t off = size_t(std::clamp(y, 0, h - 1)) * size_t(src_stride);
            return off + size_t(w) <= mip.pixels.size() ? &mip.pixels[off] : nullptr;
        };
        for (int y = -1; y <= h; ++y) {
            uint8_t* dst = &scratch_pad[size_t(y + 1) * size_t(pw)];
            const uint8_t* row = src_row(y);
            if (!row) {
                std::memset(dst, 0, size_t(pw));
                continue;
            }
            dst[0] = row[0];
            std::memcpy(dst + 1, row, size_t(w));
            dst[w + 1] = row[w - 1];
        }

        glBindTexture(GL_TEXTURE_2D, atlas_pages[size_t(target_page)].tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, ax, ay, pw, ph, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                        scratch_pad.data());

        entry.page = target_page;
        entry.x = ax + 1;
        entry.y = ay + 1;
        entry.w = w;
        entry.h = h;
        entry.valid = true;
        atlas_map.emplace(tex, entry);
        return &atlas_map[tex];
    }

    void sync_scene(const CourseScene* scene) {
        if (cached_scene == scene) return;
        destroy_scene_gl();
        cached_scene = scene;
        if (!scene) return;
        const auto& hbytes = scene->horizon_bytes();
        if (hbytes.size() == size_t(720) * 320) {
            horizon_tex = make_nearest_tex();
            glBindTexture(GL_TEXTURE_2D, horizon_tex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, 720, 320, 0, GL_LUMINANCE, GL_UNSIGNED_BYTE,
                         hbytes.data());
        }
    }

    void upload_rgba_indexed(GLuint& tex, const std::vector<uint8_t>& idx, int w, int h,
                             const Palette& pal, bool key_zero) {
        if (!tex) tex = make_nearest_tex();
        scratch_rgba.resize(size_t(w) * size_t(h) * 4);
        for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {
            const uint8_t v = i < idx.size() ? idx[i] : 0;
            const uint8_t* c = &pal.rgbx[4 * size_t(v)];
            scratch_rgba[4 * i + 0] = c[0];
            scratch_rgba[4 * i + 1] = c[1];
            scratch_rgba[4 * i + 2] = c[2];
            scratch_rgba[4 * i + 3] = (key_zero && v == 0) ? 0 : 255;
        }
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, scratch_rgba.data());
    }

    void sync_hud(const Hud* hud) {
        if (cached_hud == hud && (!hud || cached_hud_low == hud->is_low())) return;
        destroy_hud_gl();
        cached_hud = hud;
        if (!hud) return;
        cached_hud_low = hud->is_low();

        const auto& dash = hud->dashboard_art();
        dash_w = hud->dashboard_stride();
        dash_h = dash_w > 0 ? int(dash.size()) / dash_w : 0;
        if (dash_w > 0 && dash_h > 0)
            upload_rgba_indexed(dash_tex, dash, dash_w, dash_h, hud->palette(), true);

        const auto& meter = hud->meter_art();
        meter_w = hud->meter_stride();
        meter_h = meter_w > 0 ? int(meter.size()) / meter_w : 0;
        if (meter_w > 0 && meter_h > 0)
            upload_rgba_indexed(meter_tex, meter, meter_w, meter_h, hud->palette(), false);
    }

    void sync_palette(const Palette& pal, const ChannelLut& lut, bool remap) {
        const uint32_t gen = palette_generation(pal);
        if (pal_valid && gen == last_pal_gen && remap == last_pal_remap && (!remap || lut == last_pal_lut)) {
            return;
        }
        uint8_t rgba[256 * 4];
        for (int i = 0; i < 256; ++i) {
            const uint8_t* c = &pal.rgbx[4 * size_t(i)];
            rgba[4 * i + 0] = remap ? lut[c[0]] : c[0];
            rgba[4 * i + 1] = remap ? lut[c[1]] : c[1];
            rgba[4 * i + 2] = remap ? lut[c[2]] : c[2];
            rgba[4 * i + 3] = 255;
        }
        glBindTexture(GL_TEXTURE_2D, palette_tex);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        last_pal_gen = gen;
        last_pal_lut = remap ? lut : identity_lut();
        last_pal_remap = remap;
        pal_valid = true;

        if (remap && (!fade_lut_valid || lut != last_fade_lut)) {
            uint8_t fl[256 * 4];
            for (int i = 0; i < 256; ++i) {
                fl[4 * i + 0] = lut[size_t(i)];
                fl[4 * i + 1] = lut[size_t(i)];
                fl[4 * i + 2] = lut[size_t(i)];
                fl[4 * i + 3] = 255;
            }
            glBindTexture(GL_TEXTURE_2D, fade_lut_tex);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 1, GL_RGBA, GL_UNSIGNED_BYTE, fl);
            last_fade_lut = lut;
            fade_lut_valid = true;
        }
    }

    void push_indexed_quad(int page, const IndexedVert (&q)[4]) {
        // Reserve 65532 max vertices per batch (16-bit index buffer limit in GLES2).
        if (!runs.empty() && idx_verts.size() + 4 > 65532) {
            // Unlikely (>16k quads in one frame); drop overflow gracefully rather than corrupting.
            return;
        }
        if (runs.empty() || (page >= 0 && runs.back().page != page)) {
            runs.push_back({page >= 0 ? page : 0, idx_indices.size(), 0});
        }
        const uint16_t base = uint16_t(idx_verts.size());
        for (int i = 0; i < 4; ++i) idx_verts.push_back(q[i]);
        const uint16_t tris[6] = {base, uint16_t(base + 1), uint16_t(base + 2),
                                  base, uint16_t(base + 2), uint16_t(base + 3)};
        for (uint16_t idx : tris) idx_indices.push_back(idx);
        runs.back().index_count += 6;
    }

    void upload_label_slot(LabelSlot& sl, const TextRenderer& tr, Face face, int font_h,
                           const TextRect& fg_rect, const TextRect& bg_rect, uint32_t fg_col,
                           uint32_t bg_col, const std::string& str, unsigned flags,
                           int max_w, int max_h) {
        if (str.empty()) {
            sl.active = false;
            return;
        }
        sl.active = true;
        if (sl.text == str && sl.color == fg_col && sl.flags == flags && sl.height == font_h && sl.tex)
            return;

        int bx0 = std::min(fg_rect.left, bg_rect.left);
        int by0 = std::min(fg_rect.top, bg_rect.top);
        int bx1 = std::max(fg_rect.right, bg_rect.right);
        int by1 = std::max(fg_rect.bottom, bg_rect.bottom);
        if (flags & dt::noclip) {
            bx0 = std::max(0, bx0 - 16);
            by0 = std::max(0, by0 - 8);
            bx1 = std::min(max_w, bx1 + 16);
            by1 = std::min(max_h, by1 + 8);
        }
        const int bw = std::max(1, bx1 - bx0);
        const int bh = std::max(1, by1 - by0);

        RgbImage img(bw, bh, kClearPx);
        TextRect lbg{bg_rect.left - bx0, bg_rect.top - by0, bg_rect.right - bx0, bg_rect.bottom - by0};
        TextRect lfg{fg_rect.left - bx0, fg_rect.top - by0, fg_rect.right - bx0, fg_rect.bottom - by0};
        tr.draw_box(&img, face, font_h, lbg, bg_col, str, flags);
        tr.draw_box(&img, face, font_h, lfg, fg_col, str, flags);

        scratch_rgba.resize(size_t(bw) * size_t(bh) * 4);
        for (size_t i = 0; i < size_t(bw) * size_t(bh); ++i) {
            const uint32_t c = img.pixels[i];
            if (c == kClearPx) {
                scratch_rgba[4 * i + 0] = 0;
                scratch_rgba[4 * i + 1] = 0;
                scratch_rgba[4 * i + 2] = 0;
                scratch_rgba[4 * i + 3] = 0;
            } else {
                scratch_rgba[4 * i + 0] = uint8_t((c >> 16) & 0xff);
                scratch_rgba[4 * i + 1] = uint8_t((c >> 8) & 0xff);
                scratch_rgba[4 * i + 2] = uint8_t(c & 0xff);
                scratch_rgba[4 * i + 3] = 255;
            }
        }
        if (!sl.tex) sl.tex = make_nearest_tex();
        glBindTexture(GL_TEXTURE_2D, sl.tex);
        if (sl.tex_w == bw && sl.tex_h == bh) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, bw, bh, GL_RGBA, GL_UNSIGNED_BYTE,
                            scratch_rgba.data());
        } else {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, bw, bh, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                         scratch_rgba.data());
            sl.tex_w = bw;
            sl.tex_h = bh;
        }
        sl.draw_x = bx0;
        sl.draw_y = by0;
        sl.draw_w = bw;
        sl.draw_h = bh;
        sl.text = str;
        sl.color = fg_col;
        sl.flags = flags;
        sl.height = font_h;
    }

    void push_rgba_rect(GLuint tex, bool blend, float dx, float dy, float dw, float dh,
                        float u0, float v0, float u1, float v1,
                        float r = 1.f, float g = 1.f, float b = 1.f, float a = 1.f) {
        RgbaDrawQuad q;
        q.tex = tex;
        q.blend = blend;
        q.v[0] = {dx, dy, u0, v0, r, g, b, a};
        q.v[1] = {dx + dw, dy, u1, v0, r, g, b, a};
        q.v[2] = {dx + dw, dy + dh, u1, v1, r, g, b, a};
        q.v[3] = {dx, dy + dh, u0, v1, r, g, b, a};
        rgba_quads.push_back(q);
    }

    void build_hud_quads(const void* seat_key, const Game& game, const CountdownBanner& banner,
                         int wide) {
        const Hud* hud = game.assets.hud.get();
        if (!hud || !banner.dashboard) return;
        sync_hud(hud);

        const int margin = (wide - 320) / 2;
        const float xoff = float(margin * 2);
        const bool low = hud->is_low();
        const float lscale = low ? 0.5f : 1.0f;
        const int dbl = low ? 2 : 1;
        const auto& layout = hud->layout(DashMode::full);
        const HudState st = hud->state(game.race(), game.player_name, game.rider_names);

        // 1. Meter bars (drawn beneath the dashboard art cutouts).
        auto add_meter_subrect = [&](int left, int top, int right, int bottom, int dst_x, int dst_y) {
            left = std::max(0, left);
            top = std::max(0, top);
            right = std::min(meter_w, right);
            bottom = std::min(meter_h, bottom);
            if (right <= left || bottom <= top || !meter_tex) return;
            const float u0 = float(left) / float(meter_w);
            const float u1 = float(right) / float(meter_w);
            const float v0 = float(top) / float(meter_h);
            const float v1 = float(bottom) / float(meter_h);
            push_rgba_rect(meter_tex, false,
                           xoff + float(dst_x * dbl), float(dst_y * dbl),
                           float((right - left) * dbl), float((bottom - top) * dbl),
                           u0, v0, u1, v1);
        };
        auto emit_bar = [&](int value, int offset) {
            const int type = layout[size_t(offset)];
            const int x = int(layout[size_t(offset + 1)] * lscale);
            const int y = int(layout[size_t(offset + 2)] * lscale);
            value = std::clamp(value, 0, 31);
            if (!type) {
                add_meter_subrect(62, 16, 126, 34, x, y);
                add_meter_subrect(62, 0, 62 + value * 2, 16, x + 1, y + 1);
            } else {
                add_meter_subrect(11, 0, 62, 31, x, y);
                add_meter_subrect(11, 32, 62, 63 - value, x, y);
            }
        };
        emit_bar(st.stamina, 40);
        emit_bar(st.condition, 45);
        emit_bar(st.target.empty() ? 0 : st.target_stamina, 50);

        // 2. Dashboard artwork + darkened widescreen side panels.
        if (dash_tex && dash_w > 0 && dash_h > 0) {
            const float dy = float(480 - dash_h * dbl);
            const float dh = float(dash_h * dbl);
            const float dw = float(dash_w * dbl);
            if (margin > 0) {
                const float u_left = 0.5f / float(dash_w);
                const float u_right = (float(dash_w) - 0.5f) / float(dash_w);
                push_rgba_rect(dash_tex, true, 0.0f, dy, xoff, dh, u_left, 0.0f, u_left, 1.0f,
                               0.5f, 0.5f, 0.5f, 1.0f);
                push_rgba_rect(dash_tex, true, xoff + dw, dy, float(wide * 2) - (xoff + dw), dh,
                               u_right, 0.0f, u_right, 1.0f, 0.5f, 0.5f, 0.5f, 1.0f);
            }
            push_rgba_rect(dash_tex, true, xoff, dy, dw, dh, 0.0f, 0.0f, 1.0f, 1.0f);
        }

        // 3. Speed & RPM needles.
        const NeedlePen pen = hud->pen();
        const float nr = float((pen.color >> 16) & 0xff) / 255.0f;
        const float ng = float((pen.color >> 8) & 0xff) / 255.0f;
        const float nb = float(pen.color & 0xff) / 255.0f;
        auto emit_needle = [&](int angle, int offset) {
            const int32_t sx = sine(game.assets.physics, uint32_t(angle));
            const int32_t cy = sine(game.assets.physics, uint32_t(angle + 1024));
            const float x0 = xoff + float(int(layout[size_t(offset)] * lscale));
            const float y0 = float(int(layout[size_t(offset + 1)] * lscale));
            const float x1 = x0 + float(high_product(sx, int(layout[size_t(offset + 2)] * lscale)));
            const float y1 = y0 - float(high_product(cy, int(layout[size_t(offset + 3)] * lscale)));
            const float dx = x1 - x0, dy = y1 - y0;
            const float len = std::sqrt(dx * dx + dy * dy);
            const float tx = len > 0.001f ? dx / len : 1.0f;
            const float ty = len > 0.001f ? dy / len : 0.0f;
            const float nx = -ty, ny = tx;
            const float hw = 0.5f * float(std::max(1, pen.width));
            RgbaDrawQuad q;
            q.tex = white_tex;
            q.blend = false;
            q.v[0] = {x0 - tx * hw + nx * hw, y0 - ty * hw + ny * hw, 0.5f, 0.5f, nr, ng, nb, 1.f};
            q.v[1] = {x1 + tx * hw + nx * hw, y1 + ty * hw + ny * hw, 0.5f, 0.5f, nr, ng, nb, 1.f};
            q.v[2] = {x1 + tx * hw - nx * hw, y1 + ty * hw - ny * hw, 0.5f, 0.5f, nr, ng, nb, 1.f};
            q.v[3] = {x0 - tx * hw - nx * hw, y0 - ty * hw - ny * hw, 0.5f, 0.5f, nr, ng, nb, 1.f};
            rgba_quads.push_back(q);
        };
        emit_needle(st.speed_angle, 21);
        emit_needle(st.rpm_angle, 16);

        // 4. Cached text labels per seat.
        SeatHudCache& sc = seat_caches[seat_key];
        const TextRenderer& tr = hud->text_renderer();
        auto emit_label = [&](size_t slot_idx, const std::string& val, int offset, uint32_t color) {
            LabelSlot& sl = sc.slots[slot_idx];
            if (val.empty()) {
                sl.active = false;
                return;
            }
            const TextRect fg{int(layout[size_t(offset + 1)] * lscale),
                              int(layout[size_t(offset + 2)] * lscale),
                              int(layout[size_t(offset + 3)] * lscale),
                              int(layout[size_t(offset + 4)] * lscale)};
            const TextRect bg{fg.left + 2, fg.top + 2, fg.right + 2, fg.bottom + 2};
            upload_label_slot(sl, tr, Face::badloc, 32, fg, bg, color, 0, val,
                              uint32_t(layout[size_t(offset)]), 640, 480);
            if (sl.active && sl.tex) {
                push_rgba_rect(sl.tex, true, xoff + float(sl.draw_x), float(sl.draw_y),
                               float(sl.draw_w), float(sl.draw_h), 0.f, 0.f, 1.f, 1.f);
            }
        };

        emit_label(0, st.player, 0, 0xffff00);
        emit_label(1, std::to_string(st.place), 25, 0xffffff);
        char distance[32];
        std::snprintf(distance, sizeof distance, "%02d.%d", st.distance / 10, st.distance % 10);
        emit_label(2, distance, 30, 0xffffff);

        if (!st.target.empty()) {
            emit_label(3, st.target, 5, 0xffffff);
            const uint32_t magnitude =
                st.target_gap < 0 ? 0u - uint32_t(st.target_gap) : uint32_t(st.target_gap);
            const int32_t scaled = int32_t(magnitude * 1000u);
            const int64_t gwide =
                int64_t(uint64_t(uint32_t(scaled >> 31)) << 32 | uint32_t(scaled >> 4));
            const int32_t value = int32_t(gwide / (hud->is_metric() ? 3279 : 5280));
            char gap[32];
            std::snprintf(gap, sizeof gap, "%d.%03d", value / 1000, value % 1000);
            const bool ahead = st.target_gap < 1;
            emit_label(4, gap, 10, ahead ? 0xe71000 : 0x28a31c);

            const int dy = ahead ? 9 : 11;
            const TextRect bg{int((layout[11] - 12) * lscale), int((layout[12] + dy) * lscale),
                              int((layout[13] - 12) * lscale), int((layout[14] + dy) * lscale)};
            const TextRect fg{bg.left - 2, bg.top - 2, bg.right - 2, bg.bottom - 2};
            const std::string glyph(1, ahead ? '\xe9' : '\xea');
            LabelSlot& asl = sc.slots[5];
            upload_label_slot(asl, tr, Face::wingdings, 18, fg, bg, 0xffffff, 0, glyph, 0, 640, 480);
            if (asl.active && asl.tex) {
                push_rgba_rect(asl.tex, true, xoff + float(asl.draw_x), float(asl.draw_y),
                               float(asl.draw_w), float(asl.draw_h), 0.f, 0.f, 1.f, 1.f);
            }
        } else {
            sc.slots[3].active = false;
            sc.slots[4].active = false;
            sc.slots[5].active = false;
        }

        // 5. Countdown digit or quit/pause banner.
        const bool has_msg = !game.banner_message.empty();
        const std::string& btext = has_msg ? game.banner_message : banner.text;
        LabelSlot& bsl = sc.slots[6];
        if (!btext.empty()) {
            const int bw = low ? 320 : 640;
            const int bh = low ? 240 : 480;
            const int shadow = low ? 1 : 2;
            const int fheight = has_msg ? (low ? 0x12 : 0x20) : (low ? 0x48 : 0x80);
            TextRect calc{0, 0x78 >> (low ? 1 : 0), bw, bh};
            tr.draw_box(nullptr, Face::badloc, fheight, calc, 0, btext, 0x4851 | dt::calcrect);
            const int top = (0x78 >> (low ? 1 : 0)) - shadow;
            const int bot = std::min(bh, std::max(calc.bottom + 8, top + fheight + 16));
            const TextRect bg{0, (0x78 >> (low ? 1 : 0)), bw, bot};
            const TextRect fg{bg.left - shadow, bg.top - shadow, bg.right - shadow, bg.bottom - shadow};
            upload_label_slot(bsl, tr, Face::badloc, fheight, fg, bg, 0xffffff, 0, btext, 0x4851, bw, bh);
            if (bsl.active && bsl.tex) {
                push_rgba_rect(bsl.tex, true, xoff + float(bsl.draw_x * dbl), float(bsl.draw_y * dbl),
                               float(bsl.draw_w * dbl), float(bsl.draw_h * dbl), 0.f, 0.f, 1.f, 1.f);
            }
        } else {
            bsl.active = false;
        }
    }
};

GpuRaceRenderer::GpuRaceRenderer() : impl_(std::make_unique<Impl>()) {}
GpuRaceRenderer::~GpuRaceRenderer() = default;

void GpuRaceRenderer::reset_scene_caches() {
    if (impl_) {
        impl_->destroy_scene_gl();
        impl_->destroy_hud_gl();
        impl_->destroy_post_gl();
    }
}

bool GpuRaceRenderer::draw(SDL_Renderer* renderer, const void* seat_key, const Game& game,
                           const std::vector<RoadSlice>& slices, int s, int wide,
                           SDL_ScaleMode scale_mode, TexturePtr& target_texture,
                           FrameProfile* profile, bool present) {
    if (!renderer || !game.assets.scene) return false;

    const int scale_w = wide * 2 * s;
    const int scale_h = 480 * s;

    int tw = 0, th = 0, taccess = -1;
    if (target_texture) SDL_QueryTexture(target_texture.get(), nullptr, &taccess, &tw, &th);
    if (!target_texture || tw != scale_w || th != scale_h || taccess != SDL_TEXTUREACCESS_TARGET) {
        target_texture.reset(
            SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, scale_w, scale_h));
        if (!target_texture) return false;
        SDL_SetTextureScaleMode(target_texture.get(), scale_mode);
    } else {
        SDL_SetTextureScaleMode(target_texture.get(), scale_mode);
    }

    SDL_Texture* prev_target = SDL_GetRenderTarget(renderer);
    SDL_SetRenderTarget(renderer, target_texture.get());
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);

    if (!impl_->ensure_init(renderer)) {
        SDL_SetRenderTarget(renderer, prev_target);
        return false;
    }

    const CountdownBanner banner = countdown_banner(game.countdown, countdown_ticks);
    const bool message = !game.banner_message.empty();
    const bool dim = banner.dim || message;
    const bool remap = game.fade_lut.has_value() || dim;
    const ChannelLut lut = game.fade_lut ? *game.fade_lut : dim ? half_palette_lut() : identity_lut();

    // Build scene nodes before entering GL draw state.
    std::optional<ProfileScope> nodes_scope(std::in_place, profile, FrameProfile::scene_nodes);
    auto nodes = game.assets.scene->draw_nodes(
        game.race().route(), slices, game.race().camera(), game.depth,
        [&](const rr::RoadSlice& near, const rr::RoadSlice& far, int road, int32_t ceiling, bool back) {
            return game.rider_nodes(near, far, road, ceiling, back);
        },
        wide);
    nodes_scope.reset();

    {
        GlStateGuard gl_guard;
        const GLuint target_fbo = GLuint(gl_guard.fbo);

        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glViewport(0, 0, scale_w, scale_h);

        impl_->sync_scene(game.assets.scene.get());
        impl_->sync_palette(game.palette, lut, remap);

        const bool post_grade = game.enhancements.grade != Grade::none;
        const bool post_bloom = game.enhancements.bloom;
        const bool post_vig = game.enhancements.vignette;
        const bool post_mb = game.enhancements.motion_blur;
        bool use_post = post_grade || post_bloom || post_vig || post_mb;
        SeatPostCache* pc = nullptr;
        if (use_post) {
            pc = &impl_->post_caches[seat_key];
            if (!post_mb || pc->hist[0].w != scale_w || pc->hist[0].h != scale_h) pc->hist_valid = false;
            const int dw = std::max(1, scale_w / 4);
            const int dh = std::max(1, scale_h / 4);
            if (!pc->scene.ensure(scale_w, scale_h, target_fbo) ||
                (post_mb && (!pc->hist[0].ensure(scale_w, scale_h, target_fbo) ||
                             !pc->hist[1].ensure(scale_w, scale_h, target_fbo))) ||
                (post_bloom && (!pc->bloom[0].ensure(dw, dh, target_fbo) ||
                                !pc->bloom[1].ensure(dw, dh, target_fbo)))) {
                use_post = false;
            }
        } else {
            auto pit = impl_->post_caches.find(seat_key);
            if (pit != impl_->post_caches.end()) pit->second.hist_valid = false;
        }
        if (use_post) glBindFramebuffer(GL_FRAMEBUFFER, pc->scene.fbo);

        const Palette& shown = remap ? [&]() -> Palette {
            Palette p = game.palette;
            for (size_t i = 0; i < 256; ++i)
                for (size_t c = 0; c < 3; ++c) p.rgbx[4 * i + c] = lut[p.rgbx[4 * i + c]];
            return p;
        }() : game.palette;

        // Clear background to palette[0] (lower rows below y=320 use palette[0]).
        glClearColor(float(shown.rgbx[0]) / 255.0f, float(shown.rgbx[1]) / 255.0f,
                     float(shown.rgbx[2]) / 255.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        const float log_w = float(wide * 2);
        const float log_h = 480.0f;
        const float inv_half_w = 2.0f / log_w;
        const float inv_half_h = 2.0f / log_h;

        glUseProgram(impl_->prog_indexed);
        glUniform2f(impl_->u_idx_inv_half, inv_half_w, inv_half_h);
        glUniform1i(impl_->u_idx_atlas, 0);
        glUniform1i(impl_->u_idx_palette, 1);
        glUniform1f(impl_->u_idx_bilinear, 0.0f);
        glUniform2f(impl_->u_idx_atlas_size, 1.0f, 1.0f);
        glUniform2f(impl_->u_idx_inv_atlas_size, 1.0f, 1.0f);

        const bool fog_on = game.enhancements.fog > 0;
        const int draw_slices = std::max(1, game.enhancements.draw_slices);
        const int fog_strength = game.enhancements.fog;
        glUniform3f(impl_->u_idx_fog_color, float(shown.rgbx[0]) / 255.0f,
                    float(shown.rgbx[1]) / 255.0f, float(shown.rgbx[2]) / 255.0f);

        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, impl_->palette_tex);
        glActiveTexture(GL_TEXTURE0);

        glEnableVertexAttribArray(0);
        glEnableVertexAttribArray(1);
        glEnableVertexAttribArray(2);

        // 1. Horizon pass (720x320 scrolling tiled wrap across [0 .. wide*2] x [0 .. 320]).
        if (impl_->horizon_tex) {
            ProfileScope hps(profile, FrameProfile::horizon);
            impl_->idx_verts.clear();
            impl_->idx_indices.clear();
            impl_->runs.clear();

            const int32_t scroll = game.race().horizon_scroll();
            const int32_t start = int32_t(((-int64_t(scroll) % 720) + 720) % 720);
            const int xoff = (720 - wide * 2) / 2;
            int src_x = ((xoff + start) % 720 + 720) % 720;
            int dst_x = 0;
            int rem = wide * 2;
            while (rem > 0) {
                const int chunk = std::min(rem, 720 - src_x);
                const float u0 = float(src_x) / 720.0f;
                const float u1 = float(src_x + chunk) / 720.0f;
                const IndexedVert q[4] = {
                    {float(dst_x), 0.0f, u0, 0.0f, 1.0f, -1.0f, 0.0f},
                    {float(dst_x + chunk), 0.0f, u1, 0.0f, 1.0f, -1.0f, 0.0f},
                    {float(dst_x + chunk), 320.0f, u1, 1.0f, 1.0f, -1.0f, 0.0f},
                    {float(dst_x), 320.0f, u0, 1.0f, 1.0f, -1.0f, 0.0f},
                };
                impl_->push_indexed_quad(0, q);
                dst_x += chunk;
                rem -= chunk;
                src_x = 0;
            }
            if (!impl_->idx_indices.empty()) {
                glBindTexture(GL_TEXTURE_2D, impl_->horizon_tex);
                glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             GLsizeiptr(impl_->idx_verts.size() * sizeof(IndexedVert)),
                             impl_->idx_verts.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             GLsizeiptr(impl_->idx_indices.size() * sizeof(uint16_t)),
                             impl_->idx_indices.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, x)));
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, u)));
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, mode)));
                glDrawElements(GL_TRIANGLES, GLsizei(impl_->idx_indices.size()), GL_UNSIGNED_SHORT, nullptr);
            }
        }

        // 2. World DrawNodes pass (flat + textured quads batched per atlas page).
        {
            ProfileScope rps(profile, FrameProfile::raster);
            impl_->idx_verts.clear();
            impl_->idx_indices.clear();
            impl_->runs.clear();

            const ViewMode mode_2x = view_mode_wide(2, wide);
            constexpr uint32_t kFlatMask = 16;
            const bool persp_on = game.enhancements.perspective;

            for (const auto& node : nodes) {
                if (!node.texture && !node.flat_index) continue;
                if (node.texture && node.texture->mips.empty() && !(node.texture->flags & kFlatMask)) continue;

                // Map to 2x subpixel integer grid for safe normalize_quad without int32 overflow.
                std::array<RasterPoint, 4> nq{};
                int32_t minx = INT32_MAX, maxx = INT32_MIN, miny = INT32_MAX, maxy = INT32_MIN;
                for (size_t i = 0; i < 4; ++i) {
                    nq[i] = {mode_2x.map(node.view_points[i].x), mode_2x.map(node.view_points[i].y)};
                    minx = std::min(minx, nq[i].x);
                    maxx = std::max(maxx, nq[i].x);
                    miny = std::min(miny, nq[i].y);
                    maxy = std::max(maxy, nq[i].y);
                }
                if (minx > mode_2x.surface_width || maxx < 0 || miny > mode_2x.surface_height || maxy < 0)
                    continue;

                const std::array<RasterPoint, 4> orig_nq = nq;
                float corner_fog[4] = {};
                if (fog_on) {
                    for (size_t i = 0; i < 4; ++i) {
                        corner_fog[i] =
                            node.view_depth[i] > 0
                                ? float(fog_factor_q8(node.view_depth[i], draw_slices, fog_strength)) / 256.0f
                                : 0.0f;
                    }
                }
                const bool has_depth =
                    persp_on && (node.view_depth[0] | node.view_depth[1] | node.view_depth[2] | node.view_depth[3]) != 0;

                if (!node.texture || (node.texture->flags & kFlatMask)) {
                    bool horizontal = true;
                    for (const auto& p : nq) horizontal &= (p.y == nq[0].y);
                    if (!horizontal) normalize_quad(nq);

                    const uint8_t fi = node.texture ? node.texture->flat_index : *node.flat_index;
                    const float meta_mode = float(fi) / 255.0f;
                    IndexedVert q[4];
                    for (size_t i = 0; i < 4; ++i) {
                        float cf = 0.0f;
                        if (fog_on) {
                            for (size_t j = 0; j < 4; ++j) {
                                if (orig_nq[j].x == nq[i].x && orig_nq[j].y == nq[i].y) {
                                    cf = corner_fog[j];
                                    break;
                                }
                            }
                        }
                        q[i] = {float(nq[i].x) * 0.5f, float(nq[i].y) * 0.5f, 0.f, 0.f, 1.f, meta_mode, cf};
                    }
                    impl_->push_indexed_quad(-1, q);
                    continue;
                }

                const CelTexture& texture = *node.texture;
                if (texture.flags & 0x2000) continue;

                bool flip_u = node.flip_u;
                if (!normalize_quad(nq, texture.flags, &flip_u)) continue;

                const AtlasEntry* entry = impl_->get_or_upload_cel(&texture);
                if (!entry) continue;

                const AtlasPage& pg = impl_->atlas_pages[size_t(entry->page)];
                const float inv_pw = 1.0f / float(pg.width);
                const float inv_ph = 1.0f / float(pg.height);

                const auto& bounds = texture.bounds;
                const int32_t u0i = std::clamp<int32_t>(flip_u ? bounds[2] - 1 : bounds[0] + 1, 0, entry->w - 1);
                const int32_t u1i = std::clamp<int32_t>(flip_u ? bounds[0] + 1 : bounds[2] - 1, 0, entry->w - 1);
                const int32_t v0i = std::clamp<int32_t>(node.flip_v ? bounds[3] - 1 : bounds[1] + 1, 0, entry->h - 1);
                const int32_t v1i = std::clamp<int32_t>(node.flip_v ? bounds[1] + 1 : bounds[3] - 1, 0, entry->h - 1);

                const float u0 = (float(entry->x + u0i) + 0.5f) * inv_pw;
                const float u1 = (float(entry->x + u1i) + 0.5f) * inv_pw;
                const float v0 = (float(entry->y + v0i) + 0.5f) * inv_ph;
                const float v1 = (float(entry->y + v1i) + 0.5f) * inv_ph;
                const float uu[4] = {u0, u1, u1, u0};
                const float vv[4] = {v0, v0, v1, v1};
                const float meta_mode = (texture.flags & 2) ? -2.0f : -1.0f;

                IndexedVert q[4];
                for (size_t i = 0; i < 4; ++i) {
                    float cf = 0.0f;
                    float invw = 1.0f;
                    if (fog_on || has_depth) {
                        for (size_t j = 0; j < 4; ++j) {
                            if (orig_nq[j].x == nq[i].x && orig_nq[j].y == nq[i].y) {
                                cf = corner_fog[j];
                                if (has_depth) invw = node.view_depth[j] > 0 ? 256.0f / float(node.view_depth[j]) : 1.0f;
                                break;
                            }
                        }
                    }
                    q[i] = {float(nq[i].x) * 0.5f, float(nq[i].y) * 0.5f, uu[i] * invw, vv[i] * invw, invw,
                            meta_mode, cf};
                }
                impl_->push_indexed_quad(entry->page, q);
            }

            if (!impl_->idx_indices.empty()) {
                glUniform1f(impl_->u_idx_bilinear,
                            game.enhancements.filter != TextureFilter::nearest ? 1.0f : 0.0f);
                glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
                glBufferData(GL_ARRAY_BUFFER,
                             GLsizeiptr(impl_->idx_verts.size() * sizeof(IndexedVert)),
                             impl_->idx_verts.data(), GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                             GLsizeiptr(impl_->idx_indices.size() * sizeof(uint16_t)),
                             impl_->idx_indices.data(), GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, x)));
                glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, u)));
                glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(IndexedVert),
                                      reinterpret_cast<const void*>(offsetof(IndexedVert, mode)));

                int cur_page = -1;
                for (const auto& run : impl_->runs) {
                    if (run.page != cur_page && size_t(run.page) < impl_->atlas_pages.size()) {
                        cur_page = run.page;
                        const AtlasPage& pg = impl_->atlas_pages[size_t(cur_page)];
                        glBindTexture(GL_TEXTURE_2D, pg.tex);
                        glUniform2f(impl_->u_idx_atlas_size, float(pg.width), float(pg.height));
                        glUniform2f(impl_->u_idx_inv_atlas_size, 1.0f / float(pg.width),
                                    1.0f / float(pg.height));
                    }
                    glDrawElements(GL_TRIANGLES, GLsizei(run.index_count), GL_UNSIGNED_SHORT,
                                   reinterpret_cast<const void*>(run.index_offset * sizeof(uint16_t)));
                }
            }
        }

        // 2b. GPU Post-processing (grade, motion blur, bloom, vignette) -> target_fbo.
        if (use_post) {
            auto draw_fs_quad = [&]() {
                glDisableVertexAttribArray(2);
                glBindBuffer(GL_ARRAY_BUFFER, impl_->quad_vbo);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->quad_ibo);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), nullptr);
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float),
                                      reinterpret_cast<const void*>(2 * sizeof(float)));
                glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_SHORT, nullptr);
            };

            GLuint world_tex = pc->scene.tex;
            float grade_pending = float(int(game.enhancements.grade));

            if (post_mb) {
                ProfileScope mps(profile, FrameProfile::post);
                const bool reset_hist = game.post_teleported || !pc->hist_valid;
                const float speed = float(std::abs(game.race().speed()));
                const float mb_factor = reset_hist ? 0.0f : motion_blur_factor(speed, 20000.0f);
                game.post_teleported = false;
                const int dst_i = pc->hist_idx ^ 1;
                const int prev_i = pc->hist_idx;
                glBindFramebuffer(GL_FRAMEBUFFER, pc->hist[dst_i].fbo);
                glViewport(0, 0, scale_w, scale_h);
                glUseProgram(impl_->prog_mb);
                glUniform1i(impl_->u_mb_tex0, 0);
                glUniform1i(impl_->u_mb_tex1, 1);
                glUniform1f(impl_->u_mb_grade, grade_pending);
                glUniform1f(impl_->u_mb_factor, mb_factor);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, pc->hist[prev_i].tex);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, pc->scene.tex);
                draw_fs_quad();
                pc->hist_idx = dst_i;
                pc->hist_valid = true;
                world_tex = pc->hist[dst_i].tex;
                grade_pending = 0.0f;
            }

            if (post_bloom) {
                ProfileScope bps(profile, FrameProfile::post);
                const int dw = pc->bloom[0].w, dh = pc->bloom[0].h;
                glViewport(0, 0, dw, dh);
                glBindFramebuffer(GL_FRAMEBUFFER, pc->bloom[0].fbo);
                glUseProgram(impl_->prog_bright);
                glUniform1i(impl_->u_br_tex0, 0);
                glUniform1f(impl_->u_br_grade, grade_pending);
                glUniform2f(impl_->u_br_step, 1.0f / float(scale_w), 1.0f / float(scale_h));
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, world_tex);
                draw_fs_quad();

                glBindFramebuffer(GL_FRAMEBUFFER, pc->bloom[1].fbo);
                glUseProgram(impl_->prog_blur);
                glUniform1i(impl_->u_bl_tex0, 0);
                glUniform2f(impl_->u_bl_step, 1.0f / float(dw), 0.0f);
                glBindTexture(GL_TEXTURE_2D, pc->bloom[0].tex);
                draw_fs_quad();

                glBindFramebuffer(GL_FRAMEBUFFER, pc->bloom[0].fbo);
                glUniform2f(impl_->u_bl_step, 0.0f, 1.0f / float(dh));
                glBindTexture(GL_TEXTURE_2D, pc->bloom[1].tex);
                draw_fs_quad();
            }

            {
                ProfileScope cps(profile, FrameProfile::post);
                glBindFramebuffer(GL_FRAMEBUFFER, target_fbo);
                glViewport(0, 0, scale_w, scale_h);
                glUseProgram(impl_->prog_post);
                glUniform1i(impl_->u_po_tex0, 0);
                glUniform1i(impl_->u_po_tex1, 1);
                glUniform1f(impl_->u_po_grade, grade_pending);
                glUniform1f(impl_->u_po_bloom, post_bloom ? 1.0f : 0.0f);
                glUniform1f(impl_->u_po_vignette, post_vig ? 1.0f : 0.0f);
                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, post_bloom ? pc->bloom[0].tex : impl_->white_tex);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, world_tex);
                draw_fs_quad();
            }
        }

        // 3. Dashboard + HUD text overlay pass.
        {
            ProfileScope dps(profile, FrameProfile::dash);
            impl_->rgba_quads.clear();
            impl_->build_hud_quads(seat_key, game, banner, wide);
            if (!impl_->rgba_quads.empty()) {
                const size_t nq = impl_->rgba_quads.size();
                std::vector<RgbaVert> verts;
                std::vector<uint16_t> inds;
                verts.reserve(nq * 4);
                inds.reserve(nq * 6);
                for (size_t i = 0; i < nq; ++i) {
                    const uint16_t base = uint16_t(i * 4);
                    for (int k = 0; k < 4; ++k) verts.push_back(impl_->rgba_quads[i].v[k]);
                    inds.push_back(base);
                    inds.push_back(uint16_t(base + 1));
                    inds.push_back(uint16_t(base + 2));
                    inds.push_back(base);
                    inds.push_back(uint16_t(base + 2));
                    inds.push_back(uint16_t(base + 3));
                }

                glUseProgram(impl_->prog_rgba);
                glUniform2f(impl_->u_rgba_inv_half, inv_half_w, inv_half_h);
                glUniform1i(impl_->u_rgba_tex, 0);
                glUniform1i(impl_->u_rgba_fade_lut, 1);
                glUniform1f(impl_->u_rgba_use_fade, remap ? 1.0f : 0.0f);

                glActiveTexture(GL_TEXTURE1);
                glBindTexture(GL_TEXTURE_2D, impl_->fade_lut_tex);
                glActiveTexture(GL_TEXTURE0);

                glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

                glEnableVertexAttribArray(0);
                glEnableVertexAttribArray(1);
                glEnableVertexAttribArray(2);
                glBindBuffer(GL_ARRAY_BUFFER, impl_->vbo);
                glBufferData(GL_ARRAY_BUFFER, GLsizeiptr(verts.size() * sizeof(RgbaVert)), verts.data(),
                             GL_DYNAMIC_DRAW);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl_->ibo);
                glBufferData(GL_ELEMENT_ARRAY_BUFFER, GLsizeiptr(inds.size() * sizeof(uint16_t)), inds.data(),
                             GL_DYNAMIC_DRAW);
                glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(RgbaVert),
                                      reinterpret_cast<const void*>(offsetof(RgbaVert, x)));
                glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(RgbaVert),
                                      reinterpret_cast<const void*>(offsetof(RgbaVert, u)));
                glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(RgbaVert),
                                      reinterpret_cast<const void*>(offsetof(RgbaVert, r)));

                bool cur_blend = false;
                glDisable(GL_BLEND);
                GLuint cur_tex = 0;
                for (size_t i = 0; i < nq;) {
                    const GLuint tex = impl_->rgba_quads[i].tex;
                    const bool bl = impl_->rgba_quads[i].blend;
                    size_t j = i + 1;
                    while (j < nq && impl_->rgba_quads[j].tex == tex && impl_->rgba_quads[j].blend == bl) ++j;
                    if (bl != cur_blend) {
                        cur_blend = bl;
                        if (bl) glEnable(GL_BLEND); else glDisable(GL_BLEND);
                    }
                    if (tex != cur_tex) {
                        cur_tex = tex;
                        glBindTexture(GL_TEXTURE_2D, tex);
                    }
                    glDrawElements(GL_TRIANGLES, GLsizei((j - i) * 6), GL_UNSIGNED_SHORT,
                                   reinterpret_cast<const void*>(i * 6 * sizeof(uint16_t)));
                    i = j;
                }
            }
        }
    }

    SDL_SetRenderTarget(renderer, prev_target);
    if (!present) return true;

    ProfileScope ups(profile, FrameProfile::upload);
    const SDL_FRect dst{0, 0, float(scale_w), float(scale_h)};
    SDL_SetRenderLogicalPresentation(renderer, float(scale_w), float(scale_h),
                                     SDL_LOGICAL_PRESENTATION_LETTERBOX);
    if (!SDL_RenderTexture(renderer, target_texture.get(), nullptr, &dst))
        throw std::runtime_error(SDL_GetError());
    return true;
}

} // namespace rr
#endif
