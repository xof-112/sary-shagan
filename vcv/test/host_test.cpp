// Native test of the VCV host (nt_host) with the real Sary-Shagan plug-in, the way the
// Rack module drives it: audio through, panel controls, menu, persistence by name,
// two instances side by side. No Rack needed.
//   make -C vcv/test            run the checks
//   make -C vcv/test render     host-screens.png (screens as the module shows them)
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "../src/nt_host.hpp"

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

static int g_fail = 0, g_checks = 0;
#define CHECK(c, ...)                                         \
    do                                                        \
    {                                                         \
        ++g_checks;                                           \
        if(!(c))                                              \
            ++g_fail;                                         \
        std::fprintf(stderr, "%s ", (c) ? "ok  " : "FAIL");   \
        std::fprintf(stderr, __VA_ARGS__);                    \
        std::fprintf(stderr, "\n");                           \
    } while(0)

static const _NT_factory* Factory()
{
    return reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo, 0));
}

static int Param(const nt_host::Host& h, const char* name)
{
    for(int p = 0; p < h.NumParams(); ++p)
        if(!std::strcmp(h.ParamName(p), name))
            return p;
    return -1;
}
static int Get(const nt_host::Host& h, const char* name) { return h.ParamValue(Param(h, name)); }

// Same routing defaults as the module (panel jacks HOLD / CLEAR / V/OCT).
static const std::vector<std::pair<std::string, int>> kPanel = {
    {"In filter CV", 3}, {"Diffusion CV", 4}, {"Size CV", 5}, {"Dry/Wet CV", 6}, {"Rvb filter CV", 7},
    {"Pre-delay CV", 8}, {"Mod depth CV", 9}, {"Mod rate CV", 10}, {"Mod shape CV", 11}, {"Decay CV", 12},
    {"Freeze gate", 21}, {"Clear gate", 22}};

struct Src
{
    float freeze = 0.f, clear = 0.f, cv[10] = {};
    bool  burst = false; // noise burst at the start of the next Run
};

static long g_t = 0;
// Runs `secs`: optional 0.1 s noise burst into In L (In R follows, as in the module),
// controls every 32 frames, screen every 1600. Returns the output RMS.
static float Run(nt_host::Host& h, float secs, nt_host::Controls& c, Src& s)
{
    double     acc = 0;
    const long n   = static_cast<long>(secs * 48000);
    uint32_t   rnd = 12345;
    for(long i = 0; i < n; ++i, ++g_t)
    {
        float in[kNT_numInputBusses] = {};
        if(s.burst && i < 4800)
        {
            rnd   = rnd * 1664525u + 1013904223u;
            in[0] = ((rnd >> 9) / 4194304.f - 1.f) * 5.f;
        }
        in[1] = in[0];
        for(int k = 0; k < 10; ++k)
            in[2 + k] = s.cv[k];
        const float aux[nt_host::Host::kAuxIn] = {s.freeze, s.clear, 0.f, 0.f};
        float out[2];
        h.Frame(in, aux, out);
        acc += out[0] * out[0] + out[1] * out[1];
        if(i % 32 == 0)
        {
            h.Ui(c);
            c.encoders[0] = c.encoders[1] = 0;
        }
        if(i % 1600 == 0)
            h.Draw();
    }
    s.burst = false;
    return static_cast<float>(std::sqrt(acc / (2.0 * n)));
}

static void Press(nt_host::Host& h, nt_host::Controls& c, Src& s, uint16_t bit)
{
    c.buttons |= bit;
    Run(h, 0.02f, c, s);
    c.buttons &= ~bit;
    Run(h, 0.02f, c, s);
}

static void Menu(nt_host::Host& h, nt_host::Controls& c, Src& s)
{
    c.menu = true;
    Run(h, 0.02f, c, s);
    c.menu = false;
    Run(h, 0.02f, c, s);
}

// Pot sweep in small steps, like a hand on the knob.
static void Turn(nt_host::Host& h, nt_host::Controls& c, Src& s, int k, float to)
{
    const float from = c.pots[k];
    for(int i = 1; i <= 20; ++i)
    {
        c.pots[k] = from + (to - from) * i / 20.f;
        Run(h, 0.002f, c, s);
    }
}

static int g_frame = 0;
static void Frame(nt_host::Host& h, const char* title)
{
    h.Draw();
    uint8_t scr[nt_host::kScreenBytes];
    h.CopyScreen(scr);
    std::printf("F %s\n", title);
    for(int y = 0; y < 64; ++y)
    {
        std::printf("P ");
        for(int x = 0; x < 256; ++x)
        {
            const uint8_t b = scr[y * 128 + x / 2];
            std::printf("%x", (x & 1) ? (b & 15) : (b >> 4));
        }
        std::printf("\n");
    }
    ++g_frame;
}

static int Lit(nt_host::Host& h)
{
    uint8_t scr[nt_host::kScreenBytes];
    h.CopyScreen(scr);
    int n = 0;
    for(uint8_t b : scr)
        n += (b >> 4) > 0 ? 1 : 0, n += (b & 15) > 0 ? 1 : 0;
    return n;
}

int main()
{
    nt_host::Host     h;
    nt_host::Controls c;
    Src               s;
    CHECK(h.Create(Factory(), {}, 48000, kPanel), "create");
    CHECK(Get(h, "Decay CV") == 12 && Get(h, "Freeze gate") == 21 && Get(h, "Clear gate") == 22,
          "panel jacks routed (CVs on inputs 3-12, gates on aux 21/22)");

    // --- audio ---------------------------------------------------------------------
    h.SetParam(Param(h, "Dry/Wet"), 1000);
    s.burst      = true;
    float during = Run(h, 0.5f, c, s);
    float tail   = Run(h, 0.5f, c, s);
    CHECK(during > 0.05f && tail > 0.01f, "reverb tail (rms %.3f, then %.3f)", during, tail);
    Frame(h, "SPACE – Hall läuft, Pegelmesser unten rechts");

    // --- pots: pickup after setupUi (no takeover in Rack) ------------------------------
    c.pots[1] = 0.9f; // knob somewhere else than Size (50 %)
    Run(h, 0.05f, c, s);
    CHECK(Get(h, "Size") == 500, "pot far from the value: no jump");
    Frame(h, "SPACE – Size-Poti noch nicht abgeholt (Strich zeigt die Stellung)");
    Turn(h, c, s, 1, 0.3f); // sweeps through 50 %
    CHECK(std::abs(Get(h, "Size") - 300) <= 2, "pot picked up and follows (Size %d)", Get(h, "Size"));
    Turn(h, c, s, 2, 0.5f);
    Turn(h, c, s, 2, 0.8f);
    CHECK(std::abs(Get(h, "Decay") - 800) <= 2, "Pot R = Decay (%d)", Get(h, "Decay"));

    // --- encoders --------------------------------------------------------------------
    const int d0  = Get(h, "Decay");
    c.encoders[1] = 4;
    Run(h, 0.01f, c, s);
    CHECK(Get(h, "Decay") == d0 + 8, "Encoder R fine-tunes the last pot (Decay %d)", Get(h, "Decay"));
    c.encoders[0] = 1;
    Run(h, 0.01f, c, s);
    Turn(h, c, s, 0, 0.5f);
    Turn(h, c, s, 0, 0.25f);
    CHECK(std::abs(Get(h, "Input filter") + 500) <= 4, "Encoder L -> TONE, Pot L = Input filter (%d)",
          Get(h, "Input filter"));
    Frame(h, "TONE – Input filter, Dry/Wet, Reverb filter");
    c.encoders[0] = 1;
    Run(h, 0.01f, c, s);
    Frame(h, "MOD – Depth, Rate, Shape");
    c.encoders[0] = 1;
    Run(h, 0.01f, c, s);
    Frame(h, "INPUT – Level, Pre-delay, Diffuse");
    Press(h, c, s, kNT_encoderButtonL);
    CHECK(Get(h, "Diffuse input") == 0, "Encoder L push = Diffuse input");
    Press(h, c, s, kNT_encoderButtonL);
    c.encoders[0] = 1;
    Run(h, 0.01f, c, s);

    // --- buttons -----------------------------------------------------------------------
    c.buttons |= kNT_button3;
    s.burst = true;
    Run(h, 0.3f, c, s);
    float frozen = Run(h, 1.f, c, s);
    CHECK(frozen > 0.01f && Get(h, "Freeze") == 0, "FREEZE held keeps the tail (rms %.3f)", frozen);
    Press(h, c, s, kNT_encoderButtonR);
    CHECK(Get(h, "Tuned") == 1, "Encoder R push = Tuned");
    Frame(h, "SPACE – Tuned und Freeze: Tonhöhe des Tanks, RT unendlich");
    Press(h, c, s, kNT_encoderButtonR);
    c.buttons &= ~kNT_button3;
    Run(h, 0.02f, c, s);
    h.SetParam(Param(h, "Freeze mode"), 1);
    Press(h, c, s, kNT_button3);
    CHECK(Get(h, "Freeze") == 1, "latch: FREEZE toggles");
    Press(h, c, s, kNT_button3);
    h.SetParam(Param(h, "Freeze mode"), 0);

    s.burst = true;
    Run(h, 0.3f, c, s);
    Press(h, c, s, kNT_button4);
    float cleared = Run(h, 0.3f, c, s);
    CHECK(cleared < 1e-3f, "CLEAR button empties the tank (rms %.6f)", cleared);

    // --- jacks -------------------------------------------------------------------------
    h.SetParam(Param(h, "Decay"), 300);
    s.burst  = true;
    s.freeze = 5.f;
    Run(h, 0.5f, c, s);
    float held = Run(h, 2.f, c, s);
    CHECK(held > 0.01f, "FREEZE jack keeps the tail (rms %.3f)", held);
    s.freeze = 0.f;
    Run(h, 0.05f, c, s);
    s.clear = 5.f;
    Run(h, 0.01f, c, s);
    s.clear = 0.f;
    cleared = Run(h, 0.3f, c, s);
    CHECK(cleared < 1e-3f, "CLEAR jack empties the tank (rms %.6f)", cleared);

    h.SetParam(Param(h, "Dry/Wet"), 0);
    s.cv[3] = 5.f; // Dry/Wet CV +5 V: 0 % -> 100 %
    s.burst = true;
    float wet = Run(h, 0.5f, c, s);
    CHECK(wet > 0.05f, "DRY/WET jack +5 V turns it fully wet (rms %.3f)", wet);
    s.cv[3] = 0.f;
    s.cv[9] = 2.f; // Decay CV
    Run(h, 0.05f, c, s);
    Frame(h, "SPACE – Decay-CV +2 V: Marke zeigt Regler + CV");
    s.cv[9] = 0.f;

    // --- menu --------------------------------------------------------------------------
    Menu(h, c, s);
    CHECK(h.MenuOpen(), "MENU opens the parameter pages");
    Frame(h, "MENU – Parameterseiten des NT");
    c.encoders[1] = 3;
    Run(h, 0.02f, c, s);
    Menu(h, c, s);
    CHECK(!h.MenuOpen(), "MENU closes again");
    CHECK(Get(h, "Input level") == 3, "menu changed the first parameter (Input level %d)", Get(h, "Input level"));

    // --- persistence by name, second instance --------------------------------------
    auto vals = h.Values();
    {
        nt_host::Host     h2;
        nt_host::Controls c2;
        Src               s2;
        CHECK(h2.Create(Factory(), {}, 44100, vals), "second instance at 44.1 kHz from saved values");
        CHECK(Get(h2, "Size") == Get(h, "Size") && Get(h2, "Input filter") == Get(h, "Input filter"),
              "values restored by name");
        Press(h2, c2, s2, kNT_encoderButtonR);
        CHECK(Get(h2, "Tuned") == 1 && Get(h, "Tuned") == 0, "instances do not share controls");
        h2.SetParam(Param(h2, "Dry/Wet"), 1000);
        s2.burst = true;
        float r2 = Run(h2, 0.3f, c2, s2);
        CHECK(r2 > 0.01f && std::isfinite(r2), "second instance sounds");
    }
    float r1 = Run(h, 0.1f, c, s);
    CHECK(std::isfinite(r1), "first instance still runs after the second is gone");

    CHECK(Lit(h) > 200, "screen has content");
    std::fprintf(stderr, "\n%d/%d checks passed\n", g_checks - g_fail, g_checks);
    return g_fail ? 1 : 0;
}
