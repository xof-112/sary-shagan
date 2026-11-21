// Compares Sary-Shagan's float DSP core with Valley's original double-precision
// Dattorro implementation (built from a ValleyRackFree checkout).
// Run at 44.1 kHz, where the original's tank is not sample-rate clamped.
//
//   g++ -std=c++17 -O2 -I<ValleyRackFree>/src -Isrc test/compare.cpp \
//       <ValleyRackFree>/src/Plateau/Dattorro.cpp \
//       <ValleyRackFree>/src/dsp/filters/OnePoleFilters.cpp -o test/compare

#include "Plateau/Dattorro.hpp"
#include "sary_shagan_dsp.h"
#include <stdio.h>
#include <vector>
#include <cmath>

struct Settings {
    const char* name;
    double timeScale, decay, diffusion, modDepth, modSpeed, modShape;
    double inLowPitch, inHighPitch, tankLowPitch, tankHighPitch;
    bool diffuseInput;
    double preDelay;
};

static double pitchHz(double p) { return 440.0 * std::pow(2.0, p - 5.0); }

int main() {
    const double sr = 44100.0;
    const Settings cases[] = {
        {"defaults",          1.0,  0.80, 10.0, 0.0, 1.0, 0.5, 0.0, 10.0, 0.0, 10.0, true,  0.0},
        {"dark, no diffuse",  2.5,  0.95,  6.0, 0.0, 1.0, 0.5, 2.0,  7.0, 1.5,  6.0, false, 0.05},
        {"small, bright",     0.05, 0.60, 10.0, 0.0, 1.0, 0.5, 0.0, 10.0, 0.0, 10.0, true,  0.0},
        {"modulated",         1.0,  0.80, 10.0, 8.0, 3.0, 0.3, 0.0, 10.0, 0.0, 10.0, true,  0.0},
    };

    int fails = 0;
    for (const auto& c : cases) {
        Dattorro ref(sr, 16.0, 4.0);
        ref.setSampleRate(sr);
        std::vector<float> mem(400000), dtc(4000);
        sary::Reverb g;
        g.init((float)sr, mem.data(), dtc.data());

        ref.setTimeScale(c.timeScale);   g.setTimeScale((float)c.timeScale);
        ref.setDecay(c.decay);           g.setDecay((float)c.decay);
        ref.setTankDiffusion(c.diffusion); g.setDiffusion((float)c.diffusion);
        ref.setTankModDepth(c.modDepth); g.setModDepth((float)c.modDepth);
        ref.setTankModSpeed(c.modSpeed); g.setModSpeed((float)c.modSpeed);
        ref.setTankModShape(c.modShape); g.setModShape((float)c.modShape);
        ref.setInputFilterLowCutoffPitch(c.inLowPitch);   g.setInputLowCut((float)pitchHz(c.inLowPitch));
        ref.setInputFilterHighCutoffPitch(c.inHighPitch); g.setInputHighCut((float)pitchHz(c.inHighPitch));
        ref.setTankFilterLowCutFrequency(c.tankLowPitch); g.setTankLowCut((float)pitchHz(c.tankLowPitch));
        ref.setTankFilterHighCutFrequency(c.tankHighPitch); g.setTankHighCut((float)pitchHz(c.tankHighPitch));
        ref.enableInputDiffusion(c.diffuseInput); g.setInputDiffusion(c.diffuseInput);
        ref.setPreDelay(c.preDelay);     g.setPreDelay((float)c.preDelay);

        const int n = (int)(sr * 3);
        double err = 0, sig = 0, maxErr = 0;
        for (int i = 0; i < n; ++i) {
            double in = (i < 64) ? 0.5 : 0.0;
            ref.process(in, in);
            float gl, gr;
            g.process((float)in, (float)in, gl, gr);
            double dl = ref.getLeftOutput() - gl, dr = ref.getRightOutput() - gr;
            err += dl * dl + dr * dr;
            sig += ref.getLeftOutput() * ref.getLeftOutput() + ref.getRightOutput() * ref.getRightOutput();
            maxErr = std::fmax(maxErr, std::fmax(std::fabs(dl), std::fabs(dr)));
        }
        double snr = 10.0 * std::log10(sig / (err + 1e-30));
        bool ok = c.modDepth > 0 ? snr > 20.0 : snr > 60.0;
        printf("%-18s  error vs. original: %6.1f dB below signal (max abs diff %.2e)  %s\n", c.name, snr, maxErr,
               ok ? "ok" : "FAIL");
        if (!ok) ++fails;
    }
    return fails ? 1 : 0;
}
