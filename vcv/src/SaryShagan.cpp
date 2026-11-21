// Sary-Shagan as a native VCV Rack module: the disting NT plug-in (sary-shagan/sary_shagan.cpp,
// unchanged) in the small NT host from Perseids (nt_host). The panel follows the
// NT: 3 pots, 2 encoders, buttons, the 256 × 64 display 1:1. Every control is a
// Rack parameter, so MIDI-Map can learn each one; the encoders also have ◀ / ▶
// buttons for note mapping.
//
// Sary-Shagan: Dattorro plate reverb, port of Plateau by Dale Johnson (Valley Audio Soft).
// Controls and jacks follow Mesa (4ms + Valley Audio). GPL-3.0-or-later.
#include <cmath>
#include <cstring>
#include <mutex>

#include "sary_shagan_plugin.hpp"
#include "nt_host.hpp"
#include "panel_layout.hpp"

extern "C" uintptr_t pluginEntry(_NT_selector selector, uint32_t data);

namespace
{
const _NT_factory* Factory()
{
    return reinterpret_cast<const _NT_factory*>(pluginEntry(kNT_selector_factoryInfo, 0));
}

constexpr float kStepsPerTurn = 24.f; // encoder detents per full turn (and per MIDI CC sweep)
constexpr int   kUiDivider    = 32;   // controls → host every 32 frames
constexpr float kDrawHz       = 30.f;
constexpr float kRepeatDelayS = 0.4f, kRepeatRateS = 0.08f; // ◀ / ▶ held

// The panel's jacks: CV 1…10 are NT inputs 3…12, the Freeze and Clear gates go
// to the aux busses 21/22 (the NT has only 12 inputs). On the NT these routing
// parameters start at "None"; in the module they start on their jacks.
const std::vector<std::pair<std::string, int>> kPanelRouting = {
    {"In filter CV", 3}, {"Diffusion CV", 4}, {"Size CV", 5}, {"Dry/Wet CV", 6}, {"Rvb filter CV", 7},
    {"Pre-delay CV", 8}, {"Mod depth CV", 9}, {"Mod rate CV", 10}, {"Mod shape CV", 11}, {"Decay CV", 12},
    {"Freeze gate", 21}, {"Clear gate", 22}};
const char* const kCvNames[10] = {"In filter", "Diffusion", "Size", "Dry/Wet", "Rvb filter",
                                  "Pre-delay", "Mod depth", "Mod rate", "Mod shape", "Decay"};
} // namespace

struct SaryShagan : Module
{
    enum ParamId
    {
        POT_L_PARAM,
        POT_C_PARAM,
        POT_R_PARAM,
        ENC_L_PARAM,
        ENC_R_PARAM,
        ENC_L_PUSH_PARAM,
        ENC_R_PUSH_PARAM,
        ENC_L_DOWN_PARAM,
        ENC_L_UP_PARAM,
        ENC_R_DOWN_PARAM,
        ENC_R_UP_PARAM,
        FREEZE_PARAM,
        CLEAR_PARAM,
        MENU_PARAM,
        PARAMS_LEN
    };
    enum InputId
    {
        IN_L_INPUT,
        IN_R_INPUT,
        CV_INPUT,   // … 10 CVs in Mesa order
        FREEZE_INPUT = CV_INPUT + 10,
        CLEAR_INPUT,
        INPUTS_LEN
    };
    enum OutputId
    {
        OUT_L_OUTPUT,
        OUT_R_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId
    {
        FREEZE_LIGHT,
        CLEAR_LIGHT,
        LIGHTS_LEN
    };

    struct EncoderQuantity : ParamQuantity
    {
        std::string getDisplayValueString() override { return "endless"; }
    };

    nt_host::Host host;
    std::mutex    host_mutex; // held while the host is rebuilt (UI thread)
    float         sample_rate = 0.f;
    int           p_freeze = -1, p_freeze_mode = -1;

    float enc_last[2]      = {-1.f, -1.f};
    float enc_acc[2]       = {0.f, 0.f};
    int   step_held[2][2]  = {};
    float step_timer[2][2] = {};
    int   ui_div           = 0;
    float draw_phase       = 0.f;
    float clear_light      = 0.f;
    bool  clear_last       = false;

    SaryShagan()
    {
        static_assert(INPUTS_LEN == panel_layout::kNumInputs, "panel_def.py and the module disagree");
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);
        configParam(POT_L_PARAM, 0.f, 1.f, 1.f, "Pot L (Level · Diffusion · In filter · Mod depth)", "%", 0.f, 100.f);
        configParam(POT_C_PARAM, 0.f, 1.f, 0.5f, "Pot C (Pre-delay · Size · Dry/Wet · Mod rate)", "%", 0.f, 100.f);
        configParam(POT_R_PARAM, 0.f, 1.f, 0.5f, "Pot R (– · Decay · Reverb filter · Mod shape)", "%", 0.f, 100.f);
        configParam<EncoderQuantity>(ENC_L_PARAM, 0.f, 1.f, 0.f, "Encoder L (page; menu: parameter)");
        configParam<EncoderQuantity>(ENC_R_PARAM, 0.f, 1.f, 0.f, "Encoder R (fine-tune the last pot; menu: value)");
        configButton(ENC_L_PUSH_PARAM, "Encoder L push (Diffuse input; menu: next page)");
        configButton(ENC_R_PUSH_PARAM, "Encoder R push (Tuned; menu: confirm, hold = ×10)");
        configButton(ENC_L_DOWN_PARAM, "Encoder L ◀");
        configButton(ENC_L_UP_PARAM, "Encoder L ▶");
        configButton(ENC_R_DOWN_PARAM, "Encoder R ◀");
        configButton(ENC_R_UP_PARAM, "Encoder R ▶");
        configButton(FREEZE_PARAM, "Freeze (NT button 3)");
        configButton(CLEAR_PARAM, "Clear (NT button 4)");
        configButton(MENU_PARAM, "Menu (parameter pages)");
        for(int k = 0; k < 2; ++k)
            getParamQuantity(ENC_L_PARAM + k)->snapEnabled = false;
        configInput(IN_L_INPUT, "In L (NT input 1)");
        configInput(IN_R_INPUT, "In R (NT input 2; unpatched = In L)");
        for(int k = 0; k < 10; ++k)
            configInput(CV_INPUT + k, string::f("%s CV ±5 V (NT input %d)", kCvNames[k], 3 + k));
        configInput(FREEZE_INPUT, "Freeze gate (aux 1)");
        configInput(CLEAR_INPUT, "Clear trigger (aux 2)");
        configOutput(OUT_L_OUTPUT, "Out L (NT output 1)");
        configOutput(OUT_R_OUTPUT, "Out R (NT output 2)");
        configLight(FREEZE_LIGHT, "Freeze");
        configLight(CLEAR_LIGHT, "Clear");
        configBypass(IN_L_INPUT, OUT_L_OUTPUT);
        configBypass(IN_R_INPUT, OUT_R_OUTPUT);
        Rebuild(kPanelRouting, APP->engine->getSampleRate());
    }

    int Find(const char* name) const
    {
        for(int p = 0; p < host.NumParams(); ++p)
            if(!std::strcmp(host.ParamName(p), name))
                return p;
        return -1;
    }

    // New algorithm instance (sample rate change, patch load).
    void Rebuild(std::vector<std::pair<std::string, int>> values, float sr, bool keep = false)
    {
        std::lock_guard<std::mutex> l(host_mutex);
        if(keep)
            values = host.Values();
        sample_rate = sr;
        host.Create(Factory(), {}, static_cast<uint32_t>(sr + 0.5f), values);
        // The menu names the busses after the panel jacks.
        std::vector<std::string> names(kNT_numInputBusses + kNT_numOutputBusses + 2);
        names[0] = "In L";
        names[1] = "In R";
        for(int k = 0; k < 10; ++k)
            names[2 + k] = std::string(kCvNames[k]) + " jack";
        names[kNT_numInputBusses]                          = "Out L";
        names[kNT_numInputBusses + 1]                      = "Out R";
        names[kNT_numInputBusses + kNT_numOutputBusses]     = "Freeze jack";
        names[kNT_numInputBusses + kNT_numOutputBusses + 1] = "Clear jack";
        host.SetBusNames(names);
        p_freeze      = Find("Freeze");
        p_freeze_mode = Find("Freeze mode");
    }

    void onSampleRateChange(const SampleRateChangeEvent& e) override
    {
        if(std::fabs(e.sampleRate - sample_rate) > 0.5f)
            Rebuild({}, e.sampleRate, true);
    }

    void onReset(const ResetEvent& e) override
    {
        Module::onReset(e);
        std::lock_guard<std::mutex> l(host_mutex);
        host.ResetParams();
        for(const auto& nv : kPanelRouting)
            if(int p = Find(nv.first.c_str()); p >= 0)
                host.SetParam(p, nv.second);
    }

    int Steps(int k, float dt)
    {
        const float v = params[ENC_L_PARAM + k].getValue();
        if(enc_last[k] < 0.f)
            enc_last[k] = v;
        float d = v - enc_last[k];
        if(d > 0.5f)
            d -= 1.f;
        else if(d < -0.5f)
            d += 1.f;
        enc_last[k] = v;
        enc_acc[k] += d * kStepsPerTurn;
        int n = static_cast<int>(enc_acc[k]);
        enc_acc[k] -= n;
        for(int dir = 0; dir < 2; ++dir)
        {
            const bool down = params[ENC_L_DOWN_PARAM + 2 * k + dir].getValue() > 0.5f;
            const int  s    = dir ? 1 : -1;
            if(down && !step_held[k][dir])
            {
                n += s;
                step_timer[k][dir] = kRepeatDelayS;
            }
            else if(down && (step_timer[k][dir] -= dt) <= 0.f)
            {
                n += s;
                step_timer[k][dir] = kRepeatRateS;
            }
            step_held[k][dir] = down;
        }
        return n;
    }

    void process(const ProcessArgs& args) override
    {
        std::unique_lock<std::mutex> l(host_mutex, std::try_to_lock);
        if(!l.owns_lock() || !host.Ok())
        {
            outputs[OUT_L_OUTPUT].setVoltage(0.f);
            outputs[OUT_R_OUTPUT].setVoltage(0.f);
            return;
        }
        float in[kNT_numInputBusses] = {};
        in[0] = inputs[IN_L_INPUT].getVoltageSum();
        // In R unpatched: In L on both sides (Mesa normalises mono to stereo).
        in[1] = inputs[IN_R_INPUT].isConnected() ? inputs[IN_R_INPUT].getVoltageSum() : in[0];
        for(int k = 0; k < 10; ++k)
            in[2 + k] = inputs[CV_INPUT + k].getVoltage();
        const float aux[nt_host::Host::kAuxIn] = {inputs[FREEZE_INPUT].getVoltage(), inputs[CLEAR_INPUT].getVoltage(),
                                                  0.f, 0.f};
        float out[2];
        host.Frame(in, aux, out);
        outputs[OUT_L_OUTPUT].setVoltage(out[0]);
        outputs[OUT_R_OUTPUT].setVoltage(out[1]);

        if(++ui_div >= kUiDivider)
        {
            ui_div = 0;
            const float       dt = kUiDivider * args.sampleTime;
            nt_host::Controls c;
            for(int k = 0; k < 3; ++k)
                c.pots[k] = params[POT_L_PARAM + k].getValue();
            c.encoders[0] = Steps(0, dt);
            c.encoders[1] = Steps(1, dt);
            if(params[ENC_L_PUSH_PARAM].getValue() > 0.5f)
                c.buttons |= kNT_encoderButtonL;
            if(params[ENC_R_PUSH_PARAM].getValue() > 0.5f)
                c.buttons |= kNT_encoderButtonR;
            const bool freeze_btn = params[FREEZE_PARAM].getValue() > 0.5f;
            if(freeze_btn)
                c.buttons |= kNT_button3;
            const bool clear = params[CLEAR_PARAM].getValue() > 0.5f;
            if(clear)
                c.buttons |= kNT_button4;
            c.menu = params[MENU_PARAM].getValue() > 0.5f;
            host.Ui(c);

            // Lights: Freeze shows the frozen state (button, parameter or gate), Clear flashes.
            const bool latch  = p_freeze_mode >= 0 && host.ParamValue(p_freeze_mode);
            const bool gate   = inputs[FREEZE_INPUT].getVoltage() > 2.f;
            const bool frozen = (p_freeze >= 0 && host.ParamValue(p_freeze)) || (!latch && (freeze_btn || gate));
            lights[FREEZE_LIGHT].setBrightness(frozen ? 1.f : 0.f);
            const bool clear_in = inputs[CLEAR_INPUT].getVoltage() > 2.f;
            if((clear || clear_in) && !clear_last)
                clear_light = 1.f;
            clear_last  = clear || clear_in;
            clear_light = std::fmax(0.f, clear_light - dt * 4.f);
            lights[CLEAR_LIGHT].setBrightness(clear_light);
        }
        draw_phase += args.sampleTime * kDrawHz;
        if(draw_phase >= 1.f)
        {
            draw_phase -= 1.f;
            host.Draw();
        }
    }

    json_t* dataToJson() override
    {
        json_t* root = json_object();
        json_t* vals = json_object();
        for(const auto& nv : host.Values())
            json_object_set_new(vals, nv.first.c_str(), json_integer(nv.second));
        json_object_set_new(root, "values", vals);
        return root;
    }

    void dataFromJson(json_t* root) override
    {
        std::vector<std::pair<std::string, int>> values;
        if(json_t* vals = json_object_get(root, "values"))
        {
            const char* key;
            json_t*     v;
            json_object_foreach(vals, key, v) values.emplace_back(key, static_cast<int>(json_integer_value(v)));
        }
        Rebuild(values, sample_rate > 0.f ? sample_rate : APP->engine->getSampleRate());
    }
};

// The NT display, pixel for pixel: 256 × 64, 16 grey levels, lit in the dark.
struct NtDisplay : Widget
{
    SaryShagan* module = nullptr;
    uint8_t screen[nt_host::kScreenBytes] = {};

    void draw(const DrawArgs& args) override
    {
        nvgBeginPath(args.vg);
        nvgRect(args.vg, 0, 0, box.size.x, box.size.y);
        nvgFillColor(args.vg, nvgRGB(0, 0, 0));
        nvgFill(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if(layer == 1 && module)
        {
            module->host.CopyScreen(screen);
            const float sx = box.size.x / 256.f, sy = box.size.y / 64.f;
            for(int level = 1; level < 16; ++level)
            {
                bool any = false;
                nvgBeginPath(args.vg);
                for(int y = 0; y < 64; ++y)
                {
                    int run = -1;
                    for(int x = 0; x <= 256; ++x)
                    {
                        int v = 0;
                        if(x < 256)
                        {
                            const uint8_t b = screen[y * 128 + x / 2];
                            v               = (x & 1) ? (b & 15) : (b >> 4);
                        }
                        if(v == level && run < 0)
                            run = x;
                        else if(v != level && run >= 0)
                        {
                            nvgRect(args.vg, run * sx, y * sy, (x - run) * sx, sy);
                            run = -1;
                            any = true;
                        }
                    }
                }
                if(any)
                {
                    nvgFillColor(args.vg, nvgRGBA(255, 255, 255, level * 17));
                    nvgFill(args.vg);
                }
            }
        }
        Widget::drawLayer(args, layer);
    }
};

struct EndlessKnob : RoundLargeBlackKnob
{
    EndlessKnob()
    {
        minAngle = 0.f;
        maxAngle = 2.f * float(M_PI);
    }

    void Turn(float d)
    {
        if(ParamQuantity* pq = getParamQuantity())
            pq->setValue(math::eucMod(pq->getValue() + d, 1.f));
    }

    void onDragMove(const DragMoveEvent& e) override
    {
        if(e.button != GLFW_MOUSE_BUTTON_LEFT)
            return;
        const float fine = (APP->window->getMods() & RACK_MOD_MASK) == RACK_MOD_CTRL ? 0.25f : 1.f;
        Turn(-e.mouseDelta.y * 0.004f * fine);
    }

    void onHoverScroll(const HoverScrollEvent& e) override
    {
        Turn((e.scrollDelta.y > 0.f ? 1.f : -1.f) / kStepsPerTurn);
        e.consume(this);
    }
};

struct SaryShaganWidget : ModuleWidget
{
    SaryShaganWidget(SaryShagan* module)
    {
        using namespace panel_layout;
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/SaryShagan.svg")));
        for(int k = 0; k < kNumScrews; ++k)
            addChild(createWidgetCentered<ScrewBlack>(Screw(k)));

        NtDisplay* d = createWidget<NtDisplay>(DisplayPos());
        d->box.size  = DisplaySize();
        d->module    = module;
        addChild(d);

        for(int k = 0; k < 3; ++k)
            addParam(createParamCentered<RoundBigBlackKnob>(Pot(k), module, SaryShagan::POT_L_PARAM + k));
        for(int k = 0; k < 2; ++k)
        {
            addParam(createParamCentered<EndlessKnob>(Enc(k), module, SaryShagan::ENC_L_PARAM + k));
            addParam(createParamCentered<TL1105>(EncStep(k, -1), module, SaryShagan::ENC_L_DOWN_PARAM + 2 * k));
            addParam(createParamCentered<TL1105>(EncStep(k, 1), module, SaryShagan::ENC_L_UP_PARAM + 2 * k));
        }
        addParam(createParamCentered<TL1105>(EncPush(0), module, SaryShagan::ENC_L_PUSH_PARAM));
        addParam(createParamCentered<TL1105>(EncPush(1), module, SaryShagan::ENC_R_PUSH_PARAM));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(
            CenterButton(0), module, SaryShagan::FREEZE_PARAM, SaryShagan::FREEZE_LIGHT));
        addParam(createLightParamCentered<VCVLightButton<MediumSimpleLight<WhiteLight>>>(
            CenterButton(1), module, SaryShagan::CLEAR_PARAM, SaryShagan::CLEAR_LIGHT));
        addParam(createParamCentered<VCVButton>(CenterButton(2), module, SaryShagan::MENU_PARAM));
        for(int k = 0; k < SaryShagan::INPUTS_LEN; ++k)
            addInput(createInputCentered<PJ301MPort>(InputPos(k), module, k));
        for(int k = 0; k < SaryShagan::OUTPUTS_LEN; ++k)
            addOutput(createOutputCentered<PJ301MPort>(OutputPos(k), module, k));
    }

    void appendContextMenu(Menu* menu) override
    {
        SaryShagan* m = getModule<SaryShagan>();
        if(!m)
            return;
        menu->addChild(new MenuSeparator);
        menu->addChild(createMenuLabel("Sary-Shagan: Dattorro plate reverb"));
        menu->addChild(createMenuLabel("Port of Plateau by Dale Johnson (Valley Audio)"));
        auto toggle = [m](const char* name, const char* label) {
            return createCheckMenuItem(
                label, "",
                [=]() {
                    const int p = m->Find(name);
                    return p >= 0 && m->host.ParamValue(p) != 0;
                },
                [=]() {
                    std::lock_guard<std::mutex> l(m->host_mutex);
                    const int p = m->Find(name);
                    if(p >= 0)
                        m->host.SetParam(p, m->host.ParamValue(p) ? 0 : 1);
                });
        };
        menu->addChild(toggle("Tuned", "Tuned mode (Size CV = pitch)"));
        menu->addChild(toggle("Diffuse input", "Diffuse input"));
        menu->addChild(toggle("Freeze mode", "Freeze latches (button and jack)"));
        menu->addChild(toggle("Saturation", "Output soft clip"));
    }
};

Model* modelSaryShagan = createModel<SaryShagan, SaryShaganWidget>("SaryShagan");
