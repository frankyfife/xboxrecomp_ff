/*
 * d3d8_pointer.c -- a mouse pointer over the finished frame, for a title
 * whose menus the player can point at (xbox_D3D8SetPointer).
 *
 * Drawn as d3d8_overlay.c draws its text: into the swap chain at present,
 * after the display resolve, so it is sharp at any output size, never part
 * of the title's frame (not in captures, not blended by frame
 * interpolation), and it puts the render target and viewport back.
 *
 * The shape is computed in the pixel shader -- a ring with a cross, light
 * on a dark edge, like a gun sight -- so no texture is involved and it
 * stays crisp at every scale. Where it goes is given in the title's own 2D
 * pixels (640x480 for a 640x480 title) and placed as a 2D draw kept in
 * proportion in the middle of the picture is placed: one guest pixel is
 * picture-height/guest-height host pixels, about the picture's centre. That
 * is how the title's menus are laid out in widescreen too (CENTRE), so the
 * pointer's tip is over what the title drew at that position.
 */
#include "d3d8_internal.h"
#include "d3d8_overlay.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define POINTER_SIZE 26.0f      /* across, in 480-line pixels */

static struct {
    int             tried, failed;
    RhiBuffer      *cb;
    RhiShader      *vs, *ps;
    RhiBlendState  *blend;
    RhiDepthState  *depth;
    RhiRasterState *raster;
} g;

static volatile int   g_shown;
static volatile float g_gx, g_gy;

typedef struct { float x, y, w, h; float px, pad0, pad1, pad2; } PointerConstants;

static const char kSource[] =
    "cbuffer Pointer : register(b7) {\n"
    "    float4 rect;        // x, y, w, h in clip space\n"
    "    float4 info;        // x: the quad's size in pixels\n"
    "};\n"
    "struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };\n"
    "VSOut vs_main(uint id : SV_VertexID) {\n"
    "    float2 c = float2(id & 1, id >> 1);\n"
    "    VSOut o;\n"
    "    o.pos = float4(rect.x + c.x * rect.z, rect.y - c.y * rect.w, 0, 1);\n"
    "    o.uv  = c * 2.0 - 1.0;\n"
    "    return o;\n"
    "}\n"
    /* Distance in pixels from the shape's edge: negative inside. */
    "float shape(float2 p, float px) {\n"
    "    float r = length(p) * px * 0.5;\n"
    "    float R = px * 0.5;\n"
    "    float ring = abs(r - R * 0.62) - R * 0.07;\n"
    "    float2 a = abs(p) * px * 0.5;\n"
    "    float arm = max(min(a.x, a.y) - R * 0.06, abs(max(a.x, a.y) - R * 0.62) - R * 0.30);\n"
    "    float dot_ = r - R * 0.08;\n"
    "    return min(min(ring, arm), dot_);\n"
    "}\n"
    "float4 ps_main(VSOut i) : SV_Target {\n"
    "    float px = info.x;\n"
    "    float d = shape(i.uv, px);\n"
    "    float core = saturate(0.5 - d);\n"
    "    float edge = saturate(0.5 - (d - max(1.0, px / 26.0)));\n"
    "    float3 rgb = lerp(float3(0.02, 0.04, 0.10), float3(0.78, 0.92, 1.0), core);\n"
    "    return float4(rgb, max(core, edge * 0.75));\n"
    "}\n";

static RhiShader *compile_one(uint32_t stage, const char *entry, const char *target)
{
    RhiShaderSource src;
    RhiShader *s;
    char err[2048];

    memset(&src, 0, sizeof src);
    src.hlsl = kSource;
    src.len = sizeof kSource - 1;
    src.name = "pointer";
    src.entry = entry;
    src.target = target;
    s = rhi_shader_create(stage, &src, err, sizeof err);
    if (!s && err[0])
        fprintf(stderr, "D3D8 pointer: %s\n", err);
    return s;
}

static int create(void)
{
    RhiBufferDesc bd;
    RhiBlendDesc bl;
    RhiDepthDesc ds;
    RhiRasterDesc rd;

    if (g.failed) return 0;
    if (g.tried) return g.raster != NULL;
    g.tried = 1;
    g.failed = 1;                       /* until everything below exists */
    if (!rhi_device_ready()) return 0;

    memset(&bd, 0, sizeof bd);
    bd.size = sizeof(PointerConstants);
    bd.usage = RHI_USAGE_DYNAMIC;
    bd.bind = RHI_BIND_UNIFORM;
    bd.cpu_access = RHI_CPU_WRITE;
    if (!(g.cb = rhi_buffer_create(&bd, NULL))) return 0;
    if (!(g.vs = compile_one(RHI_STAGE_VERTEX, "vs_main", "vs_4_0"))) return 0;
    if (!(g.ps = compile_one(RHI_STAGE_PIXEL, "ps_main", "ps_4_0"))) return 0;

    memset(&bl, 0, sizeof bl);
    bl.enable = 1;
    bl.src = RHI_BLEND_SRC_ALPHA;
    bl.dst = RHI_BLEND_INV_SRC_ALPHA;
    bl.op = RHI_BLEND_OP_ADD;
    bl.src_alpha = RHI_BLEND_ONE;
    bl.dst_alpha = RHI_BLEND_INV_SRC_ALPHA;
    bl.op_alpha = RHI_BLEND_OP_ADD;
    bl.write_mask = RHI_WRITE_ALL;
    if (!(g.blend = rhi_blend_state_create(&bl))) return 0;
    memset(&ds, 0, sizeof ds);
    if (!(g.depth = rhi_depth_state_create(&ds))) return 0;
    memset(&rd, 0, sizeof rd);
    rd.fill = RHI_FILL_SOLID;
    rd.cull = RHI_CULL_NONE;
    rd.depth_clip = 1;
    if (!(g.raster = rhi_raster_state_create(&rd))) return 0;
    g.failed = 0;
    return 1;
}

void xbox_D3D8SetPointer(int shown, float x, float y)
{
    g_gx = x;
    g_gy = y;
    g_shown = shown;
}

void d3d8_pointer_present(RhiView *out, unsigned width, unsigned height,
                          unsigned pic_x, unsigned pic_y, unsigned pic_w, unsigned pic_h)
{
    RhiOutputState saved;
    RhiViewport vp;
    PointerConstants c;
    float blend_factor[4] = { 1, 1, 1, 1 };
    float k, cx, cy, size;
    UINT guest_w = d3d8_GetGuestWidth(), guest_h = d3d8_GetGuestHeight();
    void *mapped;

    {
        /* RECOMP_D3D8_POINTER_TEST=<x>,<y>: shown there, for a look at it
         * through d3d8_replay --present. */
        static int test = -1;
        static float tx, ty;
        if (test < 0) {
            const char *v = getenv("RECOMP_D3D8_POINTER_TEST");
            test = v && *v;
            if (test) {
                tx = (float)atof(v);
                ty = strchr(v, ',') ? (float)atof(strchr(v, ',') + 1) : 0.0f;
            }
        }
        if (test)
            xbox_D3D8SetPointer(1, tx, ty);
    }
    if (!g_shown || !out || !width || !height || !pic_h || !guest_w || !guest_h)
        return;
    if (!create())
        return;

    k = (float)pic_h / (float)guest_h;              /* host pixels a guest pixel */
    cx = (float)pic_x + (float)pic_w * 0.5f + (g_gx - (float)guest_w * 0.5f) * k;
    cy = (float)pic_y + g_gy * k;
    size = POINTER_SIZE * (float)pic_h / 480.0f;
    if (size < 12.0f) size = 12.0f;

    rhi_output_save(&saved);
    if (!(mapped = rhi_buffer_map(g.cb, RHI_MAP_WRITE_DISCARD)))
        goto restore;
    c.x = -1.0f + 2.0f * (cx - size * 0.5f) / (float)width;
    c.y =  1.0f - 2.0f * (cy - size * 0.5f) / (float)height;
    c.w =  2.0f * size / (float)width;
    c.h =  2.0f * size / (float)height;
    c.px = size;
    c.pad0 = c.pad1 = c.pad2 = 0.0f;
    memcpy(mapped, &c, sizeof c);
    rhi_buffer_unmap(g.cb);

    vp.x = vp.y = 0.0f;
    vp.width = (float)width;
    vp.height = (float)height;
    vp.min_depth = 0.0f;
    vp.max_depth = 1.0f;
    rhi_set_render_target(out, NULL);
    rhi_set_viewports(1, &vp);
    rhi_set_vertex_layout(NULL);
    rhi_set_topology(RHI_TOPOLOGY_TRIANGLE_STRIP);
    rhi_set_shader(RHI_STAGE_VERTEX, g.vs);
    rhi_set_shader(RHI_STAGE_PIXEL, g.ps);
    rhi_set_uniform_buffers(RHI_STAGE_VERTEX, 7, 1, &g.cb);
    rhi_set_uniform_buffers(RHI_STAGE_PIXEL, 7, 1, &g.cb);
    rhi_set_blend_state(g.blend, blend_factor, 0xFFFFFFFF);
    rhi_set_depth_state(g.depth, 0);
    rhi_set_raster_state(g.raster);
    rhi_draw(4, 0);
restore:
    rhi_output_restore(&saved);
}

void d3d8_pointer_shutdown(void)
{
    rhi_buffer_destroy(g.cb);            g.cb = NULL;
    rhi_shader_destroy(g.vs);            g.vs = NULL;
    rhi_shader_destroy(g.ps);            g.ps = NULL;
    rhi_blend_state_destroy(g.blend);    g.blend = NULL;
    rhi_depth_state_destroy(g.depth);    g.depth = NULL;
    rhi_raster_state_destroy(g.raster);  g.raster = NULL;
    g.tried = g.failed = 0;
}
