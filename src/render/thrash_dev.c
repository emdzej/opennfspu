/* The Direct3D 7 device THRASH drives (see thrash_internal.h), on OpenGL ES 3.0: a flip chain of two
   colour surfaces at the mode's size times an integer scale, one depth/stencil buffer, pre-transformed
   vertices, and one shader carrying out the fixed-function states the DLL sets (two texture stages,
   specular, vertex and table fog, alpha test). Not a port of anything: Direct3D's documented behaviour.

   Surfaces are stored top row first (rows go down the screen, as Direct3D's), so lock buffers and
   glReadPixels rows agree; THRASH_present flips them onto the drawable. */
#include "render/thrash_internal.h"
#include "platform.h"
#include <GLES3/gl3.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int mw, mh, mbpp, scale;       /* the mode, and the surfaces' scale */
static GLuint fbo[2], colour[2], depth;   /* [0] front, [1] back (the render target) */
/* A surface known to be one colour (cleared whole, nothing drawn on it since): a lock's read of it is
   that colour, without waiting on the GPU for the pixels (the movies lock a cleared frame each frame). */
static bool solid[2];
static uint32_t solid_c[2];
/* Or holding the pixels last written to it whole (mode pixels, the round trip through the surface being
   exact): read from that copy. */
static uint8_t *shadow[2];
static bool shadow_ok[2];

static void forget(int i)
{
    solid[i] = false;
    shadow_ok[i] = false;
}
static GLuint prog, present_prog, vao, vbo, ibo, white_tex, blit_tex, samplers[DEV_STAGES];
static uint32_t rstate[RS_COUNT];
static uint32_t tstate[DEV_STAGES][TSS_COUNT];
static DevTexture *bound[DEV_STAGES];
static int vp_x, vp_y, vp_w, vp_h;
static float gamma_scale = 1.0f;

static struct {
    GLint mode, stage_op, stage_arg, has_tex, tfactor, alphatest, alpharef, alphafunc, fog, fogmode,
        fogcolour, fogparams, specular, lodbias, tci, tex0, tex1, pointsize;
} u;
static struct { GLint tex, gamma, rect; } up;

/* ---------------------------------------------------------------- shaders */

static const char *VS =
    "#version 300 es\n"
    "layout(location=0) in vec4 a_pos;\n"        /* x, y, z, rhw: screen pixels */
    "layout(location=1) in vec4 a_diffuse;\n"    /* D3DCOLOR bytes: b, g, r, a */
    "layout(location=2) in vec4 a_specular;\n"
    "layout(location=3) in vec2 a_uv0;\n"
    "layout(location=4) in vec2 a_uv1;\n"
    "uniform vec2 u_mode;\n"
    "uniform int u_tci[2];\n"
    "uniform float u_pointsize;\n"
    "out vec4 v_diffuse; out vec4 v_specular; out vec2 v_uv[2];\n"
    "void main() {\n"
    /* Direct3D 7 puts pixel centres on integer coordinates: + 0.5 for GL's. */
    "  float w = a_pos.w > 0.0 ? 1.0 / a_pos.w : 1.0;\n"
    "  vec2 ndc = (a_pos.xy + 0.5) / u_mode * 2.0 - 1.0;\n"
    "  gl_Position = vec4(ndc * w, (a_pos.z * 2.0 - 1.0) * w, w);\n"
    "  gl_PointSize = u_pointsize;\n"
    "  v_diffuse = a_diffuse.bgra; v_specular = a_specular.bgra;\n"
    "  v_uv[0] = u_tci[0] == 1 ? a_uv1 : a_uv0;\n"
    "  v_uv[1] = u_tci[1] == 1 ? a_uv1 : a_uv0;\n"
    "}\n";

static const char *FS =
    "#version 300 es\n"
    "precision highp float; precision highp int;\n"
    "in vec4 v_diffuse; in vec4 v_specular; in vec2 v_uv[2];\n"
    "uniform sampler2D u_tex0, u_tex1;\n"
    "uniform ivec2 u_stage_op[2];\n"             /* colour op, alpha op (D3DTOP) */
    "uniform ivec4 u_stage_arg[2];\n"            /* colour arg 1, 2, alpha arg 1, 2 (D3DTA) */
    "uniform vec4 u_tfactor;\n"
    "uniform int u_alphatest, u_alpharef, u_alphafunc;\n"
    "uniform int u_fog, u_fogmode, u_specular;\n"
    "uniform vec3 u_fogcolour; uniform vec3 u_fogparams;\n"   /* start, end, density */
    "uniform float u_lodbias[2];\n"
    "out vec4 o;\n"
    "vec4 arg(int a, vec4 tex, vec4 cur) {\n"
    "  int s = a & 15; vec4 r;\n"
    "  if (s == 0) r = v_diffuse; else if (s == 1) r = cur; else if (s == 2) r = tex;\n"
    "  else if (s == 3) r = u_tfactor; else r = v_specular;\n"
    "  if ((a & 16) != 0) r = vec4(1.0) - r;\n"
    "  if ((a & 32) != 0) r = vec4(r.a);\n"
    "  return r;\n"
    "}\n"
    "vec4 op(int o, vec4 a1, vec4 a2, vec4 tex, vec4 cur) {\n"
    "  if (o == 2) return a1; if (o == 3) return a2; if (o == 4) return a1 * a2;\n"
    "  if (o == 5) return 2.0 * a1 * a2; if (o == 6) return 4.0 * a1 * a2; if (o == 7) return a1 + a2;\n"
    "  if (o == 8) return a1 + a2 - 0.5; if (o == 9) return 2.0 * (a1 + a2 - 0.5); if (o == 10) return a1 - a2;\n"
    "  if (o == 11) return a1 + a2 - a1 * a2;\n"
    "  if (o == 12) return mix(a2, a1, v_diffuse.a); if (o == 13) return mix(a2, a1, tex.a);\n"
    "  if (o == 14) return mix(a2, a1, u_tfactor.a); if (o == 15) return a1 + a2 * (1.0 - tex.a);\n"
    "  if (o == 16) return mix(a2, a1, cur.a);\n"
    "  return cur;\n"
    "}\n"
    "bool cmp(int f, int a, int r) {\n"           /* D3DCMPFUNC */
    "  if (f == 1) return false; if (f == 2) return a < r; if (f == 3) return a == r; if (f == 4) return a <= r;\n"
    "  if (f == 5) return a > r; if (f == 6) return a != r; if (f == 7) return a >= r; return true;\n"
    "}\n"
    "void main() {\n"
    "  vec4 cur = v_diffuse;\n"
    "  for (int s = 0; s < 2; s++) {\n"
    "    int cop = u_stage_op[s].x, aop = u_stage_op[s].y;\n"
    "    if (cop <= 1) break;\n"                  /* a disabled stage ends the cascade */
    "    vec4 tex = s == 0 ? texture(u_tex0, v_uv[0], u_lodbias[0]) : texture(u_tex1, v_uv[1], u_lodbias[1]);\n"
    "    vec4 c = op(cop, arg(u_stage_arg[s].x, tex, cur), arg(u_stage_arg[s].y, tex, cur), tex, cur);\n"
    "    vec4 a = aop <= 1 ? cur : op(aop, arg(u_stage_arg[s].z, tex, cur), arg(u_stage_arg[s].w, tex, cur), tex, cur);\n"
    "    cur = clamp(vec4(c.rgb, a.a), 0.0, 1.0);\n"
    "  }\n"
    "  if (u_specular != 0) cur.rgb = min(cur.rgb + v_specular.rgb, 1.0);\n"
    "  if (u_fog != 0) {\n"
    /* Table fog by the eye depth (W fog: 1/rhw, which DX7 drivers with WFOG use for transformed
       vertices). */
    "    float f, z = 1.0 / gl_FragCoord.w;\n"
    "    if (u_fogmode == 1) f = exp(-z * u_fogparams.z);\n"
    "    else if (u_fogmode == 2) f = exp(-(z * u_fogparams.z) * (z * u_fogparams.z));\n"
    "    else if (u_fogmode == 3) f = (u_fogparams.y - z) / (u_fogparams.y - u_fogparams.x);\n"
    "    else f = v_specular.a;\n"
    "    cur.rgb = mix(u_fogcolour, cur.rgb, clamp(f, 0.0, 1.0));\n"
    "  }\n"
    "  if (u_alphatest != 0 && !cmp(u_alphafunc, int(floor(cur.a * 255.0 + 0.5)), u_alpharef)) discard;\n"
    "  o = cur;\n"
    "}\n";

static const char *PVS =
    "#version 300 es\n"
    "uniform vec4 u_rect;\n"                     /* x0, y0, x1, y1 in NDC */
    "out vec2 v_uv;\n"
    "void main() {\n"
    "  vec2 c = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));\n"
    "  v_uv = vec2(c.x, 1.0 - c.y);\n"           /* surfaces are stored top row first */
    "  gl_Position = vec4(mix(u_rect.xy, u_rect.zw, c), 0.0, 1.0);\n"
    "}\n";

static const char *PFS =
    "#version 300 es\n"
    "precision mediump float;\n"
    "uniform sampler2D u_tex; uniform float u_gamma;\n"
    "in vec2 v_uv; out vec4 o;\n"
    "void main() { o = vec4(min(texture(u_tex, v_uv).rgb * u_gamma, 1.0), 1.0); }\n";

static GLuint shader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        plat_log("THRASH: shader:");
        plat_log(log);
    }
    return s;
}

static GLuint program(const char *vs, const char *fs)
{
    GLuint p = glCreateProgram();
    GLuint a = shader(GL_VERTEX_SHADER, vs), b = shader(GL_FRAGMENT_SHADER, fs);
    glAttachShader(p, a);
    glAttachShader(p, b);
    glLinkProgram(p);
    glDeleteShader(a);
    glDeleteShader(b);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = "";
        glGetProgramInfoLog(p, sizeof log, NULL, log);
        plat_log("THRASH: program:");
        plat_log(log);
    }
    return p;
}

/* ---------------------------------------------------------------- open / close */

static void defaults(void)
{
    /* Direct3D 7's device defaults (the DLL sets most of them again right after). */
    memset(rstate, 0, sizeof rstate);
    memset(tstate, 0, sizeof tstate);
    rstate[RS_ZENABLE] = 1;
    rstate[RS_FILLMODE] = 3;
    rstate[RS_SHADEMODE] = 2;
    rstate[RS_ZWRITEENABLE] = 1;
    rstate[RS_SRCBLEND] = 2;
    rstate[RS_DESTBLEND] = 1;
    rstate[RS_CULLMODE] = 3;
    rstate[RS_ZFUNC] = 4;
    rstate[RS_ALPHAFUNC] = 8;
    rstate[RS_FOGEND] = 0x3f800000;
    rstate[RS_FOGDENSITY] = 0x3f800000;
    rstate[RS_STENCILFAIL] = rstate[RS_STENCILZFAIL] = rstate[RS_STENCILPASS] = 1;
    rstate[RS_STENCILFUNC] = 8;
    rstate[RS_TEXTUREFACTOR] = 0xffffffffu;
    for (int s = 0; s < DEV_STAGES; s++) {
        tstate[s][TSS_COLOROP] = s == 0 ? 4 : 1;
        tstate[s][TSS_COLORARG1] = 2;
        tstate[s][TSS_COLORARG2] = 1;
        tstate[s][TSS_ALPHAOP] = s == 0 ? 2 : 1;
        tstate[s][TSS_ALPHAARG1] = 2;
        tstate[s][TSS_ALPHAARG2] = 1;
        tstate[s][TSS_TEXCOORDINDEX] = (uint32_t)s;
        tstate[s][TSS_ADDRESS] = tstate[s][TSS_ADDRESSU] = tstate[s][TSS_ADDRESSV] = 1;
        tstate[s][TSS_MAGFILTER] = tstate[s][TSS_MINFILTER] = 1;
        tstate[s][TSS_MIPFILTER] = 1;
        bound[s] = NULL;
    }
}

int dev_open(int width, int height, int bpp)
{
    int dw, dh;
    plat_display_size(&dw, &dh);
    for (int i = 0; i < 2; i++) {
        free(shadow[i]);
        shadow[i] = NULL;
        shadow_ok[i] = false;
    }
    mw = width;
    mh = height;
    mbpp = bpp;
    scale = dw > 0 && dh > 0 ? (dw / width < dh / height ? dw / width : dh / height) : 1;
    if (scale < 1) scale = 1;
    if (!prog) {
        prog = program(VS, FS);
        present_prog = program(PVS, PFS);
        u.mode = glGetUniformLocation(prog, "u_mode");
        u.stage_op = glGetUniformLocation(prog, "u_stage_op");
        u.stage_arg = glGetUniformLocation(prog, "u_stage_arg");
        u.tfactor = glGetUniformLocation(prog, "u_tfactor");
        u.alphatest = glGetUniformLocation(prog, "u_alphatest");
        u.alpharef = glGetUniformLocation(prog, "u_alpharef");
        u.alphafunc = glGetUniformLocation(prog, "u_alphafunc");
        u.fog = glGetUniformLocation(prog, "u_fog");
        u.fogmode = glGetUniformLocation(prog, "u_fogmode");
        u.fogcolour = glGetUniformLocation(prog, "u_fogcolour");
        u.fogparams = glGetUniformLocation(prog, "u_fogparams");
        u.specular = glGetUniformLocation(prog, "u_specular");
        u.lodbias = glGetUniformLocation(prog, "u_lodbias");
        u.tci = glGetUniformLocation(prog, "u_tci");
        u.tex0 = glGetUniformLocation(prog, "u_tex0");
        u.tex1 = glGetUniformLocation(prog, "u_tex1");
        u.pointsize = glGetUniformLocation(prog, "u_pointsize");
        up.tex = glGetUniformLocation(present_prog, "u_tex");
        up.gamma = glGetUniformLocation(present_prog, "u_gamma");
        up.rect = glGetUniformLocation(present_prog, "u_rect");
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        glGenSamplers(DEV_STAGES, samplers);
        static const uint8_t white[4] = {255, 255, 255, 255};
        glGenTextures(1, &white_tex);
        glBindTexture(GL_TEXTURE_2D, white_tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glGenTextures(1, &blit_tex);
    }
    /* 16-bit modes draw into RGB565 (the original's banding), 32-bit into RGB8; neither has destination
       alpha (X8R8G8B8 hasn't either). One depth/stencil buffer, as Direct3D keeps one z-buffer for the
       render target. */
    GLenum fmt = bpp <= 16 ? GL_RGB565 : GL_RGB8;
    glGenFramebuffers(2, fbo);
    glGenTextures(2, colour);
    glGenRenderbuffers(1, &depth);
    glBindRenderbuffer(GL_RENDERBUFFER, depth);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, mw * scale, mh * scale);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, colour[i]);
        glTexStorage2D(GL_TEXTURE_2D, 1, fmt, mw * scale, mh * scale);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour[i], 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
        glDisable(GL_SCISSOR_TEST);
        glClearColor(0, 0, 0, 1);
        glClearDepthf(1.0f);
        glClearStencil(0);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    }
    defaults();
    solid[0] = solid[1] = true;
    solid_c[0] = solid_c[1] = 0;
    shadow_ok[0] = shadow_ok[1] = false;
    vp_x = vp_y = 0;
    vp_w = mw;
    vp_h = mh;
    return 1;
}

void dev_close(void)
{
    if (!fbo[0]) return;
    glDeleteFramebuffers(2, fbo);
    glDeleteTextures(2, colour);
    glDeleteRenderbuffers(1, &depth);
    fbo[0] = fbo[1] = 0;
}

void dev_begin_scene(void) {}
void dev_end_scene(void) {}

void dev_set_render_state(int rs, uint32_t value)
{
    if (rs >= 0 && rs < RS_COUNT) rstate[rs] = value;
}

uint32_t dev_render_state(int rs) { return rs >= 0 && rs < RS_COUNT ? rstate[rs] : 0; }

void dev_set_tss(int stage, int tss, uint32_t value)
{
    if (stage < 0 || stage >= DEV_STAGES || tss < 0 || tss >= TSS_COUNT) return;
    tstate[stage][tss] = value;
    if (tss == TSS_ADDRESS) tstate[stage][TSS_ADDRESSU] = tstate[stage][TSS_ADDRESSV] = value;
}

void dev_set_texture(int stage, DevTexture *t)
{
    if (stage >= 0 && stage < DEV_STAGES) bound[stage] = t;
}

void dev_set_viewport(int x, int y, int w, int h)
{
    vp_x = x;
    vp_y = y;
    vp_w = w;
    vp_h = h;
}

void dev_set_gamma(float g) { gamma_scale = g; }

/* ---------------------------------------------------------------- drawing state */

static GLenum cmpfunc(uint32_t f)
{
    static const GLenum m[9] = {GL_ALWAYS, GL_NEVER, GL_LESS, GL_EQUAL, GL_LEQUAL, GL_GREATER, GL_NOTEQUAL, GL_GEQUAL, GL_ALWAYS};
    return f <= 8 ? m[f] : GL_ALWAYS;
}

static GLenum blendfactor(uint32_t b, bool src)
{
    switch (b) {
    case 1: return GL_ZERO;
    case 2: return GL_ONE;
    case 3: return GL_SRC_COLOR;
    case 4: return GL_ONE_MINUS_SRC_COLOR;
    case 5: return GL_SRC_ALPHA;
    case 6: return GL_ONE_MINUS_SRC_ALPHA;
    case 7: return GL_DST_ALPHA;
    case 8: return GL_ONE_MINUS_DST_ALPHA;
    case 9: return GL_DST_COLOR;
    case 10: return GL_ONE_MINUS_DST_COLOR;
    case 11: return GL_SRC_ALPHA_SATURATE;
    case 12: return src ? GL_SRC_ALPHA : GL_ONE_MINUS_SRC_ALPHA;   /* BOTHSRCALPHA */
    case 13: return src ? GL_ONE_MINUS_SRC_ALPHA : GL_SRC_ALPHA;   /* BOTHINVSRCALPHA */
    default: return src ? GL_ONE : GL_ZERO;
    }
}

static GLenum stencilop(uint32_t o)
{
    static const GLenum m[9] = {GL_KEEP, GL_KEEP, GL_ZERO, GL_REPLACE, GL_INCR, GL_DECR, GL_INVERT, GL_INCR_WRAP, GL_DECR_WRAP};
    return o <= 8 ? m[o] : GL_KEEP;
}

static GLenum wrap(uint32_t a)
{
    return a == 1 ? GL_REPEAT : a == 2 ? GL_MIRRORED_REPEAT : GL_CLAMP_TO_EDGE;
}

static float bits_float(uint32_t u) { float f; memcpy(&f, &u, 4); return f; }

static void colour4(uint32_t c, float *f)
{
    f[0] = (float)(c >> 16 & 255) / 255.0f;
    f[1] = (float)(c >> 8 & 255) / 255.0f;
    f[2] = (float)(c & 255) / 255.0f;
    f[3] = (float)(c >> 24) / 255.0f;
}

static void scissor(int x0, int y0, int x1, int y1)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > mw) x1 = mw;
    if (y1 > mh) y1 = mh;
    if (x1 < x0) x1 = x0;
    if (y1 < y0) y1 = y0;
    glEnable(GL_SCISSOR_TEST);
    glScissor(x0 * scale, y0 * scale, (x1 - x0) * scale, (y1 - y0) * scale);
}

static void apply_state(int prim)
{
    forget(1);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    glViewport(0, 0, mw * scale, mh * scale);
    scissor(vp_x, vp_y, vp_x + vp_w, vp_y + vp_h);
    glUseProgram(prog);

    if (rstate[RS_ZENABLE]) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(cmpfunc(rstate[RS_ZFUNC]));
    } else {
        glDisable(GL_DEPTH_TEST);
    }
    glDepthMask(rstate[RS_ZWRITEENABLE] ? GL_TRUE : GL_FALSE);
    if (rstate[RS_ALPHABLENDENABLE]) {
        glEnable(GL_BLEND);
        glBlendFunc(blendfactor(rstate[RS_SRCBLEND], true), blendfactor(rstate[RS_DESTBLEND], false));
    } else {
        glDisable(GL_BLEND);
    }
    /* Facing: surfaces keep Direct3D's y-down rows, so its clockwise is GL's counter-clockwise. */
    bool tri = prim >= PRIM_TRIANGLELIST;
    if (tri && rstate[RS_CULLMODE] != 1) {
        glEnable(GL_CULL_FACE);
        glFrontFace(GL_CCW);
        glCullFace(rstate[RS_CULLMODE] == 2 ? GL_FRONT : GL_BACK);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (rstate[RS_STENCILENABLE]) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(cmpfunc(rstate[RS_STENCILFUNC]), 0, 0xffffffffu);
        glStencilOp(stencilop(rstate[RS_STENCILFAIL]), stencilop(rstate[RS_STENCILZFAIL]), stencilop(rstate[RS_STENCILPASS]));
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    if (rstate[RS_DITHERENABLE]) glEnable(GL_DITHER); else glDisable(GL_DITHER);

    GLint ops[4], args[8], tci[2];
    GLfloat bias[2];
    for (int s = 0; s < DEV_STAGES; s++) {
        /* A stage without a texture ends the cascade (stage 0: the diffuse colour). The DLL's default
           blend for stage 1 selects the texture, and the game draws with stage 1 empty, so on the
           hardware it ran on an empty stage can't have contributed. */
        ops[s * 2] = bound[s] ? (GLint)tstate[s][TSS_COLOROP] : 1;
        ops[s * 2 + 1] = (GLint)tstate[s][TSS_ALPHAOP];
        args[s * 4] = (GLint)tstate[s][TSS_COLORARG1];
        args[s * 4 + 1] = (GLint)tstate[s][TSS_COLORARG2];
        args[s * 4 + 2] = (GLint)tstate[s][TSS_ALPHAARG1];
        args[s * 4 + 3] = (GLint)tstate[s][TSS_ALPHAARG2];
        tci[s] = (GLint)(tstate[s][TSS_TEXCOORDINDEX] & 0xffff);
        bias[s] = bits_float(tstate[s][TSS_MIPMAPLODBIAS]);
        if (!isfinite(bias[s])) bias[s] = 0;
        glActiveTexture(GL_TEXTURE0 + (GLenum)s);
        DevTexture *t = bound[s];
        glBindTexture(GL_TEXTURE_2D, t && t->gl ? t->gl : white_tex);
        GLuint sm = samplers[s];
        bool mips = t && t->uploaded_levels > 1, lin = tstate[s][TSS_MINFILTER] >= 2;
        uint32_t mip = tstate[s][TSS_MIPFILTER];
        GLenum minf = !mips || mip <= 1 ? (lin ? GL_LINEAR : GL_NEAREST)
                      : mip == 2 ? (lin ? GL_LINEAR_MIPMAP_NEAREST : GL_NEAREST_MIPMAP_NEAREST)
                                 : (lin ? GL_LINEAR_MIPMAP_LINEAR : GL_NEAREST_MIPMAP_LINEAR);
        glSamplerParameteri(sm, GL_TEXTURE_MIN_FILTER, (GLint)minf);
        glSamplerParameteri(sm, GL_TEXTURE_MAG_FILTER, tstate[s][TSS_MAGFILTER] >= 2 ? GL_LINEAR : GL_NEAREST);
        glSamplerParameteri(sm, GL_TEXTURE_WRAP_S, (GLint)wrap(tstate[s][TSS_ADDRESSU]));
        glSamplerParameteri(sm, GL_TEXTURE_WRAP_T, (GLint)wrap(tstate[s][TSS_ADDRESSV]));
        glBindSampler((GLuint)s, sm);
    }
    glUniform1i(u.tex0, 0);
    glUniform1i(u.tex1, 1);
    glUniform2iv(u.stage_op, 2, ops);
    glUniform4iv(u.stage_arg, 2, args);
    glUniform1iv(u.tci, 2, tci);
    glUniform1fv(u.lodbias, 2, bias);
    glUniform2f(u.mode, (float)mw, (float)mh);
    glUniform1f(u.pointsize, (float)scale);
    float tf[4], fc[4];
    colour4(rstate[RS_TEXTUREFACTOR], tf);
    glUniform4fv(u.tfactor, 1, tf);
    glUniform1i(u.alphatest, rstate[RS_ALPHATESTENABLE] != 0);
    glUniform1i(u.alpharef, (GLint)(rstate[RS_ALPHAREF] & 0xff));
    glUniform1i(u.alphafunc, (GLint)rstate[RS_ALPHAFUNC]);
    glUniform1i(u.fog, rstate[RS_FOGENABLE] != 0);
    glUniform1i(u.fogmode, (GLint)rstate[RS_FOGTABLEMODE]);
    colour4(rstate[RS_FOGCOLOR], fc);
    glUniform3fv(u.fogcolour, 1, fc);
    glUniform3f(u.fogparams, bits_float(rstate[RS_FOGSTART]), bits_float(rstate[RS_FOGEND]), bits_float(rstate[RS_FOGDENSITY]));
    glUniform1i(u.specular, rstate[RS_SPECULARENABLE] != 0);
}

/* ---------------------------------------------------------------- draw */

/* Flat shading takes each triangle's first vertex colours (Direct3D's rule; GL's provoking vertex is the
   last): the triangles are spelled out with copies of their vertices. */
static int flatten(int prim, const uint8_t *v, int stride, const uint16_t *idx, int ni, int nv, uint8_t **out)
{
    int ntri = prim == PRIM_TRIANGLELIST ? (idx ? ni : nv) / 3 : ((idx ? ni : nv) - 2);
    if (ntri <= 0) return 0;
    uint8_t *o = malloc((size_t)ntri * 3 * (size_t)stride);
    if (!o) return 0;
    for (int t = 0; t < ntri; t++) {
        int k[3];
        if (prim == PRIM_TRIANGLELIST) { k[0] = t * 3; k[1] = t * 3 + 1; k[2] = t * 3 + 2; }
        else if (prim == PRIM_TRIANGLESTRIP) { k[0] = t; k[1] = t & 1 ? t + 2 : t + 1; k[2] = t & 1 ? t + 1 : t + 2; }
        else { k[0] = t + 1; k[1] = t + 2; k[2] = 0; }
        /* the first vertex: t for lists and strips (the odd triangles' swap keeps it first), t + 1 for fans */
        const uint8_t *first = v + (size_t)(idx ? idx[k[0]] : k[0]) * stride;
        for (int j = 0; j < 3; j++) {
            int i = idx ? idx[k[j]] : k[j];
            uint8_t *d = o + ((size_t)t * 3 + j) * stride;
            memcpy(d, v + (size_t)i * stride, (size_t)stride);
            memcpy(d + 16, first + 16, 8);
        }
    }
    *out = o;
    return ntri * 3;
}

void dev_draw(int prim, const void *verts, int nverts, int stride, const uint16_t *indices, int nindices)
{
    if (!fbo[1] || nverts <= 0 || !verts) return;
    static const GLenum mode[7] = {GL_POINTS, GL_POINTS, GL_LINES, GL_LINE_STRIP, GL_TRIANGLES, GL_TRIANGLE_STRIP, GL_TRIANGLE_FAN};
    if (prim < 1 || prim > 6) return;
    uint8_t *flat = NULL;
    int nflat = 0;
    if (rstate[RS_SHADEMODE] == 1 && prim >= PRIM_TRIANGLELIST)
        nflat = flatten(prim, verts, stride, indices, nindices, nverts, &flat);
    apply_state(prim);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    if (flat) glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nflat * stride, flat, GL_STREAM_DRAW);
    else glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)nverts * stride, verts, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, stride, (const void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)16);
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, stride, (const void *)20);
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 2, GL_FLOAT, GL_FALSE, stride, (const void *)24);
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 2, GL_FLOAT, GL_FALSE, stride, (const void *)(uintptr_t)(stride > 32 ? 32 : 24));
    if (flat) {
        glDrawArrays(GL_TRIANGLES, 0, nflat);
        free(flat);
    } else if (indices && nindices > 0) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr)nindices * 2, indices, GL_STREAM_DRAW);
        glDrawElements(mode[prim], nindices, GL_UNSIGNED_SHORT, (const void *)0);
    } else {
        glDrawArrays(mode[prim], 0, nverts);
    }
}

void dev_clear(int x0, int y0, int x1, int y1, int flags, uint32_t c, float z, uint32_t stencil)
{
    if (!fbo[1]) return;
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    /* Clear rectangles are clipped to the viewport. */
    int vx1 = vp_x + vp_w, vy1 = vp_y + vp_h;
    scissor(x0 > vp_x ? x0 : vp_x, y0 > vp_y ? y0 : vp_y, x1 < vx1 ? x1 : vx1, y1 < vy1 ? y1 : vy1);
    GLbitfield m = 0;
    if (flags & CLEAR_TARGET) {
        int cx0 = x0 > vp_x ? x0 : vp_x, cy0 = y0 > vp_y ? y0 : vp_y, cx1 = x1 < vx1 ? x1 : vx1, cy1 = y1 < vy1 ? y1 : vy1;
        solid[1] = cx0 <= 0 && cy0 <= 0 && cx1 >= mw && cy1 >= mh;
        solid_c[1] = c & 0xffffff;
        shadow_ok[1] = false;
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor((float)(c >> 16 & 255) / 255.0f, (float)(c >> 8 & 255) / 255.0f, (float)(c & 255) / 255.0f, 1.0f);
        m |= GL_COLOR_BUFFER_BIT;
    }
    if (flags & CLEAR_ZBUFFER) {
        glDepthMask(GL_TRUE);
        glClearDepthf(z < 0 ? 0 : z > 1 ? 1 : z);
        m |= GL_DEPTH_BUFFER_BIT;
    }
    if (flags & CLEAR_STENCIL) {
        glStencilMask(0xffffffffu);
        glClearStencil((GLint)stencil);
        m |= GL_STENCIL_BUFFER_BIT;
    }
    if (m) glClear(m);
}

void dev_flip(bool copy)
{
    if (!fbo[1]) return;
    if (copy) {
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo[1]);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, fbo[0]);
        glDisable(GL_SCISSOR_TEST);
        glBlitFramebuffer(0, 0, mw * scale, mh * scale, 0, 0, mw * scale, mh * scale, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        solid[0] = solid[1];
        solid_c[0] = solid_c[1];
        shadow_ok[0] = shadow_ok[1];
        if (shadow_ok[1] && shadow[0] && shadow[1]) memcpy(shadow[0], shadow[1], (size_t)mw * mh * (mbpp <= 16 ? 2 : 4));
        else shadow_ok[0] = false;
        return;
    }
    bool sf = solid[0], so = shadow_ok[0];
    uint32_t sc = solid_c[0];
    uint8_t *sp = shadow[0];
    solid[0] = solid[1];
    solid_c[0] = solid_c[1];
    shadow_ok[0] = shadow_ok[1];
    shadow[0] = shadow[1];
    solid[1] = sf;
    solid_c[1] = sc;
    shadow_ok[1] = so;
    shadow[1] = sp;
    GLuint f = fbo[0], c = colour[0];
    fbo[0] = fbo[1];
    colour[0] = colour[1];
    fbo[1] = f;
    colour[1] = c;
    /* The z-buffer stays with the render target. */
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[1]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, depth);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[0]);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
}

/* ---------------------------------------------------------------- pixels */

/* Mode pixels <-> the surface (scale x scale texels each: the centre one is read, all are written). */
void dev_read_pixels(int surface, int x, int y, int w, int h, void *dst, int pitch)
{
    if (!fbo[1] || w <= 0 || h <= 0) return;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x + w > mw) w = mw - x;
    if (y + h > mh) h = mh - y;
    if (w <= 0 || h <= 0) return;
    int si = surface == 1 ? 0 : 1;
    if (shadow_ok[si] && shadow[si]) {
        int bpp = mbpp <= 16 ? 2 : 4;
        for (int r = 0; r < h; r++)
            memcpy((uint8_t *)dst + (size_t)r * pitch, shadow[si] + ((size_t)(y + r) * mw + x) * bpp, (size_t)w * bpp);
        return;
    }
    uint8_t one[4] = {(uint8_t)(solid_c[si] >> 16), (uint8_t)(solid_c[si] >> 8), (uint8_t)solid_c[si], 255};
    uint8_t *rgba = NULL;
    if (!solid[si]) {
        rgba = malloc((size_t)w * scale * h * scale * 4);
        if (!rgba) return;
        glBindFramebuffer(GL_FRAMEBUFFER, fbo[si]);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(x * scale, y * scale, w * scale, h * scale, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    int rw = w * scale;
    for (int r = 0; r < h; r++) {
        uint8_t *row = (uint8_t *)dst + (size_t)r * pitch;
        for (int c = 0; c < w; c++) {
            const uint8_t *p = rgba ? rgba + ((size_t)(r * scale + scale / 2) * rw + (size_t)(c * scale + scale / 2)) * 4 : one;
            if (mbpp <= 16) {
                uint16_t v = (uint16_t)((p[0] * 31 + 127) / 255 << 11 | (p[1] * 63 + 127) / 255 << 5 | (p[2] * 31 + 127) / 255);
                memcpy(row + c * 2, &v, 2);
            } else {
                row[c * 4] = p[2];
                row[c * 4 + 1] = p[1];
                row[c * 4 + 2] = p[0];
                row[c * 4 + 3] = 0;
            }
        }
    }
    free(rgba);
}

void dev_write_pixels(int surface, int x, int y, int w, int h, const void *src, int pitch)
{
    if (!fbo[1] || w <= 0 || h <= 0) return;
    uint8_t *rgba = malloc((size_t)w * h * 4);
    if (!rgba) return;
    for (int r = 0; r < h; r++) {
        const uint8_t *row = (const uint8_t *)src + (size_t)r * pitch;
        for (int c = 0; c < w; c++) {
            uint8_t *p = rgba + ((size_t)r * w + c) * 4;
            if (mbpp <= 16) {
                uint16_t v;
                memcpy(&v, row + c * 2, 2);
                p[0] = (uint8_t)((v >> 11) * 255 / 31);
                p[1] = (uint8_t)((v >> 5 & 63) * 255 / 63);
                p[2] = (uint8_t)((v & 31) * 255 / 31);
            } else {
                p[0] = row[c * 4 + 2];
                p[1] = row[c * 4 + 1];
                p[2] = row[c * 4];
            }
            p[3] = 255;
        }
    }
    glBindTexture(GL_TEXTURE_2D, blit_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    free(rgba);
    {
        int si = surface == 1 ? 0 : 1, bpp = mbpp <= 16 ? 2 : 4;
        forget(si);
        if (x == 0 && y == 0 && w == mw && h == mh) {
            if (!shadow[si]) shadow[si] = malloc((size_t)mw * mh * 4);
            if (shadow[si]) {
                for (int r = 0; r < h; r++) memcpy(shadow[si] + (size_t)r * mw * bpp, (const uint8_t *)src + (size_t)r * pitch, (size_t)w * bpp);
                shadow_ok[si] = true;
            }
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, fbo[surface == 1 ? 0 : 1]);
    glViewport(0, 0, mw * scale, mh * scale);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glUseProgram(present_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glUniform1i(up.tex, 0);
    glUniform1f(up.gamma, 1.0f);
    /* The rect in NDC with rows top first; the shader's v flip is undone by swapping y0 and y1. */
    float x0 = (float)x / (float)mw * 2 - 1, x1 = (float)(x + w) / (float)mw * 2 - 1;
    float y0 = (float)y / (float)mh * 2 - 1, y1 = (float)(y + h) / (float)mh * 2 - 1;
    glUniform4f(up.rect, x0, y1, x1, y0);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* The front buffer on the drawable, letterboxed to the mode's aspect, through the gamma ramp. */
void dev_present(void)
{
    int dw, dh;
    plat_display_size(&dw, &dh);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, dw, dh);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!fbo[0] || dw <= 0 || dh <= 0) return;
    float sx = 1, sy = 1, a = (float)mw / (float)mh, da = (float)dw / (float)dh;
    if (da > a) sx = a / da; else sy = da / a;
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_CULL_FACE);
    glUseProgram(present_prog);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glBindTexture(GL_TEXTURE_2D, colour[0]);
    glUniform1i(up.tex, 0);
    glUniform1f(up.gamma, gamma_scale);
    /* Row 0 of the surface (v 0) is the top: it goes to the rect's second corner, y = +sy. */
    glUniform4f(up.rect, -sx, -sy, sx, sy);
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

/* ---------------------------------------------------------------- textures */

static uint8_t expand(unsigned v, int bits) { return (uint8_t)(v * 255 / ((1u << bits) - 1)); }

/* A DXT colour block's four colours (565 endpoints; three colours + transparent black in DXT1 when
   c0 <= c1). */
static void dxt_colours(const uint8_t *b, uint8_t col[4][4], bool dxt1)
{
    uint16_t c0 = (uint16_t)(b[0] | b[1] << 8), c1 = (uint16_t)(b[2] | b[3] << 8);
    uint8_t e[2][3] = {{expand(c0 >> 11, 5), expand(c0 >> 5 & 63, 6), expand(c0 & 31, 5)},
                       {expand(c1 >> 11, 5), expand(c1 >> 5 & 63, 6), expand(c1 & 31, 5)}};
    for (int k = 0; k < 3; k++) {
        col[0][k] = e[0][k];
        col[1][k] = e[1][k];
        if (!dxt1 || c0 > c1) {
            col[2][k] = (uint8_t)((2 * e[0][k] + e[1][k]) / 3);
            col[3][k] = (uint8_t)((e[0][k] + 2 * e[1][k]) / 3);
        } else {
            col[2][k] = (uint8_t)((e[0][k] + e[1][k]) / 2);
            col[3][k] = 0;
        }
    }
    col[0][3] = col[1][3] = col[2][3] = 255;
    col[3][3] = dxt1 && c0 <= c1 ? 0 : 255;
}

/* One level of the record's format -> RGBA8. */
static void decode_level(const DevTexture *t, const uint8_t *src, int w, int h, uint8_t *out)
{
    if (t->format == THRASH_TEX_DXT1 || t->format == THRASH_TEX_DXT3) {
        bool dxt1 = t->format == THRASH_TEX_DXT1;
        int bw = (w + 3) / 4, bh = (h + 3) / 4;
        for (int by = 0; by < bh; by++)
            for (int bx = 0; bx < bw; bx++) {
                const uint8_t *b = src + ((size_t)by * bw + bx) * (dxt1 ? 8 : 16);
                const uint8_t *alpha = dxt1 ? NULL : b;
                const uint8_t *cb = dxt1 ? b : b + 8;
                uint8_t col[4][4];
                dxt_colours(cb, col, dxt1);
                for (int py = 0; py < 4; py++)
                    for (int px = 0; px < 4; px++) {
                        int x = bx * 4 + px, y = by * 4 + py;
                        if (x >= w || y >= h) continue;
                        int sel = cb[4 + py] >> (px * 2) & 3;
                        uint8_t *o = out + ((size_t)y * w + x) * 4;
                        memcpy(o, col[sel], 4);
                        if (alpha) {
                            int n = alpha[py * 2 + px / 2] >> ((px & 1) * 4) & 15;
                            o[3] = (uint8_t)(n * 17);
                        }
                    }
            }
        return;
    }
    int bits = t->format == THRASH_TEX_P4 ? 4 : t->bytespp * 8;
    size_t pitch = (size_t)((bits * w + 7) >> 3);
    for (int y = 0; y < h; y++) {
        const uint8_t *row = src + (size_t)y * pitch;
        for (int x = 0; x < w; x++) {
            uint8_t *o = out + ((size_t)y * w + x) * 4;
            uint32_t c;
            uint16_t v = 0;
            if (t->bytespp == 2) v = (uint16_t)(row[x * 2] | row[x * 2 + 1] << 8);
            switch (t->format) {
            case THRASH_TEX_P4:
            case THRASH_TEX_P8: {
                /* 4-bit: the high nibble is the left pixel (DIB order). Palettised textures are opaque. */
                int i = t->format == THRASH_TEX_P8 ? row[x] : (x & 1 ? row[x >> 1] & 15 : row[x >> 1] >> 4);
                c = t->palette[i];
                o[0] = (uint8_t)(c >> 16);
                o[1] = (uint8_t)(c >> 8);
                o[2] = (uint8_t)c;
                o[3] = 255;
                break;
            }
            case THRASH_TEX_ARGB1555:
                o[0] = expand(v >> 10 & 31, 5);
                o[1] = expand(v >> 5 & 31, 5);
                o[2] = expand(v & 31, 5);
                o[3] = v & 0x8000 ? 255 : 0;
                break;
            case THRASH_TEX_RGB565:
                o[0] = expand(v >> 11, 5);
                o[1] = expand(v >> 5 & 63, 6);
                o[2] = expand(v & 31, 5);
                o[3] = 255;
                break;
            case THRASH_TEX_ARGB4444:
                o[0] = expand(v >> 8 & 15, 4);
                o[1] = expand(v >> 4 & 15, 4);
                o[2] = expand(v & 15, 4);
                o[3] = expand(v >> 12, 4);
                break;
            case THRASH_TEX_ARGB8888:
                o[0] = row[x * 4 + 2];
                o[1] = row[x * 4 + 1];
                o[2] = row[x * 4];
                o[3] = row[x * 4 + 3];
                break;
            default:
                o[0] = o[1] = o[2] = o[3] = 255;
            }
        }
    }
}

static size_t level_bytes(const DevTexture *t, int w, int h)
{
    if (t->format == THRASH_TEX_DXT1 || t->format == THRASH_TEX_DXT3)
        return (size_t)((w + 3) / 4) * (size_t)((h + 3) / 4) * (t->format == THRASH_TEX_DXT1 ? 8 : 16);
    int bits = t->format == THRASH_TEX_P4 ? 4 : t->bytespp * 8;
    return (size_t)((bits * w + 7) >> 3) * (size_t)h;
}

void dev_texture_upload(DevTexture *t)
{
    if (!t->pixels || t->width <= 0 || t->height <= 0) return;
    if (!t->gl) glGenTextures(1, &t->gl);
    glBindTexture(GL_TEXTURE_2D, t->gl);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    int levels = t->levels > 0 ? t->levels : 1;
    uint8_t *rgba = malloc((size_t)t->width * t->height * 4);
    if (!rgba) return;
    const uint8_t *p = t->pixels;
    int w = t->width, h = t->height, l = 0;
    for (; l < levels; l++) {
        decode_level(t, p, w, h, rgba);
        glTexImage2D(GL_TEXTURE_2D, l, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        p += level_bytes(t, w, h);
        if (w == 1 && h == 1) { l++; break; }
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    free(rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, l - 1);
    t->uploaded_levels = l;
}

void dev_texture_free(DevTexture *t)
{
    for (int s = 0; s < DEV_STAGES; s++)
        if (bound[s] == t) bound[s] = NULL;
    if (t->gl) glDeleteTextures(1, &t->gl);
    t->gl = 0;
}
