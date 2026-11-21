/*
 * Sary-Shagan – Dattorro plate reverb for the Expert Sleepers disting NT
 *
 * DSP core: a single-precision, allocation-free port of the Plateau reverb
 * from ValleyRackFree (src/Plateau/Dattorro.*, src/dsp/delays,
 * src/dsp/filters/OnePoleFilters.*, src/dsp/modulation/LFO.hpp).
 *
 *   ValleyRackFree / Plateau
 *   Copyright (C) 2018-2023 Dale Johnson, Valley Audio Soft
 *   https://github.com/ValleyAudio/ValleyRackFree
 *
 *   Port for disting NT
 *   Copyright (C) 2026 xof
 *
 * Based on: Dattorro, J. (1997). Effect design part 1: Reverberator and
 * other filters. J. Audio Eng. Soc, 45(9), 660-684.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 *
 * Differences to the original (behaviour-relevant ones are marked [B]):
 *   - float instead of double, delay memory supplied by the host (no
 *     std::vector, no heap).
 *   - [B] the tank LFOs run at the real sample rate (the original leaves
 *     them at 44.1 kHz, so modulation speed shifts with the sample rate).
 *   - clear() can be done in chunks (see Sary-ShaganReverb::clearStep) so a
 *     large buffer wipe never blows a single audio block.
 */

#pragma once

#include <stdint.h>
#include <string.h>
#include <math.h>

namespace sary {

static constexpr float kTwoPi = 6.28318530717958647692f;

template <typename T>
static inline T clampv(T x, T lo, T hi) { return x < lo ? lo : (x > hi ? hi : x); }

// ---------------------------------------------------------------------------
// Linear-interpolating delay line on externally owned memory.
// Same read/write semantics as Valley's InterpDelay<T>.
// ---------------------------------------------------------------------------
struct Delay {
    float*  buf = nullptr;
    int32_t len = 0;
    int32_t w = 0;
    int32_t t = 0;
    float   f = 0.f;
    float   input = 0.f;
    float   output = 0.f;

    void init(float* mem, int32_t length) {
        buf = mem;
        len = length;
        w = 0;
        clear();
    }

    inline void process() {
        buf[w] = input;
        int32_t r = w - t;
        if (r < 0) r += len;
        if (++w == len) w = 0;
        int32_t u = r - 1;
        if (u < 0) u += len;
        const float a = buf[r];
        output = a + f * (buf[u] - a);
    }

    inline float tap(int32_t i) const {
        int32_t j = w - i;
        if (j < 0) j += len;
        return buf[j];
    }

    inline void setDelayTime(float d) {
        if (d >= (float)len) d = (float)(len - 1);
        if (d < 0.f) d = 0.f;
        t = (int32_t)d;
        f = d - (float)t;
    }

    void clear() {
        if (buf) memset(buf, 0, sizeof(float) * (size_t)len);
        input = output = 0.f;
    }
};

// ---------------------------------------------------------------------------
// Schroeder allpass around a Delay (Valley's AllpassFilter<T>).
// ---------------------------------------------------------------------------
struct Allpass {
    Delay delay;
    float input = 0.f;
    float output = 0.f;
    float gain = 0.f;

    inline float process() {
        const float s = input + delay.output * gain;
        output = delay.output - s * gain;
        delay.input = s;
        delay.process();
        return output;
    }
    void clear() { input = output = 0.f; delay.clear(); }
};

// ---------------------------------------------------------------------------
// One-pole filters (Valley's OnePoleLPFilter / OnePoleHPFilter).
// ---------------------------------------------------------------------------
struct OnePoleLP {
    float sr = 48000.f, fc = -1.f, a = 1.f, b = 0.f, z = 0.f;
    void setSampleRate(float s) { sr = s; float c = fc; fc = -1.f; setCutoff(c < 0.f ? 20000.f : c); }
    void setCutoff(float c) {
        c = clampv(c, 1.f, sr * 0.5f - 1.f);
        if (c == fc) return;
        fc = c;
        b = expf(-kTwoPi * fc / sr);
        a = 1.f - b;
    }
    inline float process(float x) { z = a * x + z * b; return z; }
    void clear() { z = 0.f; }
};

struct OnePoleHP {
    float sr = 48000.f, fc = -1.f, a0 = 1.f, a1 = -1.f, b1 = 0.f, x1 = 0.f, y1 = 0.f;
    void setSampleRate(float s) { sr = s; float c = fc; fc = -1.f; setCutoff(c < 0.f ? 10.f : c); clear(); }
    void setCutoff(float c) {
        c = clampv(c, 1.f, sr * 0.5f - 1.f);
        if (c == fc) return;
        fc = c;
        b1 = expf(-kTwoPi * fc / sr);
        a0 = (1.f + b1) * 0.5f;
        a1 = -a0;
    }
    inline float process(float x) {
        const float y = a0 * x + a1 * x1 + b1 * y1;
        x1 = x; y1 = y;
        return y;
    }
    void clear() { x1 = y1 = 0.f; }
};

// ---------------------------------------------------------------------------
// Triangle/saw LFO with movable reversal point (Valley's TriSawLFO).
// ---------------------------------------------------------------------------
struct TriSawLFO {
    float sr = 48000.f, freq = 1.f, step = 0.f, stepSize = 0.f;
    float rev = 0.5f, riseRate = 2.f, fallRate = -2.f;
    bool rising = true;

    void setSampleRate(float s) { sr = s; stepSize = freq / sr; }
    void setFrequency(float fr) { freq = fr; stepSize = freq / sr; }
    void setRevPoint(float r) {
        rev = clampv(r, 0.0001f, 0.999f);
        riseRate = 1.f / rev;
        fallRate = -1.f / (1.f - rev);
    }
    inline float process() {
        if (step > 1.f) { step -= 1.f; rising = true; }
        if (step >= rev) rising = false;
        float out = rising ? step * riseRate : step * fallRate - fallRate;
        step += stepSize;
        return out * 2.f - 1.f;
    }
};

// Valley's piecewise tanh approximation (output saturation option).
static inline float tanhDrive(float x, float drive) {
    x *= drive;
    if (x < -1.25f) return -1.f;
    if (x < -0.75f) return 1.f - (x * (-2.5f - x) - 0.5625f) - 1.f;
    if (x > 1.25f) return 1.f;
    if (x > 0.75f) return x * (2.5f - x) - 0.5625f;
    return x;
}

// ---------------------------------------------------------------------------
// Memory layout. All delay lengths derive from the sample rate, so the
// plug-in can size its DRAM/DTC requests in calculateRequirements().
// ---------------------------------------------------------------------------
struct Layout {
    static constexpr float kDattorroRate = 29761.f;
    static constexpr float kMaxTimeScale = 4.f;      // Plateau sizeMax
    static constexpr float kMaxLfoDepth = 16.f;      // Plateau modDepthMax
    static constexpr int32_t kMaxOutTap = 2974;      // largest output tap
    static constexpr float kMaxPreDelaySec = 0.5f;   // Plateau pre-delay range

    // tank: lApf1, lDel1, lApf2, lDel2, rApf1, rDel1, rApf2, rDel2
    static constexpr float kTankTimes[8] = {672.f, 4453.f, 1800.f, 3720.f,
                                            908.f, 4217.f, 2656.f, 3163.f};
    // input diffusers
    static constexpr float kInApfTimes[4] = {141.f, 107.f, 379.f, 277.f};

    int32_t tankLen[8];
    int32_t inApfLen[4];
    int32_t preDelayLen;

    void compute(float sampleRate) {
        const float s = sampleRate / kDattorroRate;
        for (int i = 0; i < 8; ++i)
            tankLen[i] = (int32_t)(s * (kTankTimes[i] * kMaxTimeScale + (float)kMaxOutTap + kMaxLfoDepth)) + 4;
        for (int i = 0; i < 4; ++i)
            inApfLen[i] = (int32_t)(s * kInApfTimes[i]) + 4;
        preDelayLen = (int32_t)(kMaxPreDelaySec * sampleRate) + 4;
    }
    uint32_t dramFloats() const {
        uint32_t n = (uint32_t)preDelayLen;
        for (int i = 0; i < 8; ++i) n += (uint32_t)tankLen[i];
        return n;
    }
    uint32_t dtcFloats() const {
        uint32_t n = 0;
        for (int i = 0; i < 4; ++i) n += (uint32_t)inApfLen[i];
        return n;
    }
};

// ---------------------------------------------------------------------------
// The Dattorro tank + input section (Valley's Dattorro1997Tank + Dattorro).
// ---------------------------------------------------------------------------
class Reverb {
public:
    // dram must hold layout.dramFloats(), dtc layout.dtcFloats() floats.
    void init(float sampleRate, float* dram, float* dtc) {
        sr = sampleRate;
        srScale = sr / Layout::kDattorroRate;
        layout.compute(sr);

        float* p = dram;
        preDelay.init(p, layout.preDelayLen); p += layout.preDelayLen;
        Delay* tankLines[8] = {&lApf1.delay, &lDel1, &lApf2.delay, &lDel2,
                               &rApf1.delay, &rDel1, &rApf2.delay, &rDel2};
        for (int i = 0; i < 8; ++i) { tankLines[i]->init(p, layout.tankLen[i]); p += layout.tankLen[i]; }

        float* q = dtc;
        Allpass* ins[4] = {&inApf1, &inApf2, &inApf3, &inApf4};
        for (int i = 0; i < 4; ++i) {
            ins[i]->delay.init(q, layout.inApfLen[i]);
            q += layout.inApfLen[i];
            ins[i]->delay.setDelayTime(Layout::kInApfTimes[i] * srScale);
        }
        inApf1.gain = inApf2.gain = 0.75f;
        inApf3.gain = inApf4.gain = 0.625f;

        static const int32_t kTaps[7] = {266, 2974, 1913, 1996, 1990, 187, 1066};
        for (int i = 0; i < 7; ++i) taps[i] = (int32_t)((float)kTaps[i] * srScale);

        lDcIn.setSampleRate(sr);  lDcIn.setCutoff(20.f);
        rDcIn.setSampleRate(sr);  rDcIn.setCutoff(20.f);
        inLp.setSampleRate(sr);   inLp.setCutoff(20000.f);
        inHp.setSampleRate(sr);   inHp.setCutoff(10.f);
        lHighCut.setSampleRate(sr); rHighCut.setSampleRate(sr);
        lLowCut.setSampleRate(sr);  rLowCut.setSampleRate(sr);
        lDcOut.setSampleRate(sr); lDcOut.setCutoff(20.f);
        rDcOut.setSampleRate(sr); rDcOut.setCutoff(20.f);

        lfo[0].setFrequency(0.10f); lfo[1].setFrequency(0.15f);
        lfo[2].setFrequency(0.12f); lfo[3].setFrequency(0.18f);
        for (auto& l : lfo) { l.setSampleRate(sr); l.setRevPoint(0.5f); }

        fadeStep = 1.f / sr;   // as in the original: ~1 s crossfade
        setTimeScale(1.f);
        setDecay(0.5f);
        setDiffusion(10.f);
        setModSpeed(1.f);
        setModDepth(0.5f);
        setPreDelay(0.f);
        clearAll();
    }

    // ---- parameters (same units as Valley's Dattorro class) ---------------
    void setTimeScale(float ts) {
        timeScale = ts < 0.0001f ? 0.0001f : (ts > Layout::kMaxTimeScale ? Layout::kMaxTimeScale : ts);
        const float k = timeScale * srScale;
        sApf[0] = 672.f * k;  sApf[1] = 1800.f * k;
        sApf[2] = 908.f * k;  sApf[3] = 2656.f * k;
        lDel1.setDelayTime(4453.f * k);
        lDel2.setDelayTime(3720.f * k);
        rDel1.setDelayTime(4217.f * k);
        rDel2.setDelayTime(3163.f * k);
    }
    void setPreDelay(float seconds) { preDelay.setDelayTime(seconds * sr); }
    void setDecay(float d) { decayParam = clampv(d, 0.f, 1.f); }
    void setDiffusion(float d) {                      // 0..10
        const float g = clampv(d, 0.f, 10.f) * 0.07f;  // max 0.7
        lApf1.gain = -g; rApf1.gain = -g;
        lApf2.gain = g;  rApf2.gain = g;
    }
    void setInputDiffusion(bool on) { diffuseInput = on ? 1.f : 0.f; }
    void setInputLowCut(float hz)  { inHp.setCutoff(hz); }
    void setInputHighCut(float hz) { inLp.setCutoff(hz); }
    void setTankLowCut(float hz)   { lLowCut.setCutoff(hz); rLowCut.setCutoff(hz); }
    void setTankHighCut(float hz)  { lHighCut.setCutoff(hz); rHighCut.setCutoff(hz); }
    void setModSpeed(float m) {
        lfo[0].setFrequency(0.10f * m); lfo[1].setFrequency(0.15f * m);
        lfo[2].setFrequency(0.12f * m); lfo[3].setFrequency(0.18f * m);
    }
    void setModDepth(float d) { lfoExcursion = d * 16.f * srScale; }   // 0..16 (Plateau range)
    void setModShape(float s) { for (auto& l : lfo) l.setRevPoint(s); }
    void freeze(bool on) { frozen = on; fadeDir = on ? -1.f : 1.f; }

    // ---- audio -------------------------------------------------------------
    inline void process(float inL, float inR, float& outL, float& outR) {
        // input section
        float x = inLp.process(lDcIn.process(inL) + rDcIn.process(inR));
        x = inHp.process(x);
        preDelay.input = x;
        preDelay.process();
        inApf1.input = preDelay.output;
        inApf2.input = inApf1.process();
        inApf3.input = inApf2.process();
        inApf4.input = inApf3.process();
        const float d4 = inApf4.process();
        const float feed = preDelay.output + (d4 - preDelay.output) * diffuseInput;

        // tank modulation
        lApf1.delay.setDelayTime(lfo[0].process() * lfoExcursion + sApf[0]);
        lApf2.delay.setDelayTime(lfo[1].process() * lfoExcursion + sApf[1]);
        rApf1.delay.setDelayTime(lfo[2].process() * lfoExcursion + sApf[2]);
        rApf2.delay.setDelayTime(lfo[3].process() * lfoExcursion + sApf[3]);

        const float decay = frozen ? 1.f : decayParam;
        lSum += feed;
        rSum += feed;

        lApf1.input = lSum;
        lDel1.input = lApf1.process();
        lDel1.process();
        const float lf = lLowCut.process(lHighCut.process(lDel1.output));
        lApf2.input = (lDel1.output * (1.f - fade) + lf * fade) * decay;
        lDel2.input = lApf2.process();
        lDel2.process();

        rApf1.input = rSum;
        rDel1.input = rApf1.process();
        rDel1.process();
        const float rf = rLowCut.process(rHighCut.process(rDel1.output));
        rApf2.input = (rDel1.output * (1.f - fade) + rf * fade) * decay;
        rDel2.input = rApf2.process();
        rDel2.process();

        rSum = lDel2.output * decay;
        lSum = rDel2.output * decay;

        float l = lApf1.output;
        l += lDel1.tap(taps[0]);
        l += lDel1.tap(taps[1]);
        l -= lApf2.delay.tap(taps[2]);
        l += lDel2.tap(taps[3]);
        l -= rDel1.tap(taps[4]);
        l -= rApf2.delay.tap(taps[5]);
        l -= rDel2.tap(taps[6]);

        float r = rApf1.output;
        r += rDel1.tap(taps[0]);
        r += rDel1.tap(taps[1]);
        r -= rApf2.delay.tap(taps[2]);
        r += rDel2.tap(taps[3]);
        r -= lDel1.tap(taps[4]);
        r -= lApf2.delay.tap(taps[5]);
        r -= lDel2.tap(taps[6]);

        outL = lDcOut.process(l) * 0.5f;
        outR = rDcOut.process(r) * 0.5f;

        fade += fadeStep * fadeDir;
        fade = fade < 0.f ? 0.f : (fade > 1.f ? 1.f : fade);
    }

    // ---- clearing ------------------------------------------------------------
    void clearAll() {
        clearStart();
        while (!clearStep(0x7fffffff)) {}
    }
    // Begin a chunked clear. Filter states are reset immediately.
    void clearStart() {
        clearLine = 0; clearPos = 0;
        lDcIn.clear(); rDcIn.clear(); inLp.clear(); inHp.clear();
        lHighCut.clear(); rHighCut.clear(); lLowCut.clear(); rLowCut.clear();
        lDcOut.clear(); rDcOut.clear();
        lSum = rSum = 0.f;
    }
    // Zero up to maxFloats samples of delay memory. Returns true when done.
    bool clearStep(int32_t maxFloats) {
        Delay* lines[13] = {&preDelay, &inApf1.delay, &inApf2.delay, &inApf3.delay, &inApf4.delay,
                            &lApf1.delay, &lDel1, &lApf2.delay, &lDel2,
                            &rApf1.delay, &rDel1, &rApf2.delay, &rDel2};
        while (maxFloats > 0 && clearLine < 13) {
            Delay* d = lines[clearLine];
            int32_t n = d->len - clearPos;
            if (n > maxFloats) n = maxFloats;
            memset(d->buf + clearPos, 0, sizeof(float) * (size_t)n);
            clearPos += n; maxFloats -= n;
            if (clearPos >= d->len) {
                d->input = d->output = 0.f;
                ++clearLine; clearPos = 0;
            }
        }
        if (clearLine >= 13) {
            Allpass* aps[8] = {&inApf1, &inApf2, &inApf3, &inApf4, &lApf1, &lApf2, &rApf1, &rApf2};
            for (auto* a : aps) a->input = a->output = 0.f;
            return true;
        }
        return false;
    }

    Layout layout;
    // Phase (0..1) of the first tank LFO, for the display.
    float lfoPhase() const { const float st = lfo[0].step; return st > 1.f ? st - 1.f : st; }

private:
    float sr = 48000.f, srScale = 1.f;
    float timeScale = 1.f;
    float decayParam = 0.5f;
    float diffuseInput = 1.f;
    float lfoExcursion = 0.f;
    float sApf[4] = {0, 0, 0, 0};
    int32_t taps[7] = {0, 0, 0, 0, 0, 0, 0};

    bool  frozen = false;
    float fade = 1.f, fadeStep = 1.f / 48000.f, fadeDir = 1.f;
    float lSum = 0.f, rSum = 0.f;

    OnePoleHP lDcIn, rDcIn, inHp, lLowCut, rLowCut, lDcOut, rDcOut;
    OnePoleLP inLp, lHighCut, rHighCut;
    Delay preDelay;
    Allpass inApf1, inApf2, inApf3, inApf4;
    Allpass lApf1, lApf2, rApf1, rApf2;
    Delay lDel1, lDel2, rDel1, rDel2;
    TriSawLFO lfo[4];

    int32_t clearLine = 13, clearPos = 0;
};

} // namespace sary
