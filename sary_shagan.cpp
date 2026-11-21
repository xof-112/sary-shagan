/*
 * Sary-Shagan – Dattorro plate reverb for the Expert Sleepers disting NT
 *
 * A port of the Plateau reverb by Dale Johnson (Valley Audio Soft),
 * https://github.com/ValleyAudio/ValleyRackFree
 * Controls and CV inputs follow the hardware descendant of Plateau, Mesa
 * (4ms Company + Valley Audio): one bipolar Input Filter and Reverb Filter,
 * Dry/Wet crossfade, Input Level, ten CV inputs (±5 V = full knob range),
 * Freeze and Clear gates, Size CV as pitch CV in Tuned mode.
 *
 * Copyright (C) 2018-2023 Dale Johnson, Valley Audio Soft (Plateau)
 * Copyright (C) 2026 xof (disting NT port)
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
 */

#include <distingnt/api.h>
#include <new>
#include <math.h>
#include <string.h>
#include "sary_shagan_dsp.h"

// Plug-in version (handbook, release notes). The VCV module has its own number in vcv/plugin.json.
#define SARY_SHAGAN_VERSION "0.4"

using namespace sary;

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------
enum {
    // Input
    kParamInputLevel,
    kParamPreDelay,
    kParamDiffuseInput,
    // Space
    kParamDiffusion,
    kParamSize,
    kParamDecay,
    // Tone
    kParamInputFilter,
    kParamDryWet,
    kParamReverbFilter,
    // Modulation
    kParamModDepth,
    kParamModRate,
    kParamModShape,
    // Modes
    kParamFreeze,
    kParamFreezeMode,
    kParamClear,
    kParamTuned,
    kParamSaturation,
    // CV inputs (Mesa order)
    kParamCvInFilter,
    kParamCvDiffusion,
    kParamCvSize,
    kParamCvDryWet,
    kParamCvRvbFilter,
    kParamCvPreDelay,
    kParamCvModDepth,
    kParamCvModRate,
    kParamCvModShape,
    kParamCvDecay,
    kParamFreezeGate,
    kParamClearGate,
    // Routing
    kParamInL,
    kParamInR,
    kParamOutL,
    kParamOutLMode,
    kParamOutR,
    kParamOutRMode,

    kNumParams
};
enum { kNumCvInputs = kParamCvDecay - kParamCvInFilter + 1 };

static const char* const kOffOn[] = {"Off", "On"};
static const char* const kFreezeModes[] = {"Momentary", "Latch"};

static const _NT_parameter parameters[] = {
    {.name = "Input level",    .min = -600, .max = 25,   .def = 0,    .unit = kNT_unitDb_minInf, .scaling = kNT_scaling10, .enumStrings = NULL},
    {.name = "Pre-delay",      .min = 0,    .max = 500,  .def = 0,    .unit = kNT_unitMs,        .scaling = 0,             .enumStrings = NULL},
    {.name = "Diffuse input",  .min = 0,    .max = 1,    .def = 1,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kOffOn},

    {.name = "Diffusion",      .min = 0,    .max = 1000, .def = 1000, .unit = kNT_unitPercent,   .scaling = kNT_scaling10, .enumStrings = NULL},
    {.name = "Size",           .min = 0,    .max = 1000, .def = 500,  .unit = kNT_unitPercent,   .scaling = kNT_scaling10, .enumStrings = NULL},
    {.name = "Decay",          .min = 0,    .max = 1000, .def = 500,  .unit = kNT_unitPercent,   .scaling = kNT_scaling10, .enumStrings = NULL},

    {.name = "Input filter",   .min = -1000, .max = 1000, .def = 0,   .unit = kNT_unitHasStrings, .scaling = 0,            .enumStrings = NULL},
    {.name = "Dry/Wet",        .min = 0,    .max = 1000, .def = 500,  .unit = kNT_unitPercent,   .scaling = kNT_scaling10, .enumStrings = NULL},
    {.name = "Reverb filter",  .min = -1000, .max = 1000, .def = 0,   .unit = kNT_unitHasStrings, .scaling = 0,            .enumStrings = NULL},

    {.name = "Mod depth",      .min = 0,    .max = 1000, .def = 50,   .unit = kNT_unitPercent,   .scaling = kNT_scaling10, .enumStrings = NULL},
    {.name = "Mod rate",       .min = 0,    .max = 1000, .def = 0,    .unit = kNT_unitHasStrings, .scaling = 0,            .enumStrings = NULL},
    {.name = "Mod shape",      .min = 0,    .max = 1000, .def = 500,  .unit = kNT_unitHasStrings, .scaling = 0,            .enumStrings = NULL},

    {.name = "Freeze",         .min = 0,    .max = 1,    .def = 0,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kOffOn},
    {.name = "Freeze mode",    .min = 0,    .max = 1,    .def = 0,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kFreezeModes},
    {.name = "Clear",          .min = 0,    .max = 1,    .def = 0,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kOffOn},
    {.name = "Tuned",          .min = 0,    .max = 1,    .def = 0,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kOffOn},
    {.name = "Saturation",     .min = 0,    .max = 1,    .def = 1,    .unit = kNT_unitEnum,      .scaling = 0,             .enumStrings = kOffOn},

    NT_PARAMETER_CV_INPUT("In filter CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Diffusion CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Size CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Dry/Wet CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Rvb filter CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Pre-delay CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Mod depth CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Mod rate CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Mod shape CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Decay CV", 0, 0)
    NT_PARAMETER_CV_INPUT("Freeze gate", 0, 0)
    NT_PARAMETER_CV_INPUT("Clear gate", 0, 0)

    NT_PARAMETER_AUDIO_INPUT("Input L", 1, 1)
    NT_PARAMETER_AUDIO_INPUT("Input R", 0, 2)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output L", 1, 13)
    NT_PARAMETER_AUDIO_OUTPUT_WITH_MODE("Output R", 1, 14)
};
static_assert(ARRAY_SIZE(parameters) == kNumParams, "parameter table mismatch");

// Which knob parameter each CV input adds to (Mesa: CV ±5 V = full knob range).
static const uint8_t kCvTarget[kNumCvInputs] = {
    kParamInputFilter, kParamDiffusion, kParamSize, kParamDryWet, kParamReverbFilter,
    kParamPreDelay, kParamModDepth, kParamModRate, kParamModShape, kParamDecay};

static const uint8_t pageInput[]  = {kParamInputLevel, kParamPreDelay, kParamDiffuseInput};
static const uint8_t pageSpace[]  = {kParamDiffusion, kParamSize, kParamDecay};
static const uint8_t pageTone[]   = {kParamInputFilter, kParamDryWet, kParamReverbFilter};
static const uint8_t pageMod[]    = {kParamModDepth, kParamModRate, kParamModShape};
static const uint8_t pageModes[]  = {kParamFreeze, kParamFreezeMode, kParamClear, kParamTuned, kParamSaturation};
static const uint8_t pageCv[]     = {kParamCvInFilter, kParamCvDiffusion, kParamCvSize, kParamCvDryWet, kParamCvRvbFilter,
                                     kParamCvPreDelay, kParamCvModDepth, kParamCvModRate, kParamCvModShape, kParamCvDecay,
                                     kParamFreezeGate, kParamClearGate};
static const uint8_t pageRouting[] = {kParamInL, kParamInR, kParamOutL, kParamOutLMode, kParamOutR, kParamOutRMode};

static const _NT_parameterPage pages[] = {
    {.name = "Input",      .numParams = ARRAY_SIZE(pageInput),   .group = 0, .unused = {0, 0}, .params = pageInput},
    {.name = "Space",      .numParams = ARRAY_SIZE(pageSpace),   .group = 0, .unused = {0, 0}, .params = pageSpace},
    {.name = "Tone",       .numParams = ARRAY_SIZE(pageTone),    .group = 0, .unused = {0, 0}, .params = pageTone},
    {.name = "Modulation", .numParams = ARRAY_SIZE(pageMod),     .group = 0, .unused = {0, 0}, .params = pageMod},
    {.name = "Modes",      .numParams = ARRAY_SIZE(pageModes),   .group = 0, .unused = {0, 0}, .params = pageModes},
    {.name = "CV inputs",  .numParams = ARRAY_SIZE(pageCv),      .group = 0, .unused = {0, 0}, .params = pageCv},
    {.name = "Routing",    .numParams = ARRAY_SIZE(pageRouting), .group = 0, .unused = {0, 0}, .params = pageRouting},
};

static const _NT_parameterPages parameterPages = {
    .numPages = ARRAY_SIZE(pages),
    .pages = pages,
};

// ---------------------------------------------------------------------------
// Mappings (knob position 0..1 -> DSP value)
// ---------------------------------------------------------------------------
static inline float exp2Fast(float x) { return expf(x * 0.69314718f); }

static inline float paramNormV(int p, int v) {
    const _NT_parameter& d = parameters[p];
    return (float)(v - d.min) / (float)(d.max - d.min);
}

// Plateau's decay knob range and curve.
static inline float decayFromNorm(float n) {
    float d = 0.1f + (0.9999f - 0.1f) * n;
    d = 1.f - d;
    return 1.f - d * d;
}

// Plateau's size curves. s = knob position 0..1. In Tuned mode the Size CV is a
// pitch CV (Mesa: exponential, polarity reversed): +1 V = tank an octave shorter.
static inline float timeScaleFromSize(float s, bool tuned, float cvOct) {
    if (tuned) {
        float ts = 0.0025f * exp2Fast(s * 5.f - cvOct);
        return clampv(ts, 0.0025f, 2.5f);
    }
    float ts = s * s;
    ts = 0.01f + ts * (4.f - 0.01f);
    return clampv(ts, 0.01f, 4.f);
}

// Mesa's single filter knob: centre = no filtering, turned up = low cut rises
// (brighter, thinner), turned down = high cut falls (darker). b in -1..1.
static const float kCutMin = 13.75f, kCutMax = 14080.f;
static inline void filterFromBipolar(float b, float& lowCut, float& highCut) {
    lowCut = kCutMin;
    highCut = kCutMax;
    if (b > 0.f) lowCut = kCutMin * exp2Fast(b * 8.f);     // up to 3.5 kHz
    else if (b < 0.f) highCut = kCutMax * exp2Fast(b * 7.f); // down to 110 Hz
}

// sin(x * pi/2) for x in 0..1 (Taylor to x^5, < 0.5 % off): equal-power crossfade without libm.
static inline float sinQuarter(float x) {
    const float t = x * 1.5707963f, t2 = t * t;
    return t * (1.f - t2 * (1.f / 6.f - t2 * (1.f / 120.f))) * 0.99549545f;   // exactly 1 at x = 1
}

static inline float modRateFactor(float n) { return n * n * 99.f + 1.f; }   // Plateau: 1..100
static inline float inputGainFromParam(int v) {                             // tenths of dB
    return v <= parameters[kParamInputLevel].min ? 0.f : exp2Fast((float)v * 0.1f * 0.16609640f);
}

// ---------------------------------------------------------------------------
// Algorithm
// ---------------------------------------------------------------------------
struct Smoother {
    float value = 0.f, target = 0.f, coeff = 0.f;
    void setTime(float seconds, float sr) { coeff = 1.f - expf(-1.f / (seconds * sr)); }
    void snap(float v) { value = target = v; }
    inline float next() { value += (target - value) * coeff; return value; }
};

enum { kNumUiPages = 4 };

struct SaryShaganAlgorithm : public _NT_algorithm {
    Reverb reverb;
    float sr = 48000.f;

    Smoother size;       // knob position 0..1 (before the curve)
    Smoother decay;      // tank decay coefficient
    Smoother dryWet;     // 0..1
    Smoother modDepth;   // 0..16
    Smoother preDelay;   // seconds
    Smoother inGain;     // linear
    bool tuned = false;
    bool saturate = true;

    // effective knob positions (0..1) including CV, for the display
    float eff[kNumParams];
    float cvSizeVolts = 0.f;

    // freeze / clear
    bool freezeHeld = false;      // button 3 held (momentary)
    bool freezeGateHigh = false;
    bool frozen = false;
    bool clearParamPrev = false;
    bool clearGateHigh = false;

    enum ClearPhase { kClearIdle, kClearFadeOut, kClearWipe, kClearFadeIn };
    ClearPhase clearPhase = kClearIdle;
    float env = 1.f;
    float envStep = 0.f;

    // custom UI
    int page = 1;                 // 0 INPUT, 1 SPACE, 2 TONE, 3 MOD
    int fineParam = kParamSize;   // what Encoder R fine-tunes: last pot touched
    bool caught[3] = {false, false, false};
    float potPos[3] = {-1.f, -1.f, -1.f};
    float meter = 0.f;            // wet output peak with release (V)
    float clip = 0.f;             // input clip indicator (s left)
    bool started = false;         // smoothers jump to their targets on the first block
};

static void setParamAudio(SaryShaganAlgorithm* a, int p, int value) {
#ifdef NT_EMU_WIN
    if (a->v) { const_cast<int16_t*>(a->v)[p] = (int16_t)value; return; }
#endif
    NT_setParameterFromAudio(NT_algorithmIndex(a), p + NT_parameterOffset(), (int16_t)value);
}

static void calculateRequirements(_NT_algorithmRequirements& req, const int32_t* specifications) {
    Layout l;
    l.compute((float)NT_globals.sampleRate);
    req.numParameters = ARRAY_SIZE(parameters);
    req.sram = sizeof(SaryShaganAlgorithm);
    req.dram = l.dramFloats() * sizeof(float);
    req.dtc = l.dtcFloats() * sizeof(float);
    req.itc = 0;
}

static _NT_algorithm* construct(const _NT_algorithmMemoryPtrs& ptrs, const _NT_algorithmRequirements& req,
                                const int32_t* specifications) {
    SaryShaganAlgorithm* alg = new (ptrs.sram) SaryShaganAlgorithm();
    alg->parameters = parameters;
    alg->parameterPages = &parameterPages;

    alg->sr = (float)NT_globals.sampleRate;
    alg->reverb.init(alg->sr, (float*)ptrs.dram, (float*)ptrs.dtc);

    alg->size.setTime(0.05f, alg->sr);
    alg->decay.setTime(0.02f, alg->sr);
    alg->dryWet.setTime(0.01f, alg->sr);
    alg->modDepth.setTime(0.05f, alg->sr);
    alg->preDelay.setTime(0.05f, alg->sr);
    alg->inGain.setTime(0.01f, alg->sr);
    alg->size.snap(0.5f);
    alg->decay.snap(decayFromNorm(0.5f));
    alg->dryWet.snap(0.5f);
    alg->modDepth.snap(0.8f);
    alg->preDelay.snap(0.f);
    alg->inGain.snap(1.f);
    alg->envStep = 1.f / (0.004f * alg->sr);
    for (int p = 0; p < kNumParams; ++p) alg->eff[p] = paramNormV(p, parameters[p].def);
    return alg;
}

static void parameterChanged(_NT_algorithm* self, int p) {
    SaryShaganAlgorithm* a = (SaryShaganAlgorithm*)self;
    const int16_t* v = a->v;
    switch (p) {
    case kParamDiffuseInput: a->reverb.setInputDiffusion(v[p] != 0); break;
    case kParamTuned:        a->tuned = v[p] != 0; break;
    case kParamSaturation:   a->saturate = v[p] != 0; break;
    case kParamFreezeMode:   a->freezeHeld = false; break;
    case kParamClear: {
        const bool on = v[p] != 0;
        if (on && !a->clearParamPrev && a->clearPhase == SaryShaganAlgorithm::kClearIdle)
            a->clearPhase = SaryShaganAlgorithm::kClearFadeOut;
        a->clearParamPrev = on;
        break;
    }
    default: break;   // knob parameters are read every block in step(), with their CVs
    }
}

static inline float busAverage(const float* bus, int n) {
    float s = 0.f;
    for (int i = 0; i < n; ++i) s += bus[i];
    return s / (float)n;
}

// Gate with Mesa's ~2 V threshold and some hysteresis.
static inline bool gateLevel(bool wasHigh, float volts) { return wasHigh ? volts > 1.f : volts > 2.f; }

static void step(_NT_algorithm* self, float* busFrames, int numFramesBy4) {
    SaryShaganAlgorithm* a = (SaryShaganAlgorithm*)self;
    const int16_t* v = a->v;
    Reverb& r = a->reverb;
    const int n = numFramesBy4 * 4;

    const float* inL = busFrames + (v[kParamInL] - 1) * n;
    const float* inR = v[kParamInR] ? busFrames + (v[kParamInR] - 1) * n : inL;
    float* outL = busFrames + (v[kParamOutL] - 1) * n;
    float* outR = busFrames + (v[kParamOutR] - 1) * n;
    const bool replaceL = v[kParamOutLMode] != 0;
    const bool replaceR = v[kParamOutRMode] != 0;

    // ---- knobs + CV (block rate) -------------------------------------------------
    for (int p = 0; p < kNumParams; ++p) a->eff[p] = paramNormV(p, v[p]);
    a->cvSizeVolts = 0.f;
    for (int k = 0; k < kNumCvInputs; ++k) {
        const int bus = v[kParamCvInFilter + k];
        if (!bus) continue;
        const float volts = busAverage(busFrames + (bus - 1) * n, n);
        const int target = kCvTarget[k];
        if (target == kParamSize && a->tuned) { a->cvSizeVolts = volts; continue; }
        a->eff[target] = clampv(a->eff[target] + volts * 0.2f, 0.f, 1.f);
    }
    a->size.target = a->eff[kParamSize];
    a->decay.target = decayFromNorm(a->eff[kParamDecay]);
    a->dryWet.target = a->eff[kParamDryWet];
    a->modDepth.target = a->eff[kParamModDepth] * 16.f;
    a->preDelay.target = a->eff[kParamPreDelay] * 0.5f;
    a->inGain.target = inputGainFromParam(v[kParamInputLevel]);
    r.setDiffusion(a->eff[kParamDiffusion] * 10.f);
    r.setModSpeed(modRateFactor(a->eff[kParamModRate]));
    r.setModShape(0.001f + a->eff[kParamModShape] * 0.998f);
    float lo, hi;
    filterFromBipolar(a->eff[kParamInputFilter] * 2.f - 1.f, lo, hi);
    r.setInputLowCut(lo);  r.setInputHighCut(hi);
    filterFromBipolar(a->eff[kParamReverbFilter] * 2.f - 1.f, lo, hi);
    r.setTankLowCut(lo);   r.setTankHighCut(hi);
    if (!a->started) {
        a->started = true;
        Smoother* all[] = {&a->size, &a->decay, &a->dryWet, &a->modDepth, &a->preDelay, &a->inGain};
        for (Smoother* sm : all) sm->snap(sm->target);
    }

    // ---- freeze: param (latched), button 3 held, gate ------------------------------
    const bool latch = v[kParamFreezeMode] != 0;
    if (v[kParamFreezeGate]) {
        const bool high = gateLevel(a->freezeGateHigh, busAverage(busFrames + (v[kParamFreezeGate] - 1) * n, n));
        if (latch && high && !a->freezeGateHigh) setParamAudio(a, kParamFreeze, v[kParamFreeze] ? 0 : 1);
        a->freezeGateHigh = high;
    } else {
        a->freezeGateHigh = false;
    }
    const bool freeze = v[kParamFreeze] || a->freezeHeld || (!latch && a->freezeGateHigh);
    if (freeze != a->frozen) { a->frozen = freeze; r.freeze(freeze); }

    // ---- clear ----------------------------------------------------------------------------
    if (v[kParamClearGate]) {
        const bool high = gateLevel(a->clearGateHigh, busAverage(busFrames + (v[kParamClearGate] - 1) * n, n));
        if (high && !a->clearGateHigh && a->clearPhase == SaryShaganAlgorithm::kClearIdle)
            a->clearPhase = SaryShaganAlgorithm::kClearFadeOut;
        a->clearGateHigh = high;
    }
    if (a->clearPhase == SaryShaganAlgorithm::kClearWipe) {
        if (r.clearStep(65536)) a->clearPhase = SaryShaganAlgorithm::kClearFadeIn;
    }

    const float cvOct = a->tuned ? a->cvSizeVolts : 0.f;
    const bool sat = a->saturate;
    float blockPeak = 0.f, inPeak = 0.f;

    for (int i = 0; i < n; ++i) {
        float env = a->env;
        switch (a->clearPhase) {
        case SaryShaganAlgorithm::kClearFadeOut:
            env -= a->envStep;
            if (env <= 0.f) { env = 0.f; r.clearStart(); a->clearPhase = SaryShaganAlgorithm::kClearWipe; }
            break;
        case SaryShaganAlgorithm::kClearWipe:
            env = 0.f;
            break;
        case SaryShaganAlgorithm::kClearFadeIn:
            env += a->envStep;
            if (env >= 1.f) { env = 1.f; a->clearPhase = SaryShaganAlgorithm::kClearIdle; }
            break;
        default: break;
        }
        a->env = env;

        r.setTimeScale(timeScaleFromSize(a->size.next(), a->tuned, cvOct));
        r.setDecay(a->decay.next());
        r.setModDepth(a->modDepth.next());
        r.setPreDelay(a->preDelay.next());
        const float g = a->inGain.next();
        const float mix = a->dryWet.next();
        // Mesa: CCW only dry, CW only wet, centre an even blend (equal power).
        // Wet ×10 as in Plateau: input enters at 1/10, the tank sums at 1/2.
        const float dryGain = sinQuarter(1.f - mix);
        const float wetGain = 10.f * sinQuarter(mix);

        const float xl = inL[i] * g, xr = inR[i] * g;
        const float ip = fabsf(xl) > fabsf(xr) ? fabsf(xl) : fabsf(xr);
        if (ip > inPeak) inPeak = ip;
        float yl, yr;
        if (a->clearPhase == SaryShaganAlgorithm::kClearWipe) {
            yl = yr = 0.f;
        } else {
            r.process(xl * 0.1f * env, xr * 0.1f * env, yl, yr);
        }
        const float wl = yl * wetGain * env, wr = yr * wetGain * env;
        const float wp = fabsf(wl) > fabsf(wr) ? fabsf(wl) : fabsf(wr);
        if (wp > blockPeak) blockPeak = wp;

        float ol = inL[i] * dryGain + wl;
        float or_ = inR[i] * dryGain + wr;
        if (sat) {   // unity up to 7.5 V, then soft into 10 V (Mesa: kicks in at 75 %)
            ol = tanhDrive(ol * 0.1f, 1.f) * 10.f;
            or_ = tanhDrive(or_ * 0.1f, 1.f) * 10.f;
        } else {
            ol = clampv(ol, -10.f, 10.f);
            or_ = clampv(or_, -10.f, 10.f);
        }
        if (replaceL) outL[i] = ol; else outL[i] += ol;
        if (replaceR) outR[i] = or_; else outR[i] += or_;
    }

    const float blockSec = (float)n / a->sr;
    const float rel = expf(-blockSec / 1.5f);
    a->meter = blockPeak > a->meter ? blockPeak : a->meter * rel;
    a->clip = inPeak > 9.6f ? 0.3f : (a->clip > blockSec ? a->clip - blockSec : 0.f);
}

// ---------------------------------------------------------------------------
// Value strings
// ---------------------------------------------------------------------------
static int hzString(char* buff, float hz) {
    int len;
    if (hz >= 1000.f) {
        len = NT_floatToString(buff, hz * 0.001f, hz < 10000.f ? 2 : 1);
        buff[len++] = 'k';
    } else {
        len = NT_floatToString(buff, hz, hz < 100.f ? 1 : 0);
    }
    buff[len++] = 'H'; buff[len++] = 'z'; buff[len] = 0;
    return len;
}

static int filterString(char* buff, int v) {
    if (v == 0) { memcpy(buff, "Off", 4); return 3; }
    float lo, hi;
    filterFromBipolar((float)v * 0.001f, lo, hi);
    memcpy(buff, v > 0 ? "LC " : "HC ", 3);
    return 3 + hzString(buff + 3, v > 0 ? lo : hi);
}

static int parameterString(_NT_algorithm* self, int p, int v, char* buff) {
    switch (p) {
    case kParamInputFilter:
    case kParamReverbFilter:
        return filterString(buff, v);
    case kParamModRate: {   // speed of the first tank LFO
        int len = NT_floatToString(buff, 0.1f * modRateFactor(paramNormV(p, v)), 2);
        memcpy(buff + len, " Hz", 4);
        return len + 3;
    }
    case kParamModShape: {
        if (v < 100) { memcpy(buff, "Ramp down", 10); return 9; }
        if (v > 900) { memcpy(buff, "Ramp up", 8); return 7; }
        if (v > 450 && v < 550) { memcpy(buff, "Triangle", 9); return 8; }
        int len = NT_intToString(buff, (v + 5) / 10);
        buff[len++] = '%'; buff[len] = 0;
        return len;
    }
    default:
        return 0;
    }
}

// ---------------------------------------------------------------------------
// Custom UI
//
// Four pages of three pots, arranged like Mesa's panel groups:
//
//   INPUT   Pot L Input level   Pot C Pre-delay   Pot R –
//   SPACE   Pot L Diffusion     Pot C Size        Pot R Decay
//   TONE    Pot L Input filter  Pot C Dry/Wet     Pot R Reverb filter
//   MOD     Pot L Mod depth     Pot C Mod rate    Pot R Mod shape
//
//   Encoder L turn   page              Encoder L push   Diffuse input on/off
//   Encoder R turn   fine-tune the     Encoder R push   Tuned on/off
//                    last pot touched
//   Button 3         Freeze (momentary, or toggles in Latch mode)
//   Button 4         Clear
//
// Pots pick up: after a page change a pot only writes once it passes the
// current value, so nothing jumps.
// ---------------------------------------------------------------------------
struct UiSlot { int8_t param; const char* label; };
struct UiPage { const char* name; UiSlot pots[3]; };

static const UiPage kUiPages[kNumUiPages] = {
    {"INPUT", {{kParamInputLevel, "Level"},     {kParamPreDelay, "Pre-delay"}, {-1, "Diffuse"}}},
    {"SPACE", {{kParamDiffusion, "Diffusion"},  {kParamSize, "Size"},          {kParamDecay, "Decay"}}},
    {"TONE",  {{kParamInputFilter, "In filter"}, {kParamDryWet, "Dry/Wet"},    {kParamReverbFilter, "Rvb filter"}}},
    {"MOD",   {{kParamModDepth, "Depth"},       {kParamModRate, "Rate"},       {kParamModShape, "Shape"}}},
};

// nt_emu (Windows build, NT_EMU_WIN): NT_setParameterFrom... always lands in the most
// recently created nt_emu instance, so write our own v[] directly there (as in Duett).
static void setParam(_NT_algorithm* self, int p, int value) {
    if (value < parameters[p].min) value = parameters[p].min;
    if (value > parameters[p].max) value = parameters[p].max;
#ifdef NT_EMU_WIN
    if (self->v) {
        const_cast<int16_t*>(self->v)[p] = (int16_t)value;
        parameterChanged(self, p);
        return;
    }
#endif
    NT_setParameterFromUi(NT_algorithmIndex(self), p + NT_parameterOffset(), (int16_t)value);
}

static inline float paramNorm(const _NT_algorithm* self, int p) { return paramNormV(p, self->v[p]); }

// Value as shown on the Sary-Shagan screen. fine = one decimal for percentages.
static int formatValue(_NT_algorithm* self, int p, char* buff, bool fine = false) {
    const int v = self->v[p];
    const _NT_parameter& d = parameters[p];
    int len;
    if (d.unit == kNT_unitHasStrings || d.unit == kNT_unitEnum) {
        if (d.unit == kNT_unitEnum) {
            const char* s = d.enumStrings[v];
            len = (int)strlen(s);
            memcpy(buff, s, (size_t)len + 1);
            return len;
        }
        if (fine && p == kParamModShape) {
            len = NT_floatToString(buff, v * 0.1f, 1);
            buff[len++] = '%'; buff[len] = 0;
            return len;
        }
        return parameterString(self, p, v, buff);
    }
    if (d.unit == kNT_unitMs) {
        len = NT_intToString(buff, v);
        memcpy(buff + len, " ms", 4);
        return len + 3;
    }
    if (d.unit == kNT_unitDb_minInf) {
        if (v <= d.min) { memcpy(buff, "-inf dB", 8); return 7; }
        len = 0;
        if (v > 0) buff[len++] = '+';
        len += NT_floatToString(buff + len, v * 0.1f, 1);
        memcpy(buff + len, " dB", 4);
        return len + 3;
    }
    if (fine) {
        len = NT_floatToString(buff, v * 0.1f, 1);
    } else {
        len = NT_intToString(buff, (v + 5) / 10);
    }
    buff[len++] = '%';
    buff[len] = 0;
    return len;
}

// log2 approximation for display values only (keeps the plug-in to expf from libm)
static inline float approxLog2(float x) {
    union { float f; uint32_t i; } u = {x};
    const float e = (float)(int)((u.i >> 23) & 0xff) - 128.f;
    u.i = (u.i & 0x007fffff) | 0x3f800000;   // mantissa in [1,2)
    const float m = u.f;
    return e + (-0.34484843f * m + 2.02466578f) * m - 0.67487759f;
}

// Estimated reverb time (RT60). One trip round the tank is ~0.725 s * time scale
// and passes the decay gain four times.
static float estimateRt60(SaryShaganAlgorithm* a) {
    const float ts = timeScaleFromSize(a->size.target, a->tuned, a->tuned ? a->cvSizeVolts : 0.f);
    const float d = a->decay.target;
    if (d >= 0.99999f) return 1e9f;
    return 3.f * 0.72541f * ts / (-4.f * 0.30103f * approxLog2(d));
}

// Short names for the FINE line, in caps like the panel labels.
static const char* kFineNames[kNumParams];
static void initFineNames() {
    static bool done = false;
    if (done) return;
    done = true;
    for (int p = 0; p < kNumParams; ++p) kFineNames[p] = "";
    kFineNames[kParamInputLevel] = "LEVEL";     kFineNames[kParamPreDelay] = "PRE-DLY";
    kFineNames[kParamDiffusion] = "DIFFUSION";  kFineNames[kParamSize] = "SIZE";
    kFineNames[kParamDecay] = "DECAY";          kFineNames[kParamInputFilter] = "IN FILT";
    kFineNames[kParamDryWet] = "DRY/WET";       kFineNames[kParamReverbFilter] = "RVB FILT";
    kFineNames[kParamModDepth] = "DEPTH";       kFineNames[kParamModRate] = "RATE";
    kFineNames[kParamModShape] = "SHAPE";
}

static uint32_t hasCustomUi(_NT_algorithm* self) {
    if (!self->v) return 0;   // nt_emu calls this before it has connected the parameters
    uint32_t c = kNT_potL | kNT_potC | kNT_potR | kNT_encoderL | kNT_encoderR | kNT_encoderButtonL
               | kNT_encoderButtonR | kNT_button3 | kNT_button4;
#ifdef NT_EMU_WIN
    c |= kNT_button1 | kNT_button2;   // nt_emu: encoder clicks arrive unreliably -> Button 1 page, Button 2 Tuned
#endif
    return c;
}

static void changePage(SaryShaganAlgorithm* a, int delta) {
    a->page = ((a->page + delta) % kNumUiPages + kNumUiPages) % kNumUiPages;
    for (int i = 0; i < 3; ++i) a->caught[i] = false;
    const int centre = kUiPages[a->page].pots[1].param;
    if (centre >= 0) a->fineParam = centre;
}

static void customUi(_NT_algorithm* self, const _NT_uiData& data) {
    if (!self->v) return;
    SaryShaganAlgorithm* a = (SaryShaganAlgorithm*)self;
    const int16_t* v = self->v;

    const uint32_t press = data.controls & ~data.lastButtons;
    const uint32_t release = data.lastButtons & ~data.controls;

    // Button 3: Freeze. Latch mode toggles; momentary freezes while held.
    // (nt_emu delivers button presses only as one event on release: always toggle there.)
#ifdef NT_EMU_WIN
    const bool latchBtn = true;
#else
    const bool latchBtn = v[kParamFreezeMode] != 0;
#endif
    if (latchBtn) {
        if (press & kNT_button3) setParam(self, kParamFreeze, v[kParamFreeze] ? 0 : 1);
    } else {
        if (press & kNT_button3) a->freezeHeld = true;
        if (release & kNT_button3) a->freezeHeld = false;
    }
    if (press & kNT_button4) {
        if (a->clearPhase == SaryShaganAlgorithm::kClearIdle) a->clearPhase = SaryShaganAlgorithm::kClearFadeOut;
    }
#ifdef NT_EMU_WIN
    if (press & kNT_button1) changePage(a, 1);
    if (press & kNT_button2) setParam(self, kParamTuned, v[kParamTuned] ? 0 : 1);
#endif
    if (press & kNT_encoderButtonL) setParam(self, kParamDiffuseInput, v[kParamDiffuseInput] ? 0 : 1);
    if (press & kNT_encoderButtonR) setParam(self, kParamTuned, v[kParamTuned] ? 0 : 1);

    if (data.encoders[0]) changePage(a, data.encoders[0]);
    if (data.encoders[1]) {
        const int p = a->fineParam;
        const int step = parameters[p].unit == kNT_unitMs ? 1 : (p == kParamInputLevel ? 1 : 2);
        setParam(self, p, v[p] + data.encoders[1] * step);
    }

    static const uint32_t potBits[3] = {kNT_potL, kNT_potC, kNT_potR};
    const UiPage& cur = kUiPages[a->page];
    for (int i = 0; i < 3; ++i) {
        if (!(data.controls & potBits[i])) continue;
        const float pos = data.pots[i];
        const int p = cur.pots[i].param;
        if (p < 0) { a->potPos[i] = pos; continue; }
        if (!a->caught[i]) {
            const float target = paramNorm(self, p);
            const float prev = a->potPos[i] < 0.f ? pos : a->potPos[i];
            const float lo = prev < pos ? prev : pos, hi = prev < pos ? pos : prev;
            a->caught[i] = target >= lo - 0.02f && target <= hi + 0.02f;
        }
        a->potPos[i] = pos;
        if (a->caught[i]) {
            const _NT_parameter& d = parameters[p];
            setParam(self, p, d.min + (int)(pos * (float)(d.max - d.min) + 0.5f));
            a->fineParam = p;
        }
    }
}

static void setupUi(_NT_algorithm* self, _NT_float3& pots) {
    if (!self->v) return;
    SaryShaganAlgorithm* a = (SaryShaganAlgorithm*)self;
    for (int i = 0; i < 3; ++i) {
        // Report the values for the NT's soft takeover, but still pick up ourselves:
        // hosts without takeover (nt_emu, the VCV module) leave the pots where they are.
        const int p = kUiPages[a->page].pots[i].param;
        pots[i] = p >= 0 ? paramNorm(self, p) : 0.5f;
        a->potPos[i] = -1.f;
        a->caught[i] = false;
    }
}

static void drawChip(int x, int y, int w, const char* s, bool on) {
    if (on) {
        NT_drawShapeI(kNT_rectangle, x, y - 8, x + w, y + 1, 15);
        NT_drawText(x + w / 2 + 1, y, s, 0, kNT_textCentre);
    } else {
        NT_drawText(x + w / 2 + 1, y, s, 6, kNT_textCentre);
    }
}

// MOD page, Shape column: the current LFO shape as a drawing (two periods, a dot
// running at the first LFO's phase), below it the three basic shapes with a
// marker that travels from ramp down over triangle to ramp up.
static inline float triSaw(float ph, float r) { return ph < r ? ph / r : 1.f - (ph - r) / (1.f - r); }

static void drawShapeColumn(SaryShaganAlgorithm* a, int cx, float knob) {
    const float e = a->eff[kParamModShape];               // knob + CV: what is heard
    const float r = 0.001f + e * 0.998f;
    const int x0 = cx - 30, x1 = cx + 30, yTop = 27, yBot = 36, period = 30;
    int px = x0, py = yBot;
    for (int x = x0; x <= x1; ++x) {
        const float ph = (float)((x - x0) % period) / (float)period;
        const int y = yBot - (int)(triSaw(ph, r) * (float)(yBot - yTop) + 0.5f);
        if (x > x0) NT_drawShapeI(kNT_line, px, py, x, y, 15);
        px = x; py = y;
    }
    // running dot at the LFO phase (first period)
    const float ph = a->reverb.lfoPhase();
    const int dx = x0 + (int)(ph * (float)period);
    const int dy = yBot - (int)(triSaw(ph, r) * (float)(yBot - yTop) + 0.5f);
    NT_drawShapeI(kNT_rectangle, dx - 1, dy - 1, dx + 1, dy + 1, 15);

    // three basic shapes, the nearer the brighter
    static const float at[3] = {0.f, 0.5f, 1.f};
    const int iy0 = 40, iy1 = 45;
    for (int k = 0; k < 3; ++k) {
        const int ix = x0 + k * 30;
        const float d = fabsf(e - at[k]);
        const int c = d < 0.5f ? 15 - (int)(d * 22.f) : 4;
        if (k == 0) {        // ramp down
            NT_drawShapeI(kNT_line, ix - 5, iy1, ix - 5, iy0, c);
            NT_drawShapeI(kNT_line, ix - 5, iy0, ix + 5, iy1, c);
        } else if (k == 1) { // triangle
            NT_drawShapeI(kNT_line, ix - 5, iy1, ix, iy0, c);
            NT_drawShapeI(kNT_line, ix, iy0, ix + 5, iy1, c);
        } else {             // ramp up
            NT_drawShapeI(kNT_line, ix - 5, iy1, ix + 5, iy0, c);
            NT_drawShapeI(kNT_line, ix + 5, iy0, ix + 5, iy1, c);
        }
    }
    // knob position: short line under the shapes; with CV a tick where knob + CV sit
    const int kx = x0 + (int)(knob * 60.f + 0.5f);
    NT_drawShapeI(kNT_line, kx - 4, 48, kx + 4, 48, 15);
    if (fabsf(e - knob) > 0.005f) {
        const int ex = x0 + (int)(e * 60.f + 0.5f);
        NT_drawShapeI(kNT_line, ex, 47, ex, 50, 9);
    }
}

static bool draw(_NT_algorithm* self) {
    if (!self->v) return false;
    SaryShaganAlgorithm* a = (SaryShaganAlgorithm*)self;
    const UiPage& pg = kUiPages[a->page];
    char buff[kNT_parameterStringSize];
    initFineNames();

    // header: page tabs, state chips
    static const int tabW[kNumUiPages] = {35, 35, 29, 23};
    int x = 0;
    for (int i = 0; i < kNumUiPages; ++i) {
        drawChip(x, 8, tabW[i], kUiPages[i].name, i == a->page);
        x += tabW[i] + 3;
    }
    if (a->clip > 0.f) drawChip(140, 8, 23, "CLIP", true);
    if (a->tuned) drawChip(167, 8, 35, "TUNED", true);
    if (a->clearPhase != SaryShaganAlgorithm::kClearIdle) drawChip(206, 8, 23, "CLR", true);
    else if (a->frozen) drawChip(206, 8, 41, "FREEZE", true);
    NT_drawShapeI(kNT_line, 0, 11, 255, 11, 3);

    // three pot columns
    static const int cx[3] = {42, 128, 213};
    for (int i = 0; i < 3; ++i) {
        const int p = pg.pots[i].param;
        NT_drawText(cx[i], 23, pg.pots[i].label, 8, kNT_textCentre);
        if (p < 0) {   // INPUT page, right column: Diffuse input (Encoder L push)
            NT_drawText(cx[i], 35, self->v[kParamDiffuseInput] ? "On" : "Off", 15, kNT_textCentre);
            NT_drawText(cx[i], 46, "Enc L push", 6, kNT_textCentre, kNT_textTiny);
            continue;
        }
        if (p == kParamModShape) {
            drawShapeColumn(a, cx[i], paramNorm(self, p));
            if (!a->caught[i] && a->potPos[i] >= 0.f) {   // pot not picked up yet: where it stands
                const int px = cx[i] - 30 + (int)(a->potPos[i] * 60.f + 0.5f);
                NT_drawShapeI(kNT_line, px - 2, 51, px + 2, 51, 6);
            }
            continue;
        }
        formatValue(self, p, buff);
        NT_drawText(cx[i], 35, buff, 15, kNT_textCentre);
        const int x0 = cx[i] - 36, x1 = cx[i] + 36;
        NT_drawShapeI(kNT_box, x0, 39, x1, 43, 4);
        const float kn = paramNorm(self, p);
        const bool bipolar = parameters[p].min < 0 && p != kParamInputLevel;
        if (bipolar) {
            const int mid = (x0 + x1) / 2;
            const int f = x0 + (int)(kn * (float)(x1 - x0) + 0.5f);
            if (f != mid) NT_drawShapeI(kNT_rectangle, f < mid ? f : mid, 40, f < mid ? mid : f, 42, 12);
            NT_drawShapeI(kNT_line, mid, 38, mid, 44, 6);
        } else {
            const int fill = x0 + (int)(kn * (float)(x1 - x0) + 0.5f);
            if (fill > x0) NT_drawShapeI(kNT_rectangle, x0, 40, fill, 42, 12);
        }
        // CV: where knob + CV actually sit
        const float e = a->eff[p];
        if (fabsf(e - kn) > 0.005f) {
            const int ex = x0 + (int)(e * (float)(x1 - x0) + 0.5f);
            NT_drawShapeI(kNT_line, ex, 44, ex, 46, 15);
            NT_drawShapeI(kNT_line, ex - 1, 46, ex + 1, 46, 15);
        }
        // pot not picked up yet: where it stands
        if (!a->caught[i] && a->potPos[i] >= 0.f) {
            const int px = x0 + (int)(a->potPos[i] * (float)(x1 - x0) + 0.5f);
            NT_drawShapeI(kNT_line, px, 37, px, 43, 7);
        }
    }

    // extra info on SPACE: tank pitch (tuned) under Size, reverb time under Decay
    if (a->page == 1) {
        if (a->tuned) {
            const float ts = timeScaleFromSize(a->size.target, true, a->cvSizeVolts);
            const float hz = 1.f / (0.72541f * ts);
            static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            const float mf = 69.f + 12.f * approxLog2(hz / 440.f);
            const int midi = mf > 0.f ? (int)(mf + 0.5f) : -1;
            int n = NT_intToString(buff, (int)(hz + 0.5f));
            memcpy(buff + n, " Hz ", 4); n += 4;
            if (midi >= 0 && midi < 128) {
                const char* nm = names[midi % 12];
                while (*nm) buff[n++] = *nm++;
                n += NT_intToString(buff + n, midi / 12 - 1);
            }
            buff[n] = 0;
            NT_drawText(cx[1], 53, buff, 8, kNT_textCentre, kNT_textTiny);
        }
        const float rt = estimateRt60(a);
        if (a->frozen || rt > 99.f) {
            NT_drawText(cx[2], 53, "RT inf", 8, kNT_textCentre, kNT_textTiny);
        } else {
            int n = 3;
            memcpy(buff, "RT ", 3);
            n += NT_floatToString(buff + n, rt, rt < 1.f ? 2 : (rt < 10.f ? 1 : 0));
            buff[n++] = ' '; buff[n++] = 's'; buff[n] = 0;
            NT_drawText(cx[2], 53, buff, 8, kNT_textCentre, kNT_textTiny);
        }
    }

    // footer, placed like the controls: wet level meter under the centre pot (Dry/Wet
    // is a centre pot), Encoder R's FINE target right-aligned above Encoder R.
    const float db = a->meter > 1e-4f ? 6.0206f * approxLog2(a->meter * 0.1f) : -100.f;   // 0 dB = 10 V
    float m = (db + 48.f) / 48.f;
    m = m < 0.f ? 0.f : (m > 1.f ? 1.f : m);
    // "WET" + meter span exactly the width of the centre column's bar
    const int mx0 = cx[1] - 21, mx1 = cx[1] + 36;
    NT_drawText(cx[1] - 38, 62, "WET", 8, kNT_textLeft, kNT_textTiny);
    NT_drawShapeI(kNT_rectangle, mx0, 57, mx1, 61, 2);          // shaded track
    const int mf = mx0 + (int)(m * (float)(mx1 - mx0));
    if (mf > mx0) NT_drawShapeI(kNT_rectangle, mx0, 57, mf, 61, a->meter > 9.f ? 15 : 9);

    int n = 0;
    memcpy(buff, "FINE ", 5); n = 5;
    for (const char* s = kFineNames[a->fineParam]; *s; ++s) buff[n++] = *s;
    buff[n++] = ' ';
    formatValue(self, a->fineParam, buff + n, true);
    NT_drawText(253, 62, buff, 10, kNT_textRight, kNT_textTiny);   // tiny font draws 2 px to the right
    return true;
}

static const _NT_factory factory = {
    .guid = NT_MULTICHAR('X', 'o', 'S', 'S'),
    .name = "Sary-Shagan",
    .description = "Dattorro plate reverb, port of Valley Plateau",
    .numSpecifications = 0,
    .specifications = NULL,
    .calculateStaticRequirements = NULL,
    .initialise = NULL,
    .calculateRequirements = calculateRequirements,
    .construct = construct,
    .parameterChanged = parameterChanged,
    .step = step,
    .draw = draw,
    .midiRealtime = NULL,
    .midiMessage = NULL,
    .tags = kNT_tagEffect | kNT_tagReverb,
    .hasCustomUi = hasCustomUi,
    .customUi = customUi,
    .setupUi = setupUi,
    .serialise = NULL,
    .deserialise = NULL,
    .midiSysEx = NULL,
    .parameterUiPrefix = NULL,
    .parameterString = parameterString,
};

_NT_DRAM_SECTION
uintptr_t pluginEntry(_NT_selector selector, uint32_t data) {
    switch (selector) {
    case kNT_selector_version:
        return kNT_apiVersionCurrent;
    case kNT_selector_numFactories:
        return 1;
    case kNT_selector_factoryInfo:
        return (uintptr_t)((data == 0) ? &factory : NULL);
    }
    return 0;
}
