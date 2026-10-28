#include <assert.h>
#include <fenv.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <xmmintrin.h>

static uint32_t memory[1024], g_ecx, g_esp;
static unsigned rate, native_calls;
#define GUEST32(va) memory[(va) / 4u]
static int black_pc_ram_span(uint32_t address, uint32_t size)
{
    return address && (uint64_t)address + size <= sizeof(memory);
}
static unsigned black_sim_hz(void) { return rate; }
static uint32_t bits_of(float f)
{
    uint32_t bits;
    memcpy(&bits, &f, sizeof(bits));
    return bits;
}
static float threshold(void)
{
    float f;
    uint32_t bits = GUEST32(0x100u + 0x390u);
    memcpy(&f, &bits, sizeof(f));
    return f;
}
/* Stand-in for the native constructor: only its two initialized limits are
 * inputs to this wrapper. Return controls/registers must survive the hook. */
void sub_000DF4B0_gen(void)
{
    const uint32_t manager = GUEST32(g_esp + 4u);
    ++native_calls;
    /* The title sets these AFTER the library constructor. Applying the hook
     * inside that earlier constructor would silently be overwritten. */
    if (manager == 0x800u) {
        GUEST32(manager) = 0x900u;
        GUEST32(0x918u) = 0x100u;
        GUEST32(0x48Cu) = 20u;
        GUEST32(0x490u) = bits_of(0.01f);
    }
    g_ecx = 0xABCDu;
    g_esp += 8u;
    _mm_setcsr(0x7FC1u);
}
#include "physics_timing.extracted.inc"

static void reset(unsigned hz)
{
    memset(memory, 0x5A, sizeof(memory));
    rate = hz;
    g_esp = 0xA00u;
    GUEST32(g_esp + 4u) = 0x800u;
    sub_000DF4B0();
    assert(g_ecx == 0xABCDu);
    assert(g_esp == 0xA08u);
    assert(_mm_getcsr() == 0x7FC1u);
    _mm_setcsr(0x1F80u);
}

/* A synthetic unsupported falling body: use the native solver's units,
 * gravity*dt^2 and per-update drag, with no collision impulse. It must not
 * be considered at rest while gravity is still moving it. */
static int falling_sleeps(unsigned hz, float limit, unsigned ticks, float drag)
{
    float displacement = -2.0f, previous = 4.0f;
    const float dt = 1.0f / (float)hz;
    unsigned quiet = 0, i;
    for (i = 0; i < 2u * hz; ++i) {
        float energy;
        displacement = (displacement - 25.0f * dt * dt) * (1.0f - drag);
        energy = displacement * displacement;
        if (energy >= limit) quiet = 0;
        else if (previous >= energy && quiet < ticks) ++quiet;
        previous = energy;
        if (quiet == ticks) return 1;
    }
    return 0;
}

int main(void)
{
    const unsigned rates[] = { 30, 60, 120, 240 };
    unsigned i, j;
    for (i = 0; i < sizeof(rates) / sizeof(rates[0]); ++i) {
        const unsigned hz = rates[i];
        uint32_t expected[1024];
        const float ratio = 30.0f / (float)hz;
        reset(hz);
        assert(GUEST32(0x48Cu) == 20u * hz / 30u);
        assert(threshold() == 0.01f * ratio * ratio);
        memset(expected, 0x5A, sizeof(expected));
        expected[0x48Cu / 4u] = 20u * hz / 30u;
        expected[0x490u / 4u] = bits_of(0.01f * ratio * ratio);
        expected[0x800u / 4u] = 0x900u;
        expected[0x918u / 4u] = 0x100u;
        expected[0xA04u / 4u] = 0x800u;
        assert(memcmp(memory, expected, sizeof(memory)) == 0);
        assert(!falling_sleeps(hz, threshold(), GUEST32(0x48Cu), black_physics_drag(0.05f)));
        /* A one-second angular/linear decay has the same retained fraction
         * at all supported clocks, including the original bit-exact path. */
        {
            const float drag[] = { 0.01f, 0.05f, 0.4f, 0.8f };
            unsigned k;
            for (k = 0; k < sizeof(drag) / sizeof(drag[0]); ++k) {
                const float actual = black_physics_drag(drag[k]);
                assert(fabsf(powf(1.0f - actual, (float)hz) -
                             powf(1.0f - drag[k], 30.0f)) < 0.00001f);
                if (hz == 30u) assert(bits_of(actual) == bits_of(drag[k]));
            }
            _mm_setcsr(0x7FC1u);
            (void)black_physics_drag(0.4f);
            assert(_mm_getcsr() == 0x7FC1u);
            _mm_setcsr(0x1F80u);
        }
        /* A supported body retains BLACK's original settling duration. */
        j = 0;
        while (j < GUEST32(0x48Cu)) ++j;
        assert(fabsf((float)j / (float)hz - 20.0f / 30.0f) < 0.000001f);
        /* Reconstructing a world cannot scale an old result twice. */
        g_esp = 0xA00u;
        sub_000DF4B0();
        assert(GUEST32(0x48Cu) == 20u * hz / 30u);
        assert(threshold() == 0.01f * ratio * ratio);
        _mm_setcsr(0x1F80u);
    }
    assert(falling_sleeps(120u, 0.01f, 20u, 0.05f));
    assert(falling_sleeps(240u, 0.01f, 20u, 0.05f));
    /* Nonpositive, non-finite and native full-stop drag retain their bits. */
    {
        const uint32_t values[] = { 0, 0x80000000u, 0xBF800000u, 0x3F800000u,
            0x7F800000u, 0x7FC12345u, 0xFF800000u };
        for (i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
            float drag;
            memcpy(&drag, &values[i], sizeof(drag));
            assert(bits_of(black_physics_drag(drag)) == values[i]);
        }
    }
    reset(75u);
    assert(GUEST32(0x48Cu) == 20u && threshold() == 0.01f);
    for (i = 0; i < 3u; ++i) {
        uint32_t expected[1024];
        memset(memory, 0xA5, sizeof(memory));
        memcpy(expected, memory, sizeof(memory));
        rate = 120u;
        g_esp = 0xA00u;
        GUEST32(g_esp + 4u) = i == 0 ? 0u : i == 1 ? 0xFFFFFFF0u : 0xF00u;
        expected[0xA04u / 4u] = GUEST32(g_esp + 4u);
        sub_000DF4B0();
        assert(memcmp(memory, expected, sizeof(memory)) == 0);
        assert(g_ecx == 0xABCDu && _mm_getcsr() == 0x7FC1u);
        _mm_setcsr(0x1F80u);
    }
    assert(native_calls == 12u);
    puts("Physics timing: 30/60/120/240 Hz limits, falling/settled bodies, native state and bounds passed.");
    return 0;
}
