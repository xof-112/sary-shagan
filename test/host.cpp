// Sary-Shagan test host: runs the plug-in like the disting NT (bus layout, parameterChanged,
// step, customUi, draw) without hardware.
//
//   host                 all checks (DSP + UI + display bounds)
//   host --render DIR    additionally write pixel-exact screens (nt_emu fonts) as PGM to DIR
//
// The display stubs use the same fonts and placement as nt_emu, so text that runs off
// the 256x64 screen is reported.

#include "../sary_shagan.cpp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <vector>
#include <string>
#include <algorithm>
#include <chrono>

// ---- host API stubs ---------------------------------------------------------
static float g_work[4096];
const _NT_globals NT_globals = {48000, 32, g_work, sizeof(g_work), 0, 0};
uint8_t NT_screen[128 * 64];

static std::vector<int16_t>* g_values = nullptr;
static _NT_algorithm* g_alg = nullptr;
int32_t NT_algorithmIndex(const _NT_algorithm*) { return 0; }
uint32_t NT_parameterOffset(void) { return 0; }
void NT_setParameterFromUi(uint32_t, uint32_t p, int16_t v) {
    (*g_values)[p] = v;
    factory.parameterChanged(g_alg, (int)p);
}
void NT_setParameterFromAudio(uint32_t, uint32_t p, int16_t v) {
    (*g_values)[p] = v;
    factory.parameterChanged(g_alg, (int)p);
}
int NT_intToString(char* b, int32_t v) { return sprintf(b, "%d", (int)v); }
int NT_floatToString(char* b, float v, int dp) { return sprintf(b, "%.*f", dp, v); }

#include "../vcv/src/fonts/tom_thumb_4x6.h"
#include "../vcv/src/fonts/pixelmix_baseline.h"
#include "../vcv/src/fonts/selawik_aa.h"
static uint8_t scr[64][256];
static int g_oob = 0;
static void px(int x, int y, int c) { if (x < 0 || x > 255 || y < 0 || y > 63) return; scr[y][x] = (uint8_t)(c & 15); }
static int chW(char ch, _NT_textSize sz) {
    int i = (unsigned char)ch - 32; if (i < 0 || i > 94) return 0;
    return sz == kNT_textTiny ? 4 : (sz == kNT_textLarge ? fonts::selawik_aaWidths[i] : fonts::pixelmix_baselineWidths[i]);
}
void NT_drawText(int x, int y, const char* s, int c, _NT_textAlignment al, _NT_textSize sz) {
    int w = 0; for (const char* q = s; *q; ++q) w += chW(*q, sz);
    if (al == kNT_textCentre) x -= w / 2; else if (al == kNT_textRight) x -= w;
    const int asc = sz == kNT_textTiny ? 5 : (sz == kNT_textLarge ? 18 : 8);
    const int x0 = x + (sz == kNT_textTiny ? 2 : 0);
    if (x0 < 0 || x0 + w > 256 || y - asc < -1 || y > 64) { printf("  TEXT OOB '%s' x0=%d w=%d y=%d\n", s, x0, w, y); ++g_oob; }
    for (; *s; ++s) {
        int i = (unsigned char)*s - 32;
        if (i >= 0 && i <= 94) {
            if (sz == kNT_textTiny) { for (int r = 0; r < 6; ++r) for (int k = 0; k < 4; ++k) if (fonts::tomThumb4x6Font[i][r] & (0x80 >> k)) px(x + k + 2, y - 5 + r, c); }
            else if (sz == kNT_textLarge) { for (int r = 0; r < fonts::SELAWIK_AA_HEIGHT; ++r) for (int k = 0; k < fonts::SELAWIK_AA_WIDTH; ++k) { int g = fonts::selawik_aaFont[i][r * fonts::SELAWIK_AA_WIDTH + k]; if (g > 0) px(x + k, y - fonts::SELAWIK_AA_ASCENT + r, (g * c + 7) / 15); } }
            else { for (int r = 0; r < 10; ++r) for (int k = 0; k < 8; ++k) if (fonts::pixelmix_baselineFont[i][r] & (0x80 >> k)) px(x + k, y - 8 + r, c); }
        }
        x += chW(*s, sz);
    }
}
void NT_drawShapeI(_NT_shape sh, int x0, int y0, int x1, int y1, int c) {
    if (x0 < 0 || x1 > 255 || y0 < 0 || y1 > 63 || x0 > 255 || y1 < 0) { printf("  DRAW OOB %d %d %d %d\n", x0, y0, x1, y1); ++g_oob; }
    if (sh == kNT_point) { px(x0, y0, c); return; }
    if (sh == kNT_rectangle) { for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) px(x, y, c); return; }
    if (sh == kNT_box) { for (int x = std::min(x0, x1); x <= std::max(x0, x1); ++x) { px(x, y0, c); px(x, y1, c); } for (int y = std::min(y0, y1); y <= std::max(y0, y1); ++y) { px(x0, y, c); px(x1, y, c); } return; }
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1, e = dx + dy;
    for (;;) { px(x0, y0, c); if (x0 == x1 && y0 == y1) break; int e2 = 2 * e; if (e2 >= dy) { e += dy; x0 += sx; } if (e2 <= dx) { e += dx; y0 += sy; } }
}

// ---- host ---------------------------------------------------------------------
struct Host {
    SaryShaganAlgorithm* a;
    std::vector<int16_t> values;
    std::vector<uint8_t> sram, dram, dtc;
    static constexpr int kBlock = 32;
    std::vector<float> busses;

    Host() {
        _NT_algorithmRequirements req{};
        factory.calculateRequirements(req, nullptr);
        sram.assign(req.sram + 64, 0);
        dram.assign(req.dram + 64, 0xAB);   // garbage: init must clear it
        dtc.assign(req.dtc + 64, 0xCD);
        _NT_algorithmMemoryPtrs ptrs{sram.data(), dram.data(), dtc.data(), nullptr};
        a = (SaryShaganAlgorithm*)factory.construct(ptrs, req, nullptr);
        values.resize(req.numParameters);
        for (uint32_t i = 0; i < req.numParameters; ++i) values[i] = parameters[i].def;
        a->v = values.data();
        a->vIncludingCommon = values.data();
        g_values = &values; g_alg = a;
        for (uint32_t i = 0; i < req.numParameters; ++i) factory.parameterChanged(a, i);
        busses.assign(kNT_lastBus * kBlock, 0.f);
    }
    int find(const char* name) {
        for (size_t i = 0; i < values.size(); ++i) if (!strcmp(parameters[i].name, name)) return (int)i;
        fprintf(stderr, "no parameter %s\n", name); exit(1);
    }
    int get(const char* name) { return values[find(name)]; }
    void set(const char* name, int v) { int p = find(name); values[p] = (int16_t)v; factory.parameterChanged(a, p); }
    float* bus(int b) { return busses.data() + (b - 1) * kBlock; }
    template <typename Src>
    void run(int frames, Src src, std::vector<float>& L, std::vector<float>& R) {
        for (int pos = 0; pos < frames; pos += kBlock) {
            std::fill(busses.begin(), busses.end(), 0.f);
            for (int i = 0; i < kBlock; ++i) src(pos + i, bus(1)[i], bus(2)[i], bus(3)[i], bus(4)[i], bus(5)[i]);
            factory.step(a, busses.data(), kBlock / 4);
            for (int i = 0; i < kBlock; ++i) { L.push_back(bus(13)[i]); R.push_back(bus(14)[i]); }
        }
    }
    void run(int frames) { std::vector<float> L, R; run(frames, [](int, float&, float&, float&, float&, float&) {}, L, R); }

    // UI helpers (NT semantics: controls = current state + changed pots, lastButtons = previous state)
    uint16_t buttons = 0;
    float pots[3] = {0.5f, 0.5f, 0.5f};
    void ui(uint16_t newButtons, int encL = 0, int encR = 0, int potIdx = -1, float potVal = 0.f) {
        _NT_uiData d{};
        if (potIdx >= 0) pots[potIdx] = potVal;
        for (int i = 0; i < 3; ++i) d.pots[i] = pots[i];
        d.controls = newButtons;
        if (potIdx >= 0) d.controls |= (uint16_t)(kNT_potL << potIdx);
        d.lastButtons = buttons;
        d.encoders[0] = (int8_t)encL; d.encoders[1] = (int8_t)encR;
        factory.customUi(a, d);
        buttons = newButtons;
    }
    void press(uint16_t b) { ui(b); ui(0); }
    void turnPot(int i, float to) { // sweep in small steps like a real pot
        float from = pots[i];
        int n = 20;
        for (int k = 1; k <= n; ++k) ui(buttons, 0, 0, i, from + (to - from) * k / n);
    }
};

static float rms(const std::vector<float>& x, size_t a, size_t b) {
    double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += x[i] * x[i];
    return (float)sqrt(s / (double)(b - a));
}
static float peak(const std::vector<float>& x, size_t a, size_t b) {
    float p = 0; for (size_t i = a; i < b && i < x.size(); ++i) p = fmaxf(p, fabsf(x[i]));
    return p;
}
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...) do { ++g_checks; if (!(c)) { printf("  FAIL: " __VA_ARGS__); printf("\n"); ++g_fail; } else { printf("  ok:   " __VA_ARGS__); printf("\n"); } } while (0)

static auto noiseBurst(int len) {
    return [len](int i, float& a, float& b, float&, float&, float&) {
        static uint32_t s = 1;
        float n = 0.f;
        if (i < len) { s = s * 1664525u + 1013904223u; n = ((s >> 9) / 4194304.f - 1.f) * 5.f; }
        a = n; b = n;
    };
}

static const char* g_renderDir = nullptr;
static int g_frame = 0;
static void frame(Host& h, const char* title) {
    memset(scr, 0, sizeof(scr));
    const int before = g_oob;
    bool own = factory.draw(h.a);
    CHECK(own && g_oob == before, "screen fits: %s", title);
    if (!g_renderDir) return;
    char fn[512];
    snprintf(fn, sizeof(fn), "%s/screen_%02d.pgm", g_renderDir, ++g_frame);
    FILE* f = fopen(fn, "wb");
    fprintf(f, "P5 256 64 15\n");
    fwrite(scr, 1, sizeof(scr), f);
    fclose(f);
    snprintf(fn, sizeof(fn), "%s/screen_%02d.txt", g_renderDir, g_frame);
    f = fopen(fn, "w"); fprintf(f, "%s\n", title); fclose(f);
}

// ---- parameter listing for the handbook appendix -------------------------------
static std::string busName(int v, bool output) {
    if (v == 0) return "None";
    char b[32];
    if (v <= 12) snprintf(b, sizeof b, "Input %d", v);
    else if (v <= 20) snprintf(b, sizeof b, "Output %d", v - 12);
    else snprintf(b, sizeof b, "Aux %d", v - 20);
    (void)output;
    return b;
}
static std::string valueText(int p, int v) {
    const _NT_parameter& d = parameters[p];
    char b[64];
    switch (d.unit) {
    case kNT_unitEnum: return d.enumStrings[v];
    case kNT_unitHasStrings: parameterString(nullptr, p, v, b); return b;
    case kNT_unitMs: snprintf(b, sizeof b, "%d ms", v); return b;
    case kNT_unitDb_minInf:
        if (v <= d.min) return "-inf dB";
        snprintf(b, sizeof b, "%s%.1f dB", v > 0 ? "+" : "", v * 0.1); return b;
    case kNT_unitPercent:
        if (d.scaling == kNT_scaling10) snprintf(b, sizeof b, "%.1f %%", v * 0.1);
        else snprintf(b, sizeof b, "%d %%", v);
        return b;
    case kNT_unitAudioInput: case kNT_unitCvInput: case kNT_unitAudioOutput: case kNT_unitCvOutput:
        return busName(v, d.unit == kNT_unitAudioOutput || d.unit == kNT_unitCvOutput);
    case kNT_unitOutputMode: return v ? "Replace" : "Add";
    default: snprintf(b, sizeof b, "%d", v); return b;
    }
}
static void listParameters() {
    printf("V\t%s\t%d\n", SARY_SHAGAN_VERSION, (int)kNumParams);
    for (uint32_t pg = 0; pg < parameterPages.numPages; ++pg) {
        const _NT_parameterPage& page = parameterPages.pages[pg];
        for (int k = 0; k < page.numParams; ++k) {
            const int p = page.params[k];
            const _NT_parameter& d = parameters[p];
            std::string range;
            if (d.unit == kNT_unitEnum || d.unit == kNT_unitOutputMode) {
                for (int v = d.min; v <= d.max; ++v) range += (v > d.min ? " · " : "") + valueText(p, v);
            } else {
                range = valueText(p, d.min) + " … " + valueText(p, d.max);
            }
            printf("P\t%s\t%s\t%s\t%s\n", page.name, d.name, range.c_str(), valueText(p, d.def).c_str());
        }
    }
}

int main(int argc, char** argv) {
    if (argc > 1 && !strcmp(argv[1], "--list")) { listParameters(); return 0; }
    if (argc > 2 && !strcmp(argv[1], "--render")) g_renderDir = argv[2];
    const int sr = 48000;

    // ---------------------------------------------------------------- DSP
    {
        printf("[impulse] defaults, 5 V impulse, fully wet\n");
        Host h; h.set("Dry/Wet", 1000);
        std::vector<float> L, R;
        h.run(sr * 6, [](int i, float& a, float& b, float&, float&, float&) { a = b = (i == 0) ? 5.f : 0.f; }, L, R);
        CHECK(finite(L) && finite(R), "finite");
        float e1 = rms(L, 0, sr / 2), e2 = rms(L, sr * 4, sr * 6);
        CHECK(e1 > 1e-4f, "tail present (rms %.4f)", e1);
        CHECK(e2 < e1 * 0.1f, "tail decays (rms 4-6 s %.6f)", e2);
        double c = 0, ll = 0, rr = 0;
        for (int i = 0; i < sr; ++i) { c += L[i] * R[i]; ll += L[i] * L[i]; rr += R[i] * R[i]; }
        CHECK(fabs(c / sqrt(ll * rr)) < 0.95, "stereo image (L/R correlation %.2f)", c / sqrt(ll * rr));
    }
    {
        printf("[dry/wet] crossfade like Mesa\n");
        auto level = [&](int mix, bool wetOnly) {
            Host h; h.set("Dry/Wet", mix);
            std::vector<float> L, R;
            h.run(sr, [](int i, float& a, float& b, float&, float&, float&) { a = b = 2.f * sinf(i * 0.05f); }, L, R);
            return rms(L, sr / 2, sr);
        };
        float dry = level(0, false), mid = level(500, false), wet = level(1000, true);
        Host h0; h0.set("Dry/Wet", 0);
        std::vector<float> L0, R0;
        h0.run(4800, [](int i, float& a, float& b, float&, float&, float&) { a = b = 2.f * sinf(i * 0.05f); }, L0, R0);
        float err = 0; for (int i = 0; i < 4800; ++i) err = fmaxf(err, fabsf(L0[i] - 2.f * sinf(i * 0.05f)));
        CHECK(err < 1e-3f, "0 %% = only dry (max diff %.5f)", err);
        CHECK(wet > 0.1f && dry > 1.f && mid > 0.5f, "levels dry %.2f, centre %.2f, wet %.2f", dry, mid, wet);
    }
    {
        printf("[input level] -inf dB mutes the reverb input\n");
        Host h; h.set("Dry/Wet", 1000); h.set("Input level", -600);
        std::vector<float> L, R;
        h.run(sr, noiseBurst(sr / 10), L, R);
        CHECK(peak(L, 0, sr) < 1e-4f, "no reverb at -inf (peak %.6f)", peak(L, 0, sr));
    }
    {
        printf("[stability] max decay, size, modulation, 60 s\n");
        Host h;
        h.set("Decay", 1000); h.set("Size", 1000); h.set("Mod depth", 1000); h.set("Mod rate", 1000); h.set("Dry/Wet", 1000);
        std::vector<float> L, R;
        h.run(sr * 60, noiseBurst(sr / 10), L, R);
        CHECK(finite(L) && finite(R), "finite");
        float p1 = peak(L, 0, sr * 5), p2 = peak(L, sr * 55, sr * 60);
        CHECK(p2 <= p1 * 1.01f, "no runaway (peak %.2f V -> %.2f V)", p1, p2);
    }
    {
        printf("[freeze] gate 0.5-8 s (momentary)\n");
        Host h; h.set("Dry/Wet", 1000); h.set("Freeze gate", 3); h.set("Decay", 300);
        std::vector<float> L, R;
        auto burst = noiseBurst(sr / 10);
        h.run(sr * 12, [&](int i, float& a, float& b, float& g, float& x, float& y) {
            burst(i, a, b, g, x, y); g = (i > sr / 2 && i < sr * 8) ? 5.f : 0.f; }, L, R);
        float h1 = rms(L, sr * 2, sr * 3), h2 = rms(L, sr * 7, sr * 8), after = rms(L, sr * 11, sr * 12);
        CHECK(h2 > h1 * 0.5f && h2 < h1 * 2.f, "tail sustained while the gate is high");
        CHECK(after < h2 * 0.1f, "decays after the gate");
        CHECK(h.get("Freeze") == 0, "momentary gate leaves the Freeze parameter alone");
    }
    {
        printf("[freeze] latch mode: gate toggles\n");
        Host h; h.set("Freeze mode", 1); h.set("Freeze gate", 3);
        h.run(sr / 10, [&](int i, float&, float&, float& g, float&, float&) { g = i < 2400 ? 5.f : 0.f; }, *new std::vector<float>, *new std::vector<float>);
        CHECK(h.get("Freeze") == 1 && h.a->frozen, "first trigger freezes");
        h.run(sr / 10, [&](int i, float&, float&, float& g, float&, float&) { g = i < 2400 ? 5.f : 0.f; }, *new std::vector<float>, *new std::vector<float>);
        CHECK(h.get("Freeze") == 0 && !h.a->frozen, "second trigger releases");
    }
    {
        printf("[clear] gate at 1 s\n");
        Host h; h.set("Dry/Wet", 1000); h.set("Decay", 950); h.set("Clear gate", 3);
        std::vector<float> L, R;
        auto burst = noiseBurst(sr / 10);
        h.run(sr * 2, [&](int i, float& a, float& b, float& g, float& x, float& y) {
            burst(i, a, b, g, x, y); g = (i > sr && i < sr + 2000) ? 5.f : 0.f; }, L, R);
        CHECK(rms(L, sr + 9600, sr * 2) < rms(L, sr - 4800, sr) * 1e-3f, "tail cleared");
    }
    {
        printf("[tuned] Size CV as pitch CV, 1 V/oct\n");
        auto period = [&](float volts) {
            Host h; h.set("Dry/Wet", 1000); h.set("Tuned", 1); h.set("Size", 500); h.set("Decay", 900);
            h.set("Diffusion", 0); h.set("Mod depth", 0); h.set("Size CV", 4);
            std::vector<float> L, R;
            h.run(sr * 2, [&](int i, float& a, float& b, float&, float& p, float&) { a = b = (i == 0) ? 5.f : 0.f; p = volts; }, L, R);
            int best = 0; double bv = -1e9;
            for (int lag = 200; lag < 1000; ++lag) { double s = 0; for (int i = sr / 2; i < sr / 2 + 8192; ++i) s += L[i] * L[i + lag]; if (s > bv) { bv = s; best = lag; } }
            return best;
        };
        int p0 = period(0.f), p1 = period(1.f);
        CHECK(fabs((double)p0 / p1 - 2.0) < 0.1, "period %d -> %d samples", p0, p1);
    }
    {
        printf("[cv] ±5 V = full knob range, summed with the knob\n");
        Host h; h.set("Decay", 0); h.set("Decay CV", 4); h.set("Diffusion", 1000); h.set("Diffusion CV", 5);
        h.set("Reverb filter", 0); h.set("Rvb filter CV", 3);
        std::vector<float> L, R;
        h.run(480, [&](int, float&, float&, float& c3, float& c4, float& c5) { c3 = 2.5f; c4 = 5.f; c5 = -10.f; }, L, R);
        CHECK(fabsf(h.a->eff[kParamReverbFilter] - 1.f) < 1e-3f, "filter at centre + 2.5 V = fully up");
        CHECK(fabsf(h.a->eff[kParamDecay] - 1.f) < 1e-3f, "decay 0 %% + 5 V = 100 %%");
        CHECK(fabsf(h.a->eff[kParamDiffusion]) < 1e-3f, "diffusion 100 %% - 10 V clamps at 0 %%");
        CHECK(h.get("Decay") == 0, "the knob value itself is untouched");
    }
    {
        printf("[filters] bipolar Input/Reverb filter\n");
        float lo, hi;
        filterFromBipolar(0.f, lo, hi);
        CHECK(lo < 14.f && hi > 14000.f, "centre: no filtering");
        filterFromBipolar(1.f, lo, hi);
        CHECK(lo > 3000.f && hi > 14000.f, "fully up: low cut %.0f Hz", lo);
        filterFromBipolar(-1.f, lo, hi);
        CHECK(lo < 14.f && hi < 120.f, "fully down: high cut %.0f Hz", hi);
    }
    {
        printf("[sweep] 200 parameter changes while running\n");
        Host h; h.set("Dry/Wet", 1000);
        std::vector<float> L, R;
        auto burst = noiseBurst(sr * 20);
        int frames = 0;
        for (int k = 0; k < 200; ++k) {
            h.set("Size", (k * 37) % 1001); h.set("Decay", (k * 53) % 1001);
            h.set("Reverb filter", (k * 71) % 2001 - 1000); h.set("Input filter", (k * 29) % 2001 - 1000);
            h.set("Mod shape", (k * 91) % 1001); h.set("Tuned", (k / 50) % 2); h.set("Pre-delay", (k * 17) % 501);
            h.set("Input level", (k * 13) % 626 - 600);
            h.run(sr / 20, [&](int i, float& a, float& b, float& g, float& x, float& y) { burst(i + frames, a, b, g, x, y); }, L, R);
            frames += sr / 20;
        }
        CHECK(finite(L) && finite(R), "finite");
    }

    // ---------------------------------------------------------------- UI
    {
        printf("[ui] pages, pots with pickup, encoders, buttons\n");
        Host h;
        CHECK(h.a->page == 1, "starts on SPACE");
        _NT_float3 p3;
        factory.setupUi(h.a, p3);
        CHECK(fabsf(p3[0] - 1.f) < 0.01f && fabsf(p3[1] - 0.5f) < 0.01f, "setupUi reports Diffusion/Size");
        for (int i = 0; i < 3; ++i) h.pots[i] = p3[i];

        h.turnPot(1, 0.8f);
        CHECK(h.get("Size") == 800, "Pot C sets Size (%d)", h.get("Size"));
        h.turnPot(2, 0.5f); h.turnPot(2, 0.9f);
        CHECK(h.get("Decay") == 900, "Pot R sets Decay");
        h.ui(0, 0, +5);
        CHECK(h.get("Decay") == 910, "Encoder R fine-tunes the last pot (Decay 91.0 %%)");

        h.ui(0, +1);    // -> TONE
        CHECK(h.a->page == 2, "encoder L turns to TONE");
        h.ui(0, 0, 0, 0, 0.79f);
        CHECK(h.get("Input filter") == 0, "no jump after page change (pickup)");
        h.turnPot(0, 0.5f); h.turnPot(0, 0.75f);
        CHECK(h.get("Input filter") == 500, "pot picks up and follows (%d)", h.get("Input filter"));
        h.turnPot(1, 0.5f); h.turnPot(1, 1.0f);
        CHECK(h.get("Dry/Wet") == 1000, "Pot C = Dry/Wet");

        h.ui(0, +2);    // -> INPUT (wraps)
        CHECK(h.a->page == 0, "page wraps to INPUT");
        CHECK(h.a->fineParam == kParamPreDelay, "fine follows the page's centre pot");
        h.ui(0, 0, +20);
        CHECK(h.get("Pre-delay") == 20, "Encoder R: 1 ms per detent on Pre-delay");

        h.press(kNT_encoderButtonL);
        CHECK(h.get("Diffuse input") == 0, "Encoder L push toggles Diffuse input");
        h.press(kNT_encoderButtonL);
        h.press(kNT_encoderButtonR);
        CHECK(h.get("Tuned") == 1, "Encoder R push toggles Tuned");
        h.press(kNT_encoderButtonR);

        h.ui(kNT_button3);
        h.run(512);
        CHECK(h.a->frozen && h.get("Freeze") == 0, "button 3 held freezes (momentary)");
        h.ui(0);
        h.run(512);
        CHECK(!h.a->frozen, "released: no freeze");
        h.set("Freeze mode", 1);
        h.press(kNT_button3);
        CHECK(h.get("Freeze") == 1, "latch: button 3 toggles");
        h.press(kNT_button3);
        CHECK(h.get("Freeze") == 0, "latch: and back");

        h.press(kNT_button4);
        CHECK(h.a->clearPhase != SaryShaganAlgorithm::kClearIdle, "button 4 starts Clear");
        h.run(sr / 2);
        CHECK(h.a->clearPhase == SaryShaganAlgorithm::kClearIdle, "Clear finishes");
    }

    // ---------------------------------------------------------------- screens
    {
        printf("[screens] every page and state fits on the display\n");
        Host h;
        h.set("Decay CV", 3);
        std::vector<float> L, R;
        h.run(sr, [&](int i, float& a, float& b, float& c, float&, float&) {
            static uint32_t s = 1; s = s * 1664525u + 1013904223u;
            a = b = i < sr / 4 ? ((s >> 9) / 4194304.f - 1.f) * 5.f : 0.f; c = 1.f; }, L, R);
        frame(h, "SPACE: Diffusion, Size, Decay (Decay-CV +1 V: Marke unter dem Balken), Hallzeit");
        h.ui(0, -1); h.run(4800);
        frame(h, "INPUT: Input level, Pre-delay, Diffuse input (Encoder L drücken)");
        h.ui(0, +2); h.run(4800);
        frame(h, "TONE: Input filter, Dry/Wet, Reverb filter (bipolar, Mitte = aus)");
        h.set("Input filter", 400); h.set("Reverb filter", -600);
        h.ui(0, 0, 0, 1, 0.9f);   // pot not picked up yet: marker shows its position
        frame(h, "TONE: Filter verstellt, Dry/Wet-Poti noch nicht abgeholt");
        h.ui(0, +1);
        frame(h, "MOD: Depth, Rate (Hz des ersten LFO), Shape als Bild");
        for (int v : {0, 250, 750, 1000}) {
            h.set("Mod shape", v); h.run(4800);
            char t[96]; snprintf(t, sizeof t, "MOD: Shape %d %% (Punkt läuft mit dem ersten LFO)", v / 10);
            frame(h, t);
        }
        h.set("Mod shape", 300); h.set("Mod shape CV", 3); h.set("Mod rate", 600);
        h.run(4800, [](int, float&, float&, float& c, float&, float&) { c = 2.f; }, L, R);
        frame(h, "MOD: Shape 30 % + 2 V CV = 70 % (Strich = Regler, Marke = Regler + CV)");
        h.set("Mod shape CV", 0); h.set("Mod shape", 500);
        h.ui(0, +2);
        h.set("Tuned", 1); h.set("Freeze", 1); h.set("Size", 300); h.run(4800);
        frame(h, "SPACE im Tuned Mode mit Freeze: Tonhöhe des Tanks, RT unendlich");
        h.set("Freeze", 0);
        h.press(kNT_button4); h.run(64);
        frame(h, "Clear läuft (CLR)");
        h.set("Tuned", 0); h.set("Size", 500); h.set("Dry/Wet", 500);
        h.run(sr * 2);
        h.run(sr / 2, [&](int i, float& a, float& b, float&, float&, float&) { a = b = 0.3f * sinf(i * 0.03f); }, L, R);
        frame(h, "Leises Signal: WET-Pegel mit schattierter Spur");
        for (int pg = 0; pg < kNumUiPages; ++pg) {
            h.a->page = pg;
            for (int k = 0; k < kNumParams; ++k) if (parameters[k].unit < kNT_unitAudioInput) h.set(parameters[k].name, parameters[k].max);
            h.run(64);
            frame(h, "Maximalwerte");
            for (int k = 0; k < kNumParams; ++k) if (parameters[k].unit < kNT_unitAudioInput) h.set(parameters[k].name, parameters[k].min);
            h.run(64);
            frame(h, "Minimalwerte");
        }
    }

    {
        printf("[bench] host CPU, only relative\n");
        Host h; h.set("Mod depth", 500); h.set("Mod rate", 500);
        std::vector<float> L, R; L.reserve(sr * 11); R.reserve(sr * 11);
        auto t0 = std::chrono::steady_clock::now();
        h.run(sr * 10, noiseBurst(sr * 10), L, R);
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        printf("  %.2f ms per second of audio on this machine\n", ms / 10);
    }

    printf(g_fail ? "\n%d of %d checks FAILED\n" : "\nall %d checks passed\n", g_fail ? g_fail : g_checks, g_checks);
    return g_fail ? 1 : 0;
}
