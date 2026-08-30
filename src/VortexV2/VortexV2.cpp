#include "../plugin.hpp"
#include "../polyphony.h"
#include "../ui_geometry.h"
#include "dsp.h"
#include "layout.h"
#include "runtime.h"

struct VortexV2CutoffParamQuantity : ParamQuantity {
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

    void resetOutputBranch(int output)
    {
        for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
            voiceStates[lane].branches[output].reset();
    }

    static float readBroadcast(Input& input, int lane)
    {
        return vortex_v2::runtime::read_broadcast(
            lane, input.getChannels(),
            [&](int sourceLane) { return input.getVoltage(sourceLane); });
    }

    void prepareLanes(int channels)
    {
        vortex_v2::runtime::prepare_lanes(
            previousChannels, channels,
            [&](int lane) { voiceStates[lane].reset(); });
        previousChannels = channels;
    }

    VortexV2()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        auto* cutoff = configParam<VortexV2CutoffParamQuantity>(
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
        bool activeOutputs[vortex_v2::OUTPUT_COUNT] = {};
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const int outputId = outputIds[output];
            activeOutputs[output] = vortex_v2::runtime::select_output_branch(
                outputs[outputId].isConnected(),
                previousOutputConnected[output],
                channels,
                [&](int channels) {
                    outputs[outputId].setChannels(channels);
                },
                [&]() { resetOutputBranch(output); });
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
                if (!activeOutputs[output])
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

    void drawLabel(const DrawArgs& args, float x, float y,
                   float size, int align, const char* text) const
    {
        const float strokeWidth = 0.f;
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float width = wintoid::ui::inset_extent(
            box.size.x, strokeWidth);
        const float height = wintoid::ui::inset_extent(
            box.size.y, strokeWidth);
        nvgFontSize(args.vg, mm2px(size));
        nvgTextAlign(args.vg, align);
        nvgText(args.vg,
                wintoid::ui::clamp_stroke_center(mm2px(x) + inset,
                                                 width, strokeWidth),
                wintoid::ui::clamp_stroke_center(mm2px(y) + inset,
                                                 height, strokeWidth),
                text, nullptr);
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
        const int leftBaseline = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
        const char* controlLabels[3] = {"CUTOFF", "RESO", "DRIVE"};
        const float controlXs[3] = {
            CUTOFF_KNOB_X, RESONANCE_KNOB_X, DRIVE_KNOB_X
        };
        const float cvXs[3] = {
            CUTOFF_CV_X, RESONANCE_CV_X, DRIVE_CV_X
        };
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        drawLabel(args, TITLE_X, TITLE_Y, TITLE_FONT_SIZE,
                  leftBaseline, "VortexV2");

        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        drawLabel(args, LOGO_X, LOGO_Y, LOGO_FONT_SIZE,
                  leftBaseline, "wint");
        nvgFillColor(args.vg, nvgRGB(255, 77, 0));
        drawLabel(args, LOGO_OID_X, LOGO_Y, LOGO_FONT_SIZE,
                  leftBaseline, "oid");

        nvgFillColor(args.vg, nvgRGB(183, 105, 60));
        drawLabel(args, GLOBAL_SECTION_LABEL_X, GLOBAL_SECTION_LABEL_Y,
                  SECTION_LABEL_FONT_SIZE, leftBaseline,
                  "GLOBAL CONTROLS");
        drawLabel(args, OUTPUT_SECTION_LABEL_X, OUTPUT_SECTION_LABEL_Y,
                  SECTION_LABEL_FONT_SIZE, leftBaseline,
                  "FILTER OUTPUTS");

        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        for (int index = 0; index < 3; ++index)
            drawLabel(args, controlXs[index], CONTROL_LABEL_Y,
                      CONTROL_LABEL_FONT_SIZE, centerBaseline,
                      controlLabels[index]);
        for (int index = 0; index < 3; ++index)
            drawLabel(args, cvXs[index], CV_LABEL_Y, CV_LABEL_FONT_SIZE,
                      centerBaseline, "CV");
        drawLabel(args, AUDIO_IN_X, AUDIO_IN_LABEL_Y, AUDIO_IN_LABEL_FONT_SIZE,
                  centerBaseline, "IN");

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
                      OUTPUT_LABEL_FONT_SIZE, centerBaseline,
                      outputLabels[output]);
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
