#include "../plugin.hpp"
#include "../polyphony.h"
#include "../finite.h"
#include "../ui_geometry.h"
#include "dsp.h"
#include "layout.h"
#include "runtime.h"

struct VortexV2CutoffParamQuantity : ParamQuantity {
    float getDisplayValue() override {
        return vortex_v2::cutoff_param_to_hz(getValue());
    }

    void setDisplayValue(float hz) override {
        setValue(vortex_v2::cutoff_hz_to_param(hz));
    }

    std::string getDisplayValueString() override {
        const float hz = getDisplayValue();
        if (hz >= 1000.f)
            return string::f("%.2f kHz", hz / 1000.f);
        return string::f("%.1f Hz", hz);
    }

    json_t* toJson() override {
        json_t* rootJ = ParamQuantity::toJson();
        json_object_set_new(rootJ, "value", json_real(getDisplayValue()));
        return rootJ;
    }

    void fromJson(json_t* rootJ) override {
        json_t* valueJ = json_object_get(rootJ, "value");
        if (valueJ && json_number_value(valueJ) > 1.0) {
            setValue(vortex_v2::cutoff_hz_to_param(
                static_cast<float>(json_number_value(valueJ))));
            return;
        }
        ParamQuantity::fromJson(rootJ);
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
        VOCT_ATTEN_PARAM,
        PARAMS_LEN
    };
    enum InputId {
        AUDIO_INPUT,
        CUTOFF_CV_INPUT,
        RESONANCE_CV_INPUT,
        DRIVE_CV_INPUT,
        VOCT_INPUT,
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
            [&](int sourceLane) {
                return wintoid::finite_or(
                    input.getVoltage(sourceLane), 0.f);
            });
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
            CUTOFF_PARAM, 0.f, 1.f,
            vortex_v2::cutoff_hz_to_param(1000.f), "Cutoff");
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
        configParam(VOCT_ATTEN_PARAM, -1.f, 1.f, 1.f,
                    "V/Oct", "%", 0.f, 100.f);

        configInput(AUDIO_INPUT,
                    "Audio (polyphonic voice count, 1 to 16 channels)");
        configInput(CUTOFF_CV_INPUT, "Cutoff CV");
        configInput(RESONANCE_CV_INPUT, "Resonance CV");
        configInput(DRIVE_CV_INPUT, "Drive CV");
        configInput(VOCT_INPUT, "V/Oct");

        configOutput(LP6_OUTPUT, "LP 6dB");
        configOutput(LP12_OUTPUT, "LP 12dB");
        configOutput(LP24_OUTPUT, "LP 24dB");
        configOutput(HP6_OUTPUT, "HP 6dB");
        configOutput(HP12_OUTPUT, "HP 12dB");
        configOutput(HP24_OUTPUT, "HP 24dB");
        configOutput(BP_OUTPUT, "BP");
        configOutput(BP_PLUS_OUTPUT, "BP+");
        configOutput(NOTCH_OUTPUT, "NOTCH");
        configOutput(NOTCH_PLUS_OUTPUT, "NOTCH+");
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
        const float voctAtten = params[VOCT_ATTEN_PARAM].getValue();
        const bool cutoffCvConnected = inputs[CUTOFF_CV_INPUT].isConnected();
        const bool resonanceCvConnected =
            inputs[RESONANCE_CV_INPUT].isConnected();
        const bool driveCvConnected = inputs[DRIVE_CV_INPUT].isConnected();
        const bool voctConnected = inputs[VOCT_INPUT].isConnected();
        const bool reuseFilterCoefficients =
            !cutoffCvConnected && !resonanceCvConnected && !voctConnected;

        for (int lane = 0; lane < channels; ++lane) {
            float signal = readBroadcast(inputs[AUDIO_INPUT], lane) / 5.f;

            float cutoff = vortex_v2::cutoff_param_to_hz(cutoffKnob);
            const float voct = readBroadcast(inputs[VOCT_INPUT], lane)
                * voctAtten;
            cutoff = vortex_v2::cutoff_with_voct(cutoff, voct);
            if (cutoffCvConnected) {
                const float cutoffCv = readBroadcast(
                    inputs[CUTOFF_CV_INPUT], lane) * cutoffCvAtten;
                cutoff *= vortex::voct_to_mult(cutoffCv);
            }
            cutoff = clamp(cutoff, vortex_v2::MIN_CUTOFF_HZ,
                           vortex_v2::MAX_CUTOFF_HZ);

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
                signal = vortex_v2::drive_saturate(signal * (1.f + drive * 9.f));

            for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
                if (!activeOutputs[output])
                    continue;
                const float wet = vortex_v2::process_branch(
                    voiceStates[lane].branches[output],
                    vortex_v2::OUTPUT_MODES[output], signal,
                    args.sampleRate, cutoff, damping,
                    reuseFilterCoefficients);
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
    struct Label {
        float x;
        float y;
        float size;
        int align;
        int red;
        int green;
        int blue;
        const char* text;
        bool bold;
    };

    VortexV2PanelLabels()
    {
        using namespace vortex_v2_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    void drawLabel(const DrawArgs& args, const Label& label) const
    {
        const float strokeWidth = 0.f;
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float width = wintoid::ui::inset_extent(
            box.size.x, strokeWidth);
        const float height = wintoid::ui::inset_extent(
            box.size.y, strokeWidth);
        nvgFontSize(args.vg, mm2px(label.size));
        nvgFillColor(args.vg, nvgRGB(label.red, label.green, label.blue));
        nvgTextAlign(args.vg, label.align);
        const float textX = mm2px(label.x);
        const float textY = mm2px(label.y);
        auto draw = [&](float x) {
            nvgText(args.vg,
                    wintoid::ui::clamp_stroke_center(x + inset,
                                                     width, strokeWidth),
                    wintoid::ui::clamp_stroke_center(textY + inset,
                                                     height, strokeWidth),
                    label.text, nullptr);
        };
        if (label.bold) {
            const float weightOffset = mm2px(0.10f);
            draw(textX - weightOffset);
            draw(textX + weightOffset);
        }
        draw(textX);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        using namespace vortex_v2_layout;
        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) {
            Widget::drawLayer(args, layer);
            return;
        }
        nvgFontFaceId(args.vg, font->handle);

        const int centerBaseline = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
        const int leftBaseline = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
        const Label labels[] = {
            {TITLE_X, TITLE_Y, TITLE_FONT_SIZE, leftBaseline,
             36, 37, 34, "Vortex V2", true},
            {CUTOFF_KNOB_X, CUTOFF_LABEL_Y, CONTROL_LABEL_FONT_SIZE,
             centerBaseline, 36, 37, 34, "CUTOFF", false},
            {RESONANCE_KNOB_X, RESONANCE_LABEL_Y, CONTROL_LABEL_FONT_SIZE,
             centerBaseline, 36, 37, 34, "RESO", false},
            {DRIVE_KNOB_X, DRIVE_LABEL_Y, CONTROL_LABEL_FONT_SIZE,
             centerBaseline, 36, 37, 34, "DRIVE", false},
            {AUDIO_IN_X, AUDIO_IN_LABEL_Y, AUDIO_IN_LABEL_FONT_SIZE,
             centerBaseline, 36, 37, 34, "IN", false},
            {VOCT_INPUT_X, VOCT_LABEL_Y, VOCT_LABEL_FONT_SIZE,
             centerBaseline, 36, 37, 34, "V/OCT", false},
        };
        for (const Label& label : labels)
            drawLabel(args, label);

        const char* outputLabels[vortex_v2::OUTPUT_COUNT] = {
            "LP 6dB", "LP 12dB", "LP 24dB",
            "HP 6dB", "HP 12dB", "HP 24dB",
            "BP", "BP+", "NOTCH", "NOTCH+", "AP", "AP+"
        };
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        for (int output = 0; output < vortex_v2::OUTPUT_COUNT; ++output) {
            const int column = output % 3;
            const int row = output / 3;
            drawLabel(args, {
                OUTPUT_COLUMN_XS[column],
                OUTPUT_ROW_YS[row] - OUTPUT_LABEL_OFFSET,
                OUTPUT_LABEL_FONT_SIZE,
                centerBaseline,
                36, 37, 34,
                outputLabels[output],
                false,
            });
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
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(VOCT_INPUT_X, VOCT_INPUT_Y)), module,
            VortexV2::VOCT_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(VOCT_ATTEN_X, VOCT_ATTEN_Y)), module,
            VortexV2::VOCT_ATTEN_PARAM));

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
