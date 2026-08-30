#include "../plugin.hpp"
#include "../polyphony.h"
#include "../ui_geometry.h"
#include "dsp.h"
#include "layout.h"

struct CutoffParamQuantity : ParamQuantity {
    std::string getDisplayValueString() override {
        const float hz = getValue();
        if (hz >= 1000.f)
            return string::f("%.2f kHz", hz / 1000.f);
        return string::f("%.1f Hz", hz);
    }
};

struct VortexV2 : Module {
    enum ParamId {
        CUTOFF_PARAM,
        RESONANCE_PARAM,
        DRIVE_PARAM,
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
        LP6_OUTPUT,
        LP12_OUTPUT,
        LP24_OUTPUT,
        HP6_OUTPUT,
        HP12_OUTPUT,
        HP24_OUTPUT,
        BP_OUTPUT,
        BP_PLUS_OUTPUT,
        NOTCH_OUTPUT,
        NOTCH_PLUS_OUTPUT,
        AP_OUTPUT,
        AP_PLUS_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    vortex_v2::VoiceState voiceStates[wintoid::polyphony::MAX_CHANNELS];
    bool previousOutputConnected[vortex_v2::OUTPUT_COUNT] = {};
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
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output)
            previousOutputConnected[output] = false;
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
        if (channels <= 0)
            return 0.f;
        return input.getVoltage(
            wintoid::polyphony::broadcast_lane(lane, channels));
    }

    void prepareLanes(int channels)
    {
        wintoid::polyphony::reset_changed_lanes(
            previousChannels, channels,
            [&](int lane) { voiceStates[lane].reset(); });
        previousChannels = channels;
    }

    VortexV2()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        auto* cutoff = configParam<CutoffParamQuantity>(
            CUTOFF_PARAM, 20.f, 20000.f, 1000.f, "Cutoff");
        (void) cutoff;
        configParam(RESONANCE_PARAM, 0.f, 1.f, 0.f,
                    "Resonance", "%", 0.f, 100.f);
        configParam(DRIVE_PARAM, 0.f, 1.f, 0.f,
                    "Drive", "%", 0.f, 100.f);
        configParam(CUTOFF_CV_ATTEN_PARAM, -1.f, 1.f, 0.f,
                    "Cutoff CV", "%", 0.f, 100.f);
        configParam(RESONANCE_CV_ATTEN_PARAM, -1.f, 1.f, 0.f,
                    "Resonance CV", "%", 0.f, 100.f);
        configParam(DRIVE_CV_ATTEN_PARAM, -1.f, 1.f, 0.f,
                    "Drive CV", "%", 0.f, 100.f);

        configInput(AUDIO_INPUT,
                    "Audio (polyphonic voice count, 1 to 16 channels)");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV");
        configInput(RESONANCE_CV_INPUT, "Resonance CV");
        configInput(DRIVE_CV_INPUT, "Drive CV");

        configOutput(LP6_OUTPUT, "LP 6dB");
        configOutput(LP12_OUTPUT, "LP 12dB");
        configOutput(LP24_OUTPUT, "LP 24dB");
        configOutput(HP6_OUTPUT, "HP 6dB");
        configOutput(HP12_OUTPUT, "HP 12dB");
        configOutput(HP24_OUTPUT, "HP 24dB");
        configOutput(BP_OUTPUT, "BP");
        configOutput(BP_PLUS_OUTPUT, "BP+");
        configOutput(NOTCH_OUTPUT, "Notch");
        configOutput(NOTCH_PLUS_OUTPUT, "Notch+");
        configOutput(AP_OUTPUT, "AP");
        configOutput(AP_PLUS_OUTPUT, "AP+");

        clearRuntimeState();
    }

    void process(const ProcessArgs& args) override
    {
        if (args.sampleRate != previousSampleRate) {
            clearRuntimeState();
            previousSampleRate = args.sampleRate;
        }

        const int channels = wintoid::polyphony::effective_channels(
            inputs[AUDIO_INPUT].getChannels());
        prepareLanes(channels);

        const int outputIds[] = {
            LP6_OUTPUT, LP12_OUTPUT, LP24_OUTPUT,
            HP6_OUTPUT, HP12_OUTPUT, HP24_OUTPUT,
            BP_OUTPUT, BP_PLUS_OUTPUT, NOTCH_OUTPUT, NOTCH_PLUS_OUTPUT,
            AP_OUTPUT, AP_PLUS_OUTPUT
        };
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const int outputId = outputIds[output];
            const bool connected = outputs[outputId].isConnected();
            if (!connected) {
                if (previousOutputConnected[output]) {
                    for (int lane = 0;
                         lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
                        voiceStates[lane].branches[output].reset();
                }
                previousOutputConnected[output] = false;
                continue;
            }
            outputs[outputId].setChannels(channels);
            previousOutputConnected[output] = true;
        }

        const float cutoffKnob = params[CUTOFF_PARAM].getValue();
        const float resonance = params[RESONANCE_PARAM].getValue();
        const float baseDamping =
            0.707f * (1.f - resonance) + 0.01f * resonance;
        const float driveKnob = params[DRIVE_PARAM].getValue();
        const float cutoffCvAtten = params[CUTOFF_CV_ATTEN_PARAM].getValue();
        const float resonanceCvAtten =
            params[RESONANCE_CV_ATTEN_PARAM].getValue();
        const float driveCvAtten = params[DRIVE_CV_ATTEN_PARAM].getValue();
        const bool cutoffCvConnected = inputs[CUTOFF_CV_INPUT].isConnected();
        const bool resonanceCvConnected =
            inputs[RESONANCE_CV_INPUT].isConnected();
        const bool driveCvConnected = inputs[DRIVE_CV_INPUT].isConnected();

        for (int lane = 0; lane < channels; ++lane) {
            float signal = readBroadcast(inputs[AUDIO_INPUT], lane) / 5.f;

            float cutoff = cutoffKnob;
            if (cutoffCvConnected) {
                const float cutoffCv = readBroadcast(
                    inputs[CUTOFF_CV_INPUT], lane) * cutoffCvAtten;
                cutoff *= vortex::voct_to_mult(cutoffCv);
            }
            cutoff = clamp(cutoff, 20.f, 20000.f);

            float damping = baseDamping;
            if (resonanceCvConnected) {
                const float resonanceCv = readBroadcast(
                    inputs[RESONANCE_CV_INPUT], lane)
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

            for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
                if (!previousOutputConnected[output])
                    continue;
                const float wet = vortex_v2::process_branch(
                    voiceStates[lane].branches[output],
                    vortex_v2::OUTPUT_MODES[output], signal,
                    args.sampleRate, cutoff, damping);
                voiceStates[lane].branches[output].f1.z =
                    vortex::flush_denormal(
                        voiceStates[lane].branches[output].f1.z);
                voiceStates[lane].branches[output].f2a.z0 =
                    vortex::flush_denormal(
                        voiceStates[lane].branches[output].f2a.z0);
                voiceStates[lane].branches[output].f2a.z1 =
                    vortex::flush_denormal(
                        voiceStates[lane].branches[output].f2a.z1);
                voiceStates[lane].branches[output].f2b.z0 =
                    vortex::flush_denormal(
                        voiceStates[lane].branches[output].f2b.z0);
                voiceStates[lane].branches[output].f2b.z1 =
                    vortex::flush_denormal(
                        voiceStates[lane].branches[output].f2b.z1);
                outputs[outputIds[output]].setVoltage(wet * 5.f, lane);
            }
        }
    }
};

struct VortexV2PanelLabels : Widget {
    VortexV2PanelLabels()
    {
        using namespace vortex_v2_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    static void drawLabel(const DrawArgs& args, float x, float y,
                          float size, int align, const char* text)
    {
        nvgFontSize(args.vg, mm2px(size));
        nvgTextAlign(args.vg, align);
        nvgText(args.vg, mm2px(x), mm2px(y), text, nullptr);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return;

        using namespace vortex_v2_layout;
        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font)
            return;
        nvgFontFaceId(args.vg, font->handle);

        const int centerBaseline = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        drawLabel(args, TITLE_X, TITLE_Y, TITLE_FONT_SIZE,
                  NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, "VortexV2");

        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        drawLabel(args, LOGO_X, LOGO_Y, LOGO_FONT_SIZE,
                  NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, "wint");
        nvgFillColor(args.vg, nvgRGB(255, 77, 0));
        drawLabel(args, LOGO_X + 6.0f, LOGO_Y, LOGO_FONT_SIZE,
                  NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE, "oid");

        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        drawLabel(args, CUTOFF_KNOB_X, CUTOFF_KNOB_Y - 7.f, 2.8f,
                  centerBaseline, "CUTOFF");
        drawLabel(args, RESONANCE_KNOB_X, RESONANCE_KNOB_Y - 7.f, 2.8f,
                  centerBaseline, "RESO");
        drawLabel(args, DRIVE_KNOB_X, DRIVE_KNOB_Y - 7.f, 2.8f,
                  centerBaseline, "DRIVE");
        drawLabel(args, AUDIO_IN_X, AUDIO_IN_Y - 5.f, 2.8f,
                  centerBaseline, "IN");

        const float strokeWidth = mm2px(0.35f);
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float headerWidth = mm2px(33.f);
        const float headerHeight = mm2px(5.5f);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, mm2px(OUTPUTS_SECTION_X) + inset,
                       mm2px(OUTPUTS_SECTION_Y) + inset,
                       wintoid::ui::inset_extent(headerWidth, strokeWidth),
                       wintoid::ui::inset_extent(headerHeight, strokeWidth),
                       mm2px(0.8f));
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(85, 109, 128));
        nvgStrokeWidth(args.vg, strokeWidth);
        nvgStroke(args.vg);

        nvgFillColor(args.vg, nvgRGB(236, 232, 217));
        drawLabel(args, OUTPUTS_SECTION_X + 16.5f,
                  OUTPUTS_SECTION_Y + 3.7f, 2.8f, centerBaseline,
                  "FILTER OUTPUTS");

        const char* outputLabels[vortex_v2::OUTPUT_COUNT] = {
            "LP 6dB", "LP 12dB", "LP 24dB",
            "HP 6dB", "HP 12dB", "HP 24dB",
            "BP", "BP+", "Notch", "Notch+", "AP", "AP+"
        };
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const int column = output % 3;
            const int row = output / 3;
            drawLabel(args, OUTPUT_COLUMN_XS[column],
                      OUTPUT_ROW_YS[row] - OUTPUT_LABEL_OFFSET,
                      2.6f, centerBaseline, outputLabels[output]);
        }

        Widget::drawLayer(args, layer);
    }
};

struct VortexV2Widget : ModuleWidget {
    VortexV2Widget(VortexV2* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/VortexV2.svg")));

        using namespace vortex_v2_layout;
        addChild(new VortexV2PanelLabels());

        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(CUTOFF_KNOB_X, CUTOFF_KNOB_Y)), module,
            VortexV2::CUTOFF_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(RESONANCE_KNOB_X, RESONANCE_KNOB_Y)), module,
            VortexV2::RESONANCE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(DRIVE_KNOB_X, DRIVE_KNOB_Y)), module,
            VortexV2::DRIVE_PARAM));

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(CUTOFF_CV_X, CUTOFF_CV_Y)), module,
            VortexV2::CUTOFF_CV_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(CUTOFF_ATTEN_X, CUTOFF_ATTEN_Y)), module,
            VortexV2::CUTOFF_CV_ATTEN_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(RESONANCE_CV_X, RESONANCE_CV_Y)), module,
            VortexV2::RESONANCE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(RESONANCE_ATTEN_X, RESONANCE_ATTEN_Y)), module,
            VortexV2::RESONANCE_CV_ATTEN_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(DRIVE_CV_X, DRIVE_CV_Y)), module,
            VortexV2::DRIVE_CV_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(DRIVE_ATTEN_X, DRIVE_ATTEN_Y)), module,
            VortexV2::DRIVE_CV_ATTEN_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(AUDIO_IN_X, AUDIO_IN_Y)), module,
            VortexV2::AUDIO_INPUT));

        const int outputIds[] = {
            VortexV2::LP6_OUTPUT, VortexV2::LP12_OUTPUT,
            VortexV2::LP24_OUTPUT, VortexV2::HP6_OUTPUT,
            VortexV2::HP12_OUTPUT, VortexV2::HP24_OUTPUT,
            VortexV2::BP_OUTPUT, VortexV2::BP_PLUS_OUTPUT,
            VortexV2::NOTCH_OUTPUT, VortexV2::NOTCH_PLUS_OUTPUT,
            VortexV2::AP_OUTPUT, VortexV2::AP_PLUS_OUTPUT
        };
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const int column = output % 3;
            const int row = output / 3;
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(OUTPUT_COLUMN_XS[column], OUTPUT_ROW_YS[row])),
                module, outputIds[output]));
        }
    }
};

Model* modelVortexV2 = createModel<VortexV2, VortexV2Widget>("VortexV2");
