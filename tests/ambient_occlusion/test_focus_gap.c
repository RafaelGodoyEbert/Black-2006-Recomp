/* Exercise the actual scene coordinator across a prolonged loss of depth.
 * Observing presentation time must not conceal the lack of focus observations. */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "kelvin_post_order.h"
#define MAX_PATH 260
#define DXGI_FORMAT_B8G8R8A8_UNORM 87
#define PCV_AO_OFF 0
#define PCV_AA_FXAA 1
#define PCV_AA_DLAA 3
#define PCV_AA_DLSS_QUALITY 4
#define PCV_AA_DLSS_PERFORMANCE 6
#define PCV_AA_COUNT 7
typedef void ID3D11Resource;
typedef void ID3D11Texture2D;
typedef void ID3D11ShaderResourceView;
typedef struct KGpuSurf {
    int bpp, dxfmt;
    bool depth, real;
    unsigned hw, hh;
    void *srv, *rtv, *tex;
} KGpuSurf;
static struct { void *ctx; uint64_t frames; long long qpf; } G;
static struct {
    bool done, scene_fx, ao_skip_traced, post_trace, focus_skipped;
    int depth_stats, ao_method, dof, aa, sharpen;
    uint64_t ao_camera;
    char ao_scene_prefix[1], ao_dump[1];
    char dof_scene_prefix[1];
    bool dof_scene_done;
    uint64_t dof_scene_frame;
    char sharp_scene_prefix[1];
    bool sharp_scene_done;
    uint64_t sharp_scene_frame;
    unsigned focus_cur;
    long long last_qpc, focus_trace_qpc;
    void *copy, *copy_srv, *ss, *ss_srv, *focus_rtv[2], *focus_srv[2], *coc_rtv, *coc_srv;
    void *ps_focus, *ps_coc, *ps_dof, *ps_fxaa, *ps_sharp;
} s_post;
static long long clock_tick;
static unsigned checks, focus_passes, other_passes;
static float focus_alpha;
static int resource_failure;
static bool simulate_upscale;
static unsigned sharp_passes, sharp_width, sharp_height, output_width, output_height;
static void *last_copy, *sharp_input;
#define CHECK(v) do { ++checks; if (!(v)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #v); exit(1); } } while (0)
static long long qpc_now(void) { return clock_tick; }
static bool post_init(void) { return resource_failure != 1; }
static bool post_scene_resources(unsigned w, unsigned h) { (void)w; (void)h; return resource_failure != 2; }
static void kg_post_depth_stats(KGpuSurf *zs) { (void)zs; abort(); }
static void kg_post_ao_scene(KGpuSurf *cs, KGpuSurf *zs) { (void)cs; (void)zs; }
static void kg_ssr_pass(KGpuSurf *cs, KGpuSurf *zs) { (void)cs; (void)zs; }
static bool kg_dlss_run(KGpuSurf *cs, KGpuSurf *zs) {
    (void)zs;
    if (!simulate_upscale) return false;
    cs->hw *= 2; cs->hh *= 2;
    return true;
}
static bool post_output_resources(unsigned w, unsigned h) { output_width = w; output_height = h; return resource_failure != 3; }
static void post_dof_preset(int dof, float *taps, float *radius) { (void)dof; *taps = 16; *radius = 0.01f; }
static void ID3D11DeviceContext_CopyResource(void *ctx, ID3D11Resource *dst, ID3D11Resource *src) { (void)ctx; (void)src; last_copy = dst; }
static void post_cb(float w, float h, float taps, float radius, float alpha, float depth_scale,
                    float zone, float near_blur, float sharp, float ow, float oh) {
    (void)w; (void)h; (void)taps; (void)radius; (void)depth_scale;
    (void)zone; (void)near_blur; (void)sharp; (void)ow; (void)oh;
    focus_alpha = alpha;
}
static void post_pass(void *shader, void *rtv, unsigned w, unsigned h, void *t0, void *t1, void *t2, void *t3) {
    (void)rtv; (void)w; (void)h; (void)t0; (void)t1; (void)t2; (void)t3;
    if (shader == s_post.ps_focus) ++focus_passes;
    else ++other_passes;
    if (shader == s_post.ps_sharp) { ++sharp_passes; sharp_width = w; sharp_height = h; sharp_input = t0; }
}
static void kg_cache_invalidate(void) {}
static void post_dof_capture(KGpuSurf *cs, KGpuSurf *zs, uint64_t frame) { (void)cs; (void)zs; (void)frame; abort(); }
static void *post_ao_colour_capture(void *tex, unsigned w, unsigned h, const char *path) { (void)tex; (void)w; (void)h; (void)path; abort(); }
#include "focus_gap.extracted.inc"
static void reset(void) {
    memset(&s_post, 0, sizeof(s_post));
    s_post.scene_fx = true; s_post.dof = 3;
    /* Distinct shader tags let the fixture observe focus execution. */
    s_post.ps_focus = (void *)(uintptr_t)1;
    s_post.ps_coc = (void *)(uintptr_t)2;
    s_post.ps_dof = (void *)(uintptr_t)3;
    s_post.ps_sharp = (void *)(uintptr_t)4;
    s_post.copy = (void *)(uintptr_t)5; s_post.copy_srv = (void *)(uintptr_t)6;
    s_post.ss = (void *)(uintptr_t)7; s_post.ss_srv = (void *)(uintptr_t)8;
    G.qpf = 1000;
    clock_tick = 1000; resource_failure = 0;
    focus_alpha = -1; focus_passes = other_passes = 0;
    simulate_upscale = false;
    sharp_passes = sharp_width = sharp_height = output_width = output_height = 0;
    last_copy = sharp_input = NULL;
}
int main(void) {
    int token;
    const KGpuSurf color = {4, DXGI_FORMAT_B8G8R8A8_UNORM, false, true, 640, 480, &token, &token, &token};
    const KGpuSurf depth = {4, 0, true, true, 640, 480, &token, &token, &token};
    KGpuSurf cs = color, zs = depth;
    /* A failed setup must not consume the pre-HUD attempt for every effect. */
    for (int failure = 1; failure <= 2; ++failure) {
        reset(); resource_failure = failure;
        kg_post_scene(&cs, &zs);
        CHECK(!s_post.done); CHECK(focus_passes == 0);
        resource_failure = 0;
        kg_post_scene(&cs, &zs);
        CHECK(s_post.done); CHECK(focus_passes == 1);
    }
    /* Continuous valid observations retain normal exponential smoothing. */
    reset(); kg_post_scene(&cs, &zs);
    CHECK(focus_passes == 1); CHECK(focus_alpha == 1.0f); CHECK(s_post.last_qpc == 1000);
    clock_tick = 1016; kg_post_scene(&cs, &zs);
    CHECK(focus_passes == 2); CHECK(fabsf(focus_alpha - (float)(1.0 - exp(-0.016 / 0.25))) < 1e-6f);
    CHECK(s_post.last_qpc == 1016);
    /* A 960ms missing-depth interval must reacquire rather than blend near focus.
     * Cover each production rejection gate and the DoF-disabled state. */
    for (int kind = 0; kind < 9; ++kind) {
        reset(); cs = color; zs = depth; kg_post_scene(&cs, &zs);
        CHECK(s_post.last_qpc == 1000);
        for (int frame = 1; frame <= 60; ++frame) {
            clock_tick = 1000 + frame * 16;
            switch (kind) {
            case 0: break; /* no depth surface */
            case 1: zs.depth = false; break;
            case 2: zs.real = false; break;
            case 3: zs.srv = NULL; break;
            case 4: zs.hw = 320; break;
            case 5: zs.hh = 240; break;
            case 6: s_post.dof = 0; break;
            case 7: resource_failure = 1; break;
            case 8: resource_failure = 2; break;
            }
            kg_post_scene(&cs, kind == 0 ? NULL : &zs);
            CHECK(focus_passes == 1); CHECK(s_post.last_qpc == 1000);
        }
        zs = depth; s_post.dof = 3; resource_failure = 0; clock_tick = 1976;
        kg_post_scene(&cs, &zs);
        CHECK(focus_passes == 2); CHECK(focus_alpha == 1.0f); CHECK(s_post.last_qpc == 1976);
    }
    /* A brief dropped-depth frame still smooths over the actual observation gap. */
    reset(); kg_post_scene(&cs, &zs);
    clock_tick = 1016; kg_post_scene(&cs, NULL);
    clock_tick = 1032; kg_post_scene(&cs, &zs);
    CHECK(fabsf(focus_alpha - (float)(1.0 - exp(-0.032 / 0.25))) < 1e-6f);
    /* DLSS swaps the colour surface to output size. Sharpening must still run
     * once, using output-sized scratch without resizing the focus resources. */
    reset(); cs = color; zs = depth; s_post.sharpen = 3; s_post.aa = PCV_AA_DLSS_QUALITY;
    simulate_upscale = true;
    kg_post_scene(&cs, &zs);
    CHECK(focus_passes == 1); CHECK(sharp_passes == 1);
    CHECK(sharp_width == 1280 && sharp_height == 960);
    CHECK(output_width == 1280 && output_height == 960);
    CHECK(last_copy == s_post.ss && sharp_input == s_post.ss_srv);
    reset(); cs = color; s_post.sharpen = 3; s_post.aa = PCV_AA_DLSS_QUALITY;
    kg_post_scene(&cs, &zs); /* unavailable DLSS: FXAA then native-size sharpening */
    CHECK(sharp_passes == 1 && other_passes == 4);
    CHECK(sharp_width == 640 && sharp_height == 480);
    CHECK(last_copy == s_post.copy && sharp_input == s_post.copy_srv);
    reset(); cs = color; s_post.sharpen = 0; s_post.aa = PCV_AA_DLSS_QUALITY; simulate_upscale = true;
    kg_post_scene(&cs, &zs); CHECK(sharp_passes == 0 && output_width == 0);
    reset(); cs = color; s_post.sharpen = 3; s_post.aa = PCV_AA_DLSS_QUALITY; simulate_upscale = true; resource_failure = 3;
    kg_post_scene(&cs, &zs); CHECK(sharp_passes == 0); CHECK(focus_passes == 1);
    printf("PASS: %u production focus-gap checks\n", checks);
    return 0;
}
