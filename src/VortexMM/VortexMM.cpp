#include "../plugin.hpp"
#include "dsp.h"

struct CutoffParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float hz = getValue();
        if (hz >= 1000.f)
            return string::f("%.2f kHz", hz / 1000.f);
        return string::f("%.1f Hz", hz);
    }
};

struct VortexMM : Module {
    enum ParamId {
        MODE_PARAM,
        CUTOFF_PARAM,
        RESONANCE_PARAM,
        DRIVE_PARAM,

        // CV attenuverters
        CUTOFF_CV_ATTEN_PARAM,
        RESONANCE_CV_ATTEN_PARAM,
        DRIVE_CV_ATTEN_PARAM,

        PARAMS_LEN
    };
    enum InputId {
        AUDIO_INPUT,

        CUTOFF_CV_INPUT,
        RESONANCE_CV_INPUT,
        DRIVE_CV_INPUT,

        INPUTS_LEN
    };
    enum OutputId {
        AUDIO_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    // Filter state
    vortex::Filter1 f1;
    vortex::Filter2 f2a, f2b;
    int lastMode = -1;

    VortexMM() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Main params
        configParam(MODE_PARAM, 0.f, 11.f, 0.f, "Mode");
        getParamQuantity(MODE_PARAM)->snapEnabled = true;

        auto* cpq = configParam<CutoffParamQuantity>(CUTOFF_PARAM, 20.f, 20000.f, 1000.f, "Cutoff");
        (void)cpq;

        configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f, "Resonance", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.f, "Drive", "%", 0.f, 100.f);

        // CV attenuverters
        configParam(CUTOFF_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "Cutoff CV", "%", 0.f, 100.f);
        configParam(RESONANCE_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "Resonance CV", "%", 0.f, 100.f);
        configParam(DRIVE_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "Drive CV", "%", 0.f, 100.f);

        // Inputs
        configInput(AUDIO_INPUT, "Audio");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV");
        configInput(RESONANCE_CV_INPUT, "Resonance CV");
        configInput(DRIVE_CV_INPUT, "Drive CV");

        // Output
        configOutput(AUDIO_OUTPUT, "Audio");
    }

    void process(const ProcessArgs& args) override {
        float fs = args.sampleRate;

        // --- Read input ---
        float input = inputs[AUDIO_INPUT].getVoltage() / 5.f;  // normalize to ~+/-1

        // --- Mode ---
        int mode = (int)params[MODE_PARAM].getValue();

        // Reset filter state when mode changes
        if (mode != lastMode) {
            f1.reset();
            f2a.reset();
            f2b.reset();
            lastMode = mode;
        }

        // --- Cutoff ---
        float cutoff = params[CUTOFF_PARAM].getValue();

        if (inputs[CUTOFF_CV_INPUT].isConnected()) {
            float cutoffCv = inputs[CUTOFF_CV_INPUT].getVoltage()
                           * params[CUTOFF_CV_ATTEN_PARAM].getValue();
            cutoff *= vortex::voct_to_mult(cutoffCv);
        }

        cutoff = clamp(cutoff, 20.f, 20000.f);

        // --- Resonance ---
        // Map knob 0-1 to damping 0.707-0.01
        float resoParam = params[RESONANCE_PARAM].getValue();
        float damping = 0.707f * (1.f - resoParam) + 0.01f * resoParam;

        if (inputs[RESONANCE_CV_INPUT].isConnected()) {
            float resoCv = inputs[RESONANCE_CV_INPUT].getVoltage()
                         * params[RESONANCE_CV_ATTEN_PARAM].getValue() * 0.2f;
            damping -= resoCv;
            damping = clamp(damping, 0.01f, 0.707f);
        }

        // --- Drive ---
        float drv = params[DRIVE_PARAM].getValue();
        if (inputs[DRIVE_CV_INPUT].isConnected()) {
            float driveCv = inputs[DRIVE_CV_INPUT].getVoltage()
                          * params[DRIVE_CV_ATTEN_PARAM].getValue() / 10.f;
            drv = clamp(drv + driveCv, 0.f, 1.f);
        }

        // --- Drive stage ---
        float signal = input;
        if (drv > 0.f) {
            float driveGain = 1.f + drv * 9.f;
            signal = vortex::soft_clip(signal * driveGain);
        }

        // --- Filter ---
        float wet = 0.f;

        switch (mode) {
        case 0: // LP 6dB
            vortex::filter1_configure_lp(f1, fs, cutoff);
            wet = f1.process_lp(signal);
            break;
        case 1: // LP 12dB
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_LP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_LP);
            break;
        case 2: // LP 24dB
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_LP);
            vortex::filter2_configure(f2b, fs, cutoff, damping, vortex::F2_LP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_LP);
            wet = vortex::filter2_process(f2b, wet, vortex::F2_LP);
            break;
        case 3: // HP 6dB
            vortex::filter1_configure_hp(f1, fs, cutoff);
            wet = f1.process_hp(signal);
            break;
        case 4: // HP 12dB
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_HP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_HP);
            break;
        case 5: // HP 24dB
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_HP);
            vortex::filter2_configure(f2b, fs, cutoff, damping, vortex::F2_HP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_HP);
            wet = vortex::filter2_process(f2b, wet, vortex::F2_HP);
            break;
        case 6: // BP
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_BP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_BP);
            break;
        case 7: // BP+
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_BP);
            vortex::filter2_configure(f2b, fs, cutoff, damping, vortex::F2_BP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_BP);
            wet = vortex::filter2_process(f2b, wet, vortex::F2_BP);
            break;
        case 8: // Notch
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_NOTCH);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_NOTCH);
            break;
        case 9: // Notch+
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_NOTCH);
            vortex::filter2_configure(f2b, fs, cutoff, damping, vortex::F2_NOTCH);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_NOTCH);
            wet = vortex::filter2_process(f2b, wet, vortex::F2_NOTCH);
            break;
        case 10: // AP
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_AP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_AP);
            break;
        case 11: // AP+
            vortex::filter2_configure(f2a, fs, cutoff, damping, vortex::F2_AP);
            vortex::filter2_configure(f2b, fs, cutoff, damping, vortex::F2_AP);
            wet = vortex::filter2_process(f2a, signal, vortex::F2_AP);
            wet = vortex::filter2_process(f2b, wet, vortex::F2_AP);
            break;
        }

        // Flush denormals
        f1.z = vortex::flush_denormal(f1.z);
        f2a.z0 = vortex::flush_denormal(f2a.z0);
        f2a.z1 = vortex::flush_denormal(f2a.z1);
        f2b.z0 = vortex::flush_denormal(f2b.z0);
        f2b.z1 = vortex::flush_denormal(f2b.z1);

        // Output at +/-5V
        outputs[AUDIO_OUTPUT].setVoltage(wet * 5.f);
    }
};

#include "layout.h"

static const char* modeStrings[] = {
    "LP 6dB", "LP 12dB", "LP 24dB",
    "HP 6dB", "HP 12dB", "HP 24dB",
    "BP", "BP+",
    "Notch", "Notch+",
    "AP", "AP+"
};

struct ModeDisplay : Widget {
    VortexMM* module = nullptr;

    ModeDisplay() {
        using namespace vortexmm_layout;
        float w = PANEL_WIDTH - 10;
        box.size = mm2px(Vec(w, 8));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        // Background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(1));
        nvgFillColor(args.vg, nvgRGB(10, 10, 26));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(64, 64, 96));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // Text
        int mode = 0;
        if (module)
            mode = (int)module->params[VortexMM::MODE_PARAM].getValue();

        const char* text = modeStrings[mode];
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(128, 255, 128));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text, nullptr);

        Widget::drawLayer(args, layer);
    }

    void onButton(const ButtonEvent& e) override {
        if (!module) return;

        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            int mode = (int)module->params[VortexMM::MODE_PARAM].getValue();
            mode = (mode + 1) % 12;
            module->params[VortexMM::MODE_PARAM].setValue((float)mode);
            e.consume(this);
        }
        else if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Filter Mode"));
            for (int i = 0; i < 12; i++) {
                int modeIdx = i;
                menu->addChild(createMenuItem(modeStrings[i], "",
                    [=]() { module->params[VortexMM::MODE_PARAM].setValue((float)modeIdx); }));
            }
            e.consume(this);
        }
    }
};

namespace {
struct PanelLabels : Widget {
    PanelLabels() {
        using namespace vortexmm_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        using namespace vortexmm_layout;

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);

        // Title
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(220, 220, 220));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, mm2px(PANEL_WIDTH / 2), mm2px(8.0f), "VortexMM", nullptr);

        // wintoid logo (bottom center, between screws)
        nvgFontSize(args.vg, 10);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        float wintBounds[4];
        nvgTextBounds(args.vg, 0, 0, "wint", nullptr, wintBounds);
        float wintWidth = wintBounds[2] - wintBounds[0];
        float oidBounds[4];
        nvgTextBounds(args.vg, 0, 0, "oid", nullptr, oidBounds);
        float oidWidth = oidBounds[2] - oidBounds[0];
        float totalWidth = wintWidth + oidWidth;

        float logoX = mm2px(PANEL_WIDTH / 2) - totalWidth / 2;
        float logoY = mm2px(124.5f);

        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        nvgText(args.vg, logoX, logoY, "wint", nullptr);

        nvgFillColor(args.vg, nvgRGB(255, 77, 0));
        nvgText(args.vg, logoX + wintWidth, logoY, "oid", nullptr);

        float lineY = logoY + mm2px(2.5f);
        nvgStrokeWidth(args.vg, 1.0f);

        nvgStrokeColor(args.vg, nvgRGBA(255, 255, 255, 200));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, logoX, lineY);
        nvgLineTo(args.vg, logoX + wintWidth, lineY);
        nvgStroke(args.vg);

        nvgStrokeColor(args.vg, nvgRGB(255, 77, 0));
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, logoX + wintWidth, lineY);
        nvgLineTo(args.vg, logoX + totalWidth, lineY);
        nvgStroke(args.vg);

        // Knob labels (above each knob)
        nvgFontSize(args.vg, 9);
        nvgFillColor(args.vg, nvgRGB(180, 180, 180));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);

        nvgText(args.vg, mm2px(CUTOFF_KNOB_X), mm2px(CUTOFF_KNOB_Y - 6.0f), "Cutoff", nullptr);
        nvgText(args.vg, mm2px(RESONANCE_KNOB_X), mm2px(RESONANCE_KNOB_Y - 6.0f), "Reso", nullptr);
        nvgText(args.vg, mm2px(DRIVE_KNOB_X), mm2px(DRIVE_KNOB_Y - 6.0f), "Drive", nullptr);

        // Audio I/O labels
        nvgFontSize(args.vg, 9);
        nvgFillColor(args.vg, nvgRGB(180, 180, 180));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgText(args.vg, mm2px(AUDIO_IN_X), mm2px(AUDIO_IN_Y - 4.5f), "In", nullptr);
        nvgText(args.vg, mm2px(AUDIO_OUT_X), mm2px(AUDIO_OUT_Y - 4.5f), "Out", nullptr);

        Widget::drawLayer(args, layer);
    }
};
} // anonymous namespace

struct VortexMMWidget : ModuleWidget {
    VortexMMWidget(VortexMM* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/VortexMM.svg")));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using namespace vortexmm_layout;

        // Panel labels
        {
            PanelLabels* labels = new PanelLabels();
            addChild(labels);
        }

        // Mode display
        {
            ModeDisplay* display = new ModeDisplay();
            display->module = module;
            float modeW = PANEL_WIDTH - 10;
            display->box.pos = mm2px(Vec(MODE_DISPLAY_X - modeW / 2, MODE_DISPLAY_Y - 4));
            addChild(display);
        }

        // Main knobs (RoundSmallBlackKnob to match FourMM VCA knob size)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CUTOFF_KNOB_X, CUTOFF_KNOB_Y)), module, VortexMM::CUTOFF_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(RESONANCE_KNOB_X, RESONANCE_KNOB_Y)), module, VortexMM::RESONANCE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(DRIVE_KNOB_X, DRIVE_KNOB_Y)), module, VortexMM::DRIVE_PARAM));

        // CV jacks + attenuverters
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_CUTOFF_JACK_X, CV_CUTOFF_JACK_Y)), module, VortexMM::CUTOFF_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_CUTOFF_ATTEN_X, CV_CUTOFF_ATTEN_Y)), module, VortexMM::CUTOFF_CV_ATTEN_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_RESONANCE_JACK_X, CV_RESONANCE_JACK_Y)), module, VortexMM::RESONANCE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_RESONANCE_ATTEN_X, CV_RESONANCE_ATTEN_Y)), module, VortexMM::RESONANCE_CV_ATTEN_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_DRIVE_JACK_X, CV_DRIVE_JACK_Y)), module, VortexMM::DRIVE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_DRIVE_ATTEN_X, CV_DRIVE_ATTEN_Y)), module, VortexMM::DRIVE_CV_ATTEN_PARAM));

        // Audio I/O
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(AUDIO_IN_X, AUDIO_IN_Y)), module, VortexMM::AUDIO_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(AUDIO_OUT_X, AUDIO_OUT_Y)), module, VortexMM::AUDIO_OUTPUT));
    }
};

Model* modelVortexMM = createModel<VortexMM, VortexMMWidget>("VortexMM");
