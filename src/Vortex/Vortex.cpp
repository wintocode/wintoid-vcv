#include "../plugin.hpp"
#include "../polyphony.h"
#include "../finite.h"
#include "dsp.h"

struct CutoffParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        float hz = getValue();
        if (hz >= 1000.f)
            return string::f("%.2f kHz", hz / 1000.f);
        return string::f("%.1f Hz", hz);
    }
};

struct Vortex : Module {
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

    vortex::VoiceState voiceStates[wintoid::polyphony::MAX_CHANNELS];
    int lastMode = -1;
    int previousChannels = 0;
    float previousSampleRate = 0.f;

    void resetAllVoiceStates()
    {
        for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
            voiceStates[lane].reset();
    }

    void clearRuntimeState()
    {
        resetAllVoiceStates();
        lastMode = -1;
        previousChannels = 0;
        previousSampleRate = 0.f;
    }

    void onReset() override
    {
        clearRuntimeState();
    }

    static float readBroadcast(Input& input, int lane)
    {
        const int channels = input.getChannels();
        if (channels <= 0) return 0.f;
        return wintoid::finite_or(
            input.getVoltage(
                wintoid::polyphony::broadcast_lane(lane, channels)),
            0.f);
    }

    void prepareLanes(int channels)
    {
        wintoid::polyphony::reset_changed_lanes(
            previousChannels, channels,
            [&](int lane) { voiceStates[lane].reset(); });
        previousChannels = channels;
    }

    Vortex() {
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
        configInput(AUDIO_INPUT,
                    "Audio (polyphonic voice count, 1 to 16 channels)");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV");
        configInput(RESONANCE_CV_INPUT, "Resonance CV");
        configInput(DRIVE_CV_INPUT, "Drive CV");

        // Output
        configOutput(AUDIO_OUTPUT, "Audio");
        clearRuntimeState();
    }

    void process(const ProcessArgs& args) override
    {
        if (args.sampleRate != previousSampleRate) {
            clearRuntimeState();
            previousSampleRate = args.sampleRate;
        }

        const int mode = (int)params[MODE_PARAM].getValue();
        if (mode != lastMode) {
            resetAllVoiceStates();
            lastMode = mode;
        }

        const int channels = wintoid::polyphony::effective_channels(
            inputs[AUDIO_INPUT].getChannels());
        prepareLanes(channels);
        outputs[AUDIO_OUTPUT].setChannels(channels);

        const float cutoffKnob = params[CUTOFF_PARAM].getValue();
        const float resonance = params[RESONANCE_PARAM].getValue();
        const float baseDamping =
            0.707f * (1.f - resonance) + 0.01f * resonance;
        const float driveKnob = params[DRIVE_PARAM].getValue();
        const float cutoffCvAtten =
            params[CUTOFF_CV_ATTEN_PARAM].getValue();
        const float resonanceCvAtten =
            params[RESONANCE_CV_ATTEN_PARAM].getValue();
        const float driveCvAtten = params[DRIVE_CV_ATTEN_PARAM].getValue();
        const bool cutoffCvConnected = inputs[CUTOFF_CV_INPUT].isConnected();
        const bool resonanceCvConnected =
            inputs[RESONANCE_CV_INPUT].isConnected();
        const bool driveCvConnected = inputs[DRIVE_CV_INPUT].isConnected();

        for (int lane = 0; lane < channels; ++lane) {
            vortex::VoiceState& voice = voiceStates[lane];
            float signal = readBroadcast(inputs[AUDIO_INPUT], lane) / 5.f;

            float cutoff = cutoffKnob;
            if (cutoffCvConnected) {
                const float cutoffCv =
                    readBroadcast(inputs[CUTOFF_CV_INPUT], lane)
                    * cutoffCvAtten;
                cutoff *= vortex::voct_to_mult(cutoffCv);
            }
            cutoff = clamp(cutoff, 20.f, 20000.f);

            float damping = baseDamping;
            if (resonanceCvConnected) {
                const float resonanceCv =
                    readBroadcast(inputs[RESONANCE_CV_INPUT], lane)
                    * resonanceCvAtten * 0.2f;
                damping = clamp(damping - resonanceCv, 0.01f, 0.707f);
            }

            float drive = driveKnob;
            if (driveCvConnected) {
                const float driveCv = readBroadcast(inputs[DRIVE_CV_INPUT], lane)
                    * driveCvAtten / 10.f;
                drive = clamp(drive + driveCv, 0.f, 1.f);
            }
            if (drive > 0.f)
                signal = vortex::soft_clip(signal * (1.f + drive * 9.f));

            float wet = 0.f;
            switch (mode) {
            case 0:
                vortex::filter1_configure_lp(voice.f1, args.sampleRate, cutoff);
                wet = voice.f1.process_lp(signal);
                break;
            case 1:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_LP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_LP);
                break;
            case 2:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_LP);
                vortex::filter2_configure(
                    voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_LP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_LP);
                wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_LP);
                break;
            case 3:
                vortex::filter1_configure_hp(voice.f1, args.sampleRate, cutoff);
                wet = voice.f1.process_hp(signal);
                break;
            case 4:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_HP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_HP);
                break;
            case 5:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_HP);
                vortex::filter2_configure(
                    voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_HP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_HP);
                wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_HP);
                break;
            case 6:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_BP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_BP);
                break;
            case 7:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_BP);
                vortex::filter2_configure(
                    voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_BP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_BP);
                wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_BP);
                break;
            case 8:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
                wet = vortex::filter2_process(
                    voice.f2a, signal, vortex::F2_NOTCH);
                break;
            case 9:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
                vortex::filter2_configure(
                    voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_NOTCH);
                wet = vortex::filter2_process(
                    voice.f2a, signal, vortex::F2_NOTCH);
                wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_NOTCH);
                break;
            case 10:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_AP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_AP);
                break;
            case 11:
                vortex::filter2_configure(
                    voice.f2a, args.sampleRate, cutoff, damping, vortex::F2_AP);
                vortex::filter2_configure(
                    voice.f2b, args.sampleRate, cutoff, damping, vortex::F2_AP);
                wet = vortex::filter2_process(voice.f2a, signal, vortex::F2_AP);
                wet = vortex::filter2_process(voice.f2b, wet, vortex::F2_AP);
                break;
            }

            voice.f1.z = vortex::flush_denormal(voice.f1.z);
            voice.f2a.z0 = vortex::flush_denormal(voice.f2a.z0);
            voice.f2a.z1 = vortex::flush_denormal(voice.f2a.z1);
            voice.f2b.z0 = vortex::flush_denormal(voice.f2b.z0);
            voice.f2b.z1 = vortex::flush_denormal(voice.f2b.z1);
            outputs[AUDIO_OUTPUT].setVoltage(wet * 5.f, lane);
        }
    }
};

#include "layout.h"
#include "../ui_geometry.h"

static const char* modeStrings[] = {
    "LP 6dB", "LP 12dB", "LP 24dB",
    "HP 6dB", "HP 12dB", "HP 24dB",
    "BP", "BP+",
    "Notch", "Notch+",
    "AP", "AP+"
};

struct ModeDisplay : Widget {
    Vortex* module = nullptr;

    ModeDisplay() {
        using namespace vortex_layout;
        float w = PANEL_WIDTH - 10;
        box.size = mm2px(Vec(w, 8));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        // Background: inset by the stroke so hosts that clip at the widget
        // box (MetaModule) do not cut the outer half of the border.
        const float strokeWidth = 0.5f;
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, inset, inset,
                       wintoid::ui::inset_extent(box.size.x, strokeWidth),
                       wintoid::ui::inset_extent(box.size.y, strokeWidth),
                       mm2px(1));
        nvgFillColor(args.vg, nvgRGB(10, 10, 26));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(64, 64, 96));
        nvgStrokeWidth(args.vg, strokeWidth);
        nvgStroke(args.vg);

        // Text: select the font explicitly; if it fails to load, keep the
        // background/border and skip the text rather than inherit another
        // widget's font.
        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (font) {
            int mode = 0;
            if (module)
                mode = (int)module->params[Vortex::MODE_PARAM].getValue();

            const char* text = modeStrings[mode];
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 14);
            nvgFillColor(args.vg, nvgRGB(128, 255, 128));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, text, nullptr);
        }

        Widget::drawLayer(args, layer);
    }

    void onButton(const ButtonEvent& e) override {
        if (!module) return;

        if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT) {
            int mode = (int)module->params[Vortex::MODE_PARAM].getValue();
            mode = (mode + 1) % 12;
            module->params[Vortex::MODE_PARAM].setValue((float)mode);
            e.consume(this);
        }
        else if (e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT) {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Filter Mode"));
            for (int i = 0; i < 12; i++) {
                int modeIdx = i;
                menu->addChild(createMenuItem(modeStrings[i], "",
                    [=]() { module->params[Vortex::MODE_PARAM].setValue((float)modeIdx); }));
            }
            e.consume(this);
        }
    }
};

namespace {
struct PanelLabels : Widget {
    PanelLabels() {
        using namespace vortex_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) return;

        using namespace vortex_layout;

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) return;
        nvgFontFaceId(args.vg, font->handle);

        // Title
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(220, 220, 220));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, mm2px(PANEL_WIDTH / 2), mm2px(TITLE_Y), "Vortex", nullptr);

        // wintoid logo (bottom center on the screw-free panel)
        nvgFontSize(args.vg, LOGO_FONT_SIZE);
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);

        float wintBounds[4];
        nvgTextBounds(args.vg, 0, 0, "wint", nullptr, wintBounds);
        float wintWidth = wintBounds[2] - wintBounds[0];
        float oidBounds[4];
        nvgTextBounds(args.vg, 0, 0, "oid", nullptr, oidBounds);
        float oidWidth = oidBounds[2] - oidBounds[0];
        float totalWidth = wintWidth + oidWidth;

        float logoX = mm2px(PANEL_WIDTH / 2) - totalWidth / 2;
        float logoY = mm2px(LOGO_BASELINE_Y);

        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        nvgText(args.vg, logoX, logoY, "wint", nullptr);

        nvgFillColor(args.vg, nvgRGB(255, 77, 0));
        nvgText(args.vg, logoX + wintWidth, logoY, "oid", nullptr);

        float lineY = logoY + mm2px(LOGO_UNDERLINE_OFFSET);
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

        nvgText(args.vg, mm2px(CUTOFF_KNOB_X), mm2px(CUTOFF_KNOB_Y - KNOB_LABEL_OFFSET), "Cutoff", nullptr);
        nvgText(args.vg, mm2px(RESONANCE_KNOB_X), mm2px(RESONANCE_KNOB_Y - KNOB_LABEL_OFFSET), "Reso", nullptr);
        nvgText(args.vg, mm2px(DRIVE_KNOB_X), mm2px(DRIVE_KNOB_Y - KNOB_LABEL_OFFSET), "Drive", nullptr);

        // Audio I/O labels
        nvgFontSize(args.vg, 9);
        nvgFillColor(args.vg, nvgRGB(180, 180, 180));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_BOTTOM);
        nvgText(args.vg, mm2px(AUDIO_IN_X), mm2px(AUDIO_IN_Y - AUDIO_LABEL_OFFSET), "In", nullptr);
        nvgText(args.vg, mm2px(AUDIO_OUT_X), mm2px(AUDIO_OUT_Y - AUDIO_LABEL_OFFSET), "Out", nullptr);

        Widget::drawLayer(args, layer);
    }
};
} // anonymous namespace

struct VortexWidget : ModuleWidget {
    VortexWidget(Vortex* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Vortex.svg")));

        using namespace vortex_layout;

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

        // Main knobs (RoundSmallBlackKnob to match Four VCA knob size)
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(CUTOFF_KNOB_X, CUTOFF_KNOB_Y)), module, Vortex::CUTOFF_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(RESONANCE_KNOB_X, RESONANCE_KNOB_Y)), module, Vortex::RESONANCE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(DRIVE_KNOB_X, DRIVE_KNOB_Y)), module, Vortex::DRIVE_PARAM));

        // CV jacks + attenuverters
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_CUTOFF_JACK_X, CV_CUTOFF_JACK_Y)), module, Vortex::CUTOFF_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_CUTOFF_ATTEN_X, CV_CUTOFF_ATTEN_Y)), module, Vortex::CUTOFF_CV_ATTEN_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_RESONANCE_JACK_X, CV_RESONANCE_JACK_Y)), module, Vortex::RESONANCE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_RESONANCE_ATTEN_X, CV_RESONANCE_ATTEN_Y)), module, Vortex::RESONANCE_CV_ATTEN_PARAM));

        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(CV_DRIVE_JACK_X, CV_DRIVE_JACK_Y)), module, Vortex::DRIVE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(CV_DRIVE_ATTEN_X, CV_DRIVE_ATTEN_Y)), module, Vortex::DRIVE_CV_ATTEN_PARAM));

        // Audio I/O
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(AUDIO_IN_X, AUDIO_IN_Y)), module, Vortex::AUDIO_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(AUDIO_OUT_X, AUDIO_OUT_Y)), module, Vortex::AUDIO_OUTPUT));
    }
};

Model* modelVortex = createModel<Vortex, VortexWidget>("VortexMM");
