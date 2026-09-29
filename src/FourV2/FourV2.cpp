#include "../plugin.hpp"
#include "../polyphony.h"
#include "../finite.h"
#include "../text_entry.h"
#include "../ui_geometry.h"
#include "engine.h"
#include "layout.h"
#include "routing.h"

#include <cmath>
#include <string>

namespace {

inline float unit_param(float value, float fallback)
{
    return clamp(four_v2::finite_or(value, fallback), 0.f, 1.f);
}

inline float bipolar_param(float value, float fallback = 0.f)
{
    return clamp(four_v2::finite_or(value, fallback), -1.f, 1.f);
}

inline float cents_multiplier(float cents)
{
    return four_v2::fine_multiplier(cents);
}

inline int fold_type_index(float value)
{
    value = four_v2::finite_or(value, 0.f);
    value = fmaxf(0.f, fminf(2.f, value));
    return static_cast<int>(floorf(value + 0.5f));
}

struct CoarseParamQuantity : ParamQuantity {
    int freqModeParamId = 0;
    int fineParamId = 0;

    int mode() {
        return module ? four_v2::clamp_mode(
                            module->params[freqModeParamId].getValue())
                      : four_v2::RATIO_MODE;
    }

    float fine() {
        return module ? module->params[fineParamId].getValue() : 0.f;
    }

    std::string getDisplayValueString() override {
        return four_v2::frequency_label(getValue(), mode(), fine());
    }

    // Typed text is read in the displayed units: hertz in Fixed mode, a
    // ratio such as "3:2" in Ratio mode. Unparseable text is ignored.
    void setDisplayValueString(std::string s) override {
        if (mode() == four_v2::FIXED_MODE) {
            float hz = 0.f;
            if (wintoid::text_entry::parse_frequency_hz(s, hz))
                setValue(four_v2::coarse_from_fixed_frequency(hz, fine()));
            return;
        }
        float ratio = 0.f;
        if (wintoid::text_entry::parse_ratio(s, ratio))
            setValue(four_v2::coarse_from_ratio(ratio));
    }
};

struct FourV2FrequencyModeSwitch : app::SvgSwitch {
    FourV2FrequencyModeSwitch()
    {
        shadow->opacity = 0.0;
        addFrame(Svg::load(asset::plugin(
            pluginInstance, "res/FourV2FrequencyMode_Ratio.svg")));
        addFrame(Svg::load(asset::plugin(
            pluginInstance, "res/FourV2FrequencyMode_Fixed.svg")));
    }
};

struct FourV2FoldTypeSwitch : app::SvgSwitch {
    FourV2FoldTypeSwitch()
    {
        shadow->opacity = 0.0;
        addFrame(Svg::load(asset::plugin(
            pluginInstance, "res/FourV2FoldType_Symmetric.svg")));
        addFrame(Svg::load(asset::plugin(
            pluginInstance, "res/FourV2FoldType_Asymmetric.svg")));
        addFrame(Svg::load(asset::plugin(
            pluginInstance, "res/FourV2FoldType_SoftClip.svg")));
    }
};

} // namespace

struct FourV2 : Module {
    enum ParamId {
        ALGORITHM_PARAM, TUNE_PARAM, PM_DEPTH_PARAM, PM_DEPTH_CV_ATTEN_PARAM,
        MASTER_PARAM, EXT_PM_ATTEN_PARAM,
        OP1_COARSE_PARAM, OP2_COARSE_PARAM, OP3_COARSE_PARAM, OP4_COARSE_PARAM,
        OP1_FINE_PARAM, OP2_FINE_PARAM, OP3_FINE_PARAM, OP4_FINE_PARAM,
        OP1_OUTPUT_PARAM, OP2_OUTPUT_PARAM, OP3_OUTPUT_PARAM, OP4_OUTPUT_PARAM,
        OP1_WARP_PARAM, OP2_WARP_PARAM, OP3_WARP_PARAM, OP4_WARP_PARAM,
        OP1_FOLD_PARAM, OP2_FOLD_PARAM, OP3_FOLD_PARAM, OP4_FOLD_PARAM,
        OP1_FEEDBACK_PARAM, OP2_FEEDBACK_PARAM, OP3_FEEDBACK_PARAM, OP4_FEEDBACK_PARAM,
        OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM, OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM,
        OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM, OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM,
        OP1_OUTPUT_CV_ATTEN_PARAM, OP2_OUTPUT_CV_ATTEN_PARAM,
        OP3_OUTPUT_CV_ATTEN_PARAM, OP4_OUTPUT_CV_ATTEN_PARAM,
        OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM,
        OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM,
        OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM,
        OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM,
        OP1_FEEDBACK_CV_ATTEN_PARAM, OP2_FEEDBACK_CV_ATTEN_PARAM,
        OP3_FEEDBACK_CV_ATTEN_PARAM, OP4_FEEDBACK_CV_ATTEN_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        VOCT_INPUT, PM_DEPTH_CV_INPUT, EXT_PM_INPUT,
        OP1_OUTPUT_CV_INPUT, OP2_OUTPUT_CV_INPUT,
        OP3_OUTPUT_CV_INPUT, OP4_OUTPUT_CV_INPUT,
        OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT,
        OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT,
        OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT,
        OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT,
        OP1_FEEDBACK_CV_INPUT, OP2_FEEDBACK_CV_INPUT,
        OP3_FEEDBACK_CV_INPUT, OP4_FEEDBACK_CV_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        MAIN_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        OVER_LIGHT,
        LIGHTS_LEN
    };

    four_v2::EngineState engineStates[wintoid::polyphony::MAX_CHANNELS];
    four_v2::OverDetector overDetector;
    int previousChannels = 0;
    float previousSampleRate = 0.f;

    void resetLane(int lane)
    {
        four_v2::reset(engineStates[lane]);
    }

    void clearRuntimeState()
    {
        for (int lane = 0; lane < wintoid::polyphony::MAX_CHANNELS; ++lane)
            resetLane(lane);
        overDetector.reset();
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
        return wintoid::finite_or(
            input.getVoltage(
                wintoid::polyphony::broadcast_lane(lane, channels)),
            0.f);
    }

    void prepareLanes(int channels)
    {
        wintoid::polyphony::reset_changed_lanes(
            previousChannels, channels,
            [&](int lane) { resetLane(lane); });
        previousChannels = channels;
    }

    FourV2()
    {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        configParam(ALGORITHM_PARAM, 1.f, 16.f, 1.f, "Algorithm");
        getParamQuantity(ALGORITHM_PARAM)->snapEnabled = true;
        configParam(TUNE_PARAM, -100.f, 100.f, 0.f, "Tune", " cents");
        configParam(PM_DEPTH_PARAM, 0.f, 1.f, 1.f, "PM Depth", "%", 0.f, 100.f);
        configParam(PM_DEPTH_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "PM Depth CV Attenuverter", "%", 0.f, 100.f);
        configParam(MASTER_PARAM, 0.f, 1.f, 1.f, "Master", "%", 0.f, 100.f);
        configParam(EXT_PM_ATTEN_PARAM, 0.f, 1.f, 0.f, "External PM Attenuator", "%", 0.f, 100.f);

        const int coarse_ids[] = {
            OP1_COARSE_PARAM, OP2_COARSE_PARAM,
            OP3_COARSE_PARAM, OP4_COARSE_PARAM
        };
        const int fine_ids[] = {
            OP1_FINE_PARAM, OP2_FINE_PARAM,
            OP3_FINE_PARAM, OP4_FINE_PARAM
        };
        const int output_ids[] = {
            OP1_OUTPUT_PARAM, OP2_OUTPUT_PARAM,
            OP3_OUTPUT_PARAM, OP4_OUTPUT_PARAM
        };
        const int warp_ids[] = {
            OP1_WARP_PARAM, OP2_WARP_PARAM,
            OP3_WARP_PARAM, OP4_WARP_PARAM
        };
        const int fold_ids[] = {
            OP1_FOLD_PARAM, OP2_FOLD_PARAM,
            OP3_FOLD_PARAM, OP4_FOLD_PARAM
        };
        const int feedback_ids[] = {
            OP1_FEEDBACK_PARAM, OP2_FEEDBACK_PARAM,
            OP3_FEEDBACK_PARAM, OP4_FEEDBACK_PARAM
        };
        const int freq_mode_ids[] = {
            OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM,
            OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM
        };
        const int fold_type_ids[] = {
            OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM,
            OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM
        };
        const int output_cv_atten_ids[] = {
            OP1_OUTPUT_CV_ATTEN_PARAM, OP2_OUTPUT_CV_ATTEN_PARAM,
            OP3_OUTPUT_CV_ATTEN_PARAM, OP4_OUTPUT_CV_ATTEN_PARAM
        };
        const int warp_cv_atten_ids[] = {
            OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM,
            OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM
        };
        const int fold_cv_atten_ids[] = {
            OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM,
            OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM
        };
        const int feedback_cv_atten_ids[] = {
            OP1_FEEDBACK_CV_ATTEN_PARAM, OP2_FEEDBACK_CV_ATTEN_PARAM,
            OP3_FEEDBACK_CV_ATTEN_PARAM, OP4_FEEDBACK_CV_ATTEN_PARAM
        };

        for (int op = 0; op < 4; ++op) {
            const std::string name = "Op " + std::to_string(op + 1);
            const float output_default = op == 0 ? 1.f : 0.f;

            // The typed quantity converts display text in both directions;
            // the parameter range, default, and snapping remain ordinary
            // Rack metadata.
            auto* coarse_quantity = configParam<CoarseParamQuantity>(
                coarse_ids[op], 0.f, 14.f, 5.f,
                name + " Coarse");
            coarse_quantity->freqModeParamId = freq_mode_ids[op];
            coarse_quantity->fineParamId = fine_ids[op];
            configParam(fine_ids[op], -100.f, 100.f, 0.f,
                        name + " Fine", " cents");
            configParam(output_ids[op], 0.f, 1.f, output_default,
                        name + " Output", "%", 0.f, 100.f);
            configParam(warp_ids[op], 0.f, 1.f, 0.f,
                        name + " Warp", "%", 0.f, 100.f);
            configParam(fold_ids[op], 0.f, 1.f, 0.f,
                        name + " Fold", "%", 0.f, 100.f);
            configParam(feedback_ids[op], 0.f, 1.f, 0.f,
                        name + " Feedback", "%", 0.f, 100.f);

            configSwitch(freq_mode_ids[op], 0.f, 1.f, 0.f,
                         name + " Frequency Mode", {"Ratio", "Fixed"});
            getParamQuantity(freq_mode_ids[op])->snapEnabled = true;
            configSwitch(fold_type_ids[op], 0.f, 2.f, 0.f,
                         name + " Fold Type",
                         {"Symmetric", "Asymmetric", "Soft Clip"});
            getParamQuantity(fold_type_ids[op])->snapEnabled = true;

            configParam(output_cv_atten_ids[op], -1.f, 1.f, 0.f,
                        name + " Output CV Attenuverter", "%", 0.f, 100.f);
            configParam(warp_cv_atten_ids[op], -1.f, 1.f, 0.f,
                        name + " Warp CV Attenuverter", "%", 0.f, 100.f);
            configParam(fold_cv_atten_ids[op], -1.f, 1.f, 0.f,
                        name + " Fold CV Attenuverter", "%", 0.f, 100.f);
            configParam(feedback_cv_atten_ids[op], -1.f, 1.f, 0.f,
                        name + " Feedback CV Attenuverter", "%", 0.f, 100.f);
        }

        configInput(VOCT_INPUT,
                    "V/OCT (polyphonic voice count, 1 to 16 channels)");
        configInput(PM_DEPTH_CV_INPUT, "PM Depth CV");
        configInput(EXT_PM_INPUT, "External PM");

        const int output_cv_input_ids[] = {
            OP1_OUTPUT_CV_INPUT, OP2_OUTPUT_CV_INPUT,
            OP3_OUTPUT_CV_INPUT, OP4_OUTPUT_CV_INPUT
        };
        const int warp_cv_input_ids[] = {
            OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT,
            OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT
        };
        const int fold_cv_input_ids[] = {
            OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT,
            OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT
        };
        const int feedback_cv_input_ids[] = {
            OP1_FEEDBACK_CV_INPUT, OP2_FEEDBACK_CV_INPUT,
            OP3_FEEDBACK_CV_INPUT, OP4_FEEDBACK_CV_INPUT
        };
        const int* cv_input_ids[] = {
            output_cv_input_ids, warp_cv_input_ids,
            fold_cv_input_ids, feedback_cv_input_ids
        };
        const char* cv_names[] = {"Output", "Warp", "Fold", "Feedback"};
        for (int row = 0; row < 4; ++row) {
            for (int op = 0; op < 4; ++op) {
                configInput(
                    cv_input_ids[row][op],
                    "Op " + std::to_string(op + 1) + " " + cv_names[row] + " CV");
            }
        }

        configOutput(MAIN_OUTPUT, "Main output");
        configLight(OVER_LIGHT, "OVER");
        clearRuntimeState();
    }

    void process(const ProcessArgs& args) override
    {
        if (args.sampleRate != previousSampleRate) {
            clearRuntimeState();
            previousSampleRate = args.sampleRate;
        }

        const int channels = wintoid::polyphony::effective_channels(
            inputs[VOCT_INPUT].getChannels());
        prepareLanes(channels);
        outputs[MAIN_OUTPUT].setChannels(channels);

        const int coarse_ids[] = {
            OP1_COARSE_PARAM, OP2_COARSE_PARAM,
            OP3_COARSE_PARAM, OP4_COARSE_PARAM
        };
        const int fine_ids[] = {
            OP1_FINE_PARAM, OP2_FINE_PARAM,
            OP3_FINE_PARAM, OP4_FINE_PARAM
        };
        const int output_ids[] = {
            OP1_OUTPUT_PARAM, OP2_OUTPUT_PARAM,
            OP3_OUTPUT_PARAM, OP4_OUTPUT_PARAM
        };
        const int warp_ids[] = {
            OP1_WARP_PARAM, OP2_WARP_PARAM,
            OP3_WARP_PARAM, OP4_WARP_PARAM
        };
        const int fold_ids[] = {
            OP1_FOLD_PARAM, OP2_FOLD_PARAM,
            OP3_FOLD_PARAM, OP4_FOLD_PARAM
        };
        const int feedback_ids[] = {
            OP1_FEEDBACK_PARAM, OP2_FEEDBACK_PARAM,
            OP3_FEEDBACK_PARAM, OP4_FEEDBACK_PARAM
        };
        const int freq_mode_ids[] = {
            OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM,
            OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM
        };
        const int fold_type_ids[] = {
            OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM,
            OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM
        };
        const int output_cv_input_ids[] = {
            OP1_OUTPUT_CV_INPUT, OP2_OUTPUT_CV_INPUT,
            OP3_OUTPUT_CV_INPUT, OP4_OUTPUT_CV_INPUT
        };
        const int warp_cv_input_ids[] = {
            OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT,
            OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT
        };
        const int fold_cv_input_ids[] = {
            OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT,
            OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT
        };
        const int feedback_cv_input_ids[] = {
            OP1_FEEDBACK_CV_INPUT, OP2_FEEDBACK_CV_INPUT,
            OP3_FEEDBACK_CV_INPUT, OP4_FEEDBACK_CV_INPUT
        };
        const int output_cv_atten_ids[] = {
            OP1_OUTPUT_CV_ATTEN_PARAM, OP2_OUTPUT_CV_ATTEN_PARAM,
            OP3_OUTPUT_CV_ATTEN_PARAM, OP4_OUTPUT_CV_ATTEN_PARAM
        };
        const int warp_cv_atten_ids[] = {
            OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM,
            OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM
        };
        const int fold_cv_atten_ids[] = {
            OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM,
            OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM
        };
        const int feedback_cv_atten_ids[] = {
            OP1_FEEDBACK_CV_ATTEN_PARAM, OP2_FEEDBACK_CV_ATTEN_PARAM,
            OP3_FEEDBACK_CV_ATTEN_PARAM, OP4_FEEDBACK_CV_ATTEN_PARAM
        };

        four_v2::EngineParams common;
        common.algorithm = four_v2::algorithm_index(
            params[ALGORITHM_PARAM].getValue());
        common.master = unit_param(
            params[MASTER_PARAM].getValue(), 1.f);

        const float global_tune = cents_multiplier(
            params[TUNE_PARAM].getValue());
        const float pm_depth = unit_param(
            params[PM_DEPTH_PARAM].getValue(), 1.f);
        const float pm_cv_atten = bipolar_param(
            params[PM_DEPTH_CV_ATTEN_PARAM].getValue());
        const float external_pm_atten = unit_param(
            params[EXT_PM_ATTEN_PARAM].getValue(), 0.f);

        for (int op = 0; op < 4; ++op) {
            common.opCoarse[op] = clamp(
                four_v2::finite_or(
                    params[coarse_ids[op]].getValue(),
                    (float)four_v2::DEFAULT_RATIO_INDEX),
                four_v2::COARSE_MIN, four_v2::COARSE_MAX);
            common.opFine[op] = cents_multiplier(
                params[fine_ids[op]].getValue());
            common.opFreqMode[op] = four_v2::clamp_mode(
                params[freq_mode_ids[op]].getValue());
            common.opFoldType[op] = fold_type_index(
                params[fold_type_ids[op]].getValue());
        }

        float peakVolts = 0.f;
        for (int lane = 0; lane < channels; ++lane) {
            four_v2::EngineParams ep = common;
            const float voct = four_v2::finite_or(
                readBroadcast(inputs[VOCT_INPUT], lane), 0.f);
            ep.baseFreq = four_v2::finite_or(
                four_v2::voct_to_freq(voct) * global_tune, 261.63f);

            const float pm_cv_voltage = four_v2::finite_or(
                readBroadcast(inputs[PM_DEPTH_CV_INPUT], lane), 0.f);
            ep.pmDepth = four_v2::normalize_modulated_unit_value(
                pm_depth, pm_cv_voltage, pm_cv_atten,
                inputs[PM_DEPTH_CV_INPUT].isConnected());

            const float external_pm_volts = four_v2::finite_or(
                readBroadcast(inputs[EXT_PM_INPUT], lane), 0.f);
            const float external_pm_cycles = four_v2::finite_or(
                external_pm_volts * external_pm_atten * 0.1f, 0.f);

            for (int op = 0; op < 4; ++op) {
                const float output_default = op == 0 ? 1.f : 0.f;
                const float output_knob = four_v2::finite_or(
                    params[output_ids[op]].getValue(), output_default);
                const float output_cv = four_v2::finite_or(readBroadcast(
                    inputs[output_cv_input_ids[op]], lane), 0.f);
                const float output_atten = bipolar_param(
                    params[output_cv_atten_ids[op]].getValue());
                ep.opOutput[op] = four_v2::normalize_modulated_unit_value(
                    output_knob, output_cv, output_atten,
                    inputs[output_cv_input_ids[op]].isConnected());

                const float warp_knob = four_v2::finite_or(
                    params[warp_ids[op]].getValue(), 0.f);
                const float warp_cv = four_v2::finite_or(readBroadcast(
                    inputs[warp_cv_input_ids[op]], lane), 0.f);
                const float warp_atten = bipolar_param(
                    params[warp_cv_atten_ids[op]].getValue());
                ep.opWarp[op] = four_v2::normalize_modulated_unit_value(
                    warp_knob, warp_cv, warp_atten,
                    inputs[warp_cv_input_ids[op]].isConnected());

                const float fold_knob = four_v2::finite_or(
                    params[fold_ids[op]].getValue(), 0.f);
                const float fold_cv = four_v2::finite_or(readBroadcast(
                    inputs[fold_cv_input_ids[op]], lane), 0.f);
                const float fold_atten = bipolar_param(
                    params[fold_cv_atten_ids[op]].getValue());
                ep.opFold[op] = four_v2::normalize_modulated_unit_value(
                    fold_knob, fold_cv, fold_atten,
                    inputs[fold_cv_input_ids[op]].isConnected());

                const float feedback_knob = four_v2::finite_or(
                    params[feedback_ids[op]].getValue(), 0.f);
                const float feedback_cv = four_v2::finite_or(readBroadcast(
                    inputs[feedback_cv_input_ids[op]], lane), 0.f);
                const float feedback_atten = bipolar_param(
                    params[feedback_cv_atten_ids[op]].getValue());
                ep.opFeedback[op] = four_v2::normalize_modulated_unit_value(
                    feedback_knob, feedback_cv, feedback_atten,
                    inputs[feedback_cv_input_ids[op]].isConnected());
            }

            const float out = four_v2::engine_process(
                engineStates[lane], ep, args.sampleTime, external_pm_cycles);
            const float volts = out * 5.f;
            outputs[MAIN_OUTPUT].setVoltage(volts, lane);
            peakVolts = fmaxf(peakVolts, fabsf(volts));
        }

        lights[OVER_LIGHT].setBrightness(
            overDetector.process(peakVolts, args.sampleTime) ? 1.f : 0.f);
    }
};

struct FourV2PanelLabels : Widget {
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

    FourV2PanelLabels()
    {
        using namespace four_v2_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    static void drawLabel(const DrawArgs& args, const Label& label)
    {
        nvgFontSize(args.vg, mm2px(label.size));
        nvgFillColor(args.vg, nvgRGB(label.red, label.green, label.blue));
        nvgTextAlign(args.vg, label.align);
        if (label.bold) {
            const float weightOffset = mm2px(0.10f);
            nvgText(args.vg, mm2px(label.x) - weightOffset,
                    mm2px(label.y), label.text, nullptr);
            nvgText(args.vg, mm2px(label.x) + weightOffset,
                    mm2px(label.y), label.text, nullptr);
        }
        nvgText(args.vg, mm2px(label.x), mm2px(label.y), label.text, nullptr);
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (!font) {
            Widget::drawLayer(args, layer);
            return;
        }
        nvgFontFaceId(args.vg, font->handle);

        using namespace four_v2_layout;
        const int leftBaseline = NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE;
        const int centerBaseline = NVG_ALIGN_CENTER | NVG_ALIGN_BASELINE;
        const int rightMiddle = NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE;

        const Label labels[] = {
            {TITLE_X, TITLE_Y, TITLE_FONT_SIZE, leftBaseline,
             36, 37, 34, "Four V2", true},
            {ALGORITHM_KNOB_X, ALGORITHM_LABEL_Y, GLOBAL_LABEL_SIZE, centerBaseline,
             36, 37, 34, "ALGO", false},
            {TUNE_KNOB_X, GLOBAL_LABEL_Y, GLOBAL_LABEL_SIZE, centerBaseline,
             36, 37, 34, "TUNE", false},
            {PM_DEPTH_KNOB_X, GLOBAL_LABEL_Y, GLOBAL_LABEL_SIZE, centerBaseline,
             36, 37, 34, "PM DEPTH", false},
            {MASTER_KNOB_X, GLOBAL_LABEL_Y, GLOBAL_LABEL_SIZE, centerBaseline,
             36, 37, 34, "MASTER", false},
            {EXTERNAL_PM_LABEL_X, EXTERNAL_PM_LABEL_Y, GLOBAL_LABEL_SIZE, rightMiddle,
             36, 37, 34, "EXT PM", false},
        };
        for (const Label& label : labels)
            drawLabel(args, label);

        const float operatorHeadingX[] = {
            OP1_SECTION_X + OPERATOR_HEADING_X_OFFSET,
            OP2_SECTION_X + OPERATOR_HEADING_X_OFFSET,
            OP3_SECTION_X + OPERATOR_HEADING_X_OFFSET,
            OP4_SECTION_X + OPERATOR_HEADING_X_OFFSET
        };
        const char* operatorHeadings[] = {"OP1", "OP2", "OP3", "OP4"};
        const float coarseX[] = {
            OP1_COARSE_X, OP2_COARSE_X, OP3_COARSE_X, OP4_COARSE_X
        };
        const float modeX[] = {
            OP1_FREQ_MODE_X, OP2_FREQ_MODE_X,
            OP3_FREQ_MODE_X, OP4_FREQ_MODE_X
        };
        const float fineX[] = {
            OP1_FINE_X, OP2_FINE_X, OP3_FINE_X, OP4_FINE_X
        };
        const float foldTypeX[] = {
            OP1_FOLD_TYPE_X, OP2_FOLD_TYPE_X,
            OP3_FOLD_TYPE_X, OP4_FOLD_TYPE_X
        };
        const float parameterX[4][4] = {
            {OP1_OUTPUT_X, OP2_OUTPUT_X, OP3_OUTPUT_X, OP4_OUTPUT_X},
            {OP1_WARP_X, OP2_WARP_X, OP3_WARP_X, OP4_WARP_X},
            {OP1_FOLD_X, OP2_FOLD_X, OP3_FOLD_X, OP4_FOLD_X},
            {OP1_FEEDBACK_X, OP2_FEEDBACK_X, OP3_FEEDBACK_X, OP4_FEEDBACK_X}
        };
        const float parameterLabelY[] = {
            OPERATOR_OUTPUT_LABEL_Y, OPERATOR_WARP_LABEL_Y,
            OPERATOR_FOLD_LABEL_Y, OPERATOR_FEEDBACK_LABEL_Y
        };
        const char* parameterLabels[] = {
            "LEVEL", "WARP", "FOLD", "FEEDBACK"
        };
        for (int op = 0; op < 4; ++op) {
            drawLabel(args, {operatorHeadingX[op], OPERATOR_HEADING_Y,
                              OPERATOR_HEADING_SIZE, leftBaseline,
                              36, 37, 34,
                              operatorHeadings[op], true});
            drawLabel(args, {coarseX[op], OPERATOR_COARSE_MODE_LABEL_Y,
                              OPERATOR_LABEL_SIZE,
                              centerBaseline, 36, 37, 34, "COARSE", false});
            drawLabel(args, {modeX[op], OPERATOR_COARSE_MODE_LABEL_Y,
                              OPERATOR_MODE_LABEL_SIZE,
                              centerBaseline, 36, 37, 34, "MODE", false});
            drawLabel(args, {fineX[op], OPERATOR_FINE_FOLD_TYPE_LABEL_Y,
                              OPERATOR_LABEL_SIZE,
                              centerBaseline, 36, 37, 34, "FINE", false});
            drawLabel(args, {foldTypeX[op], OPERATOR_FINE_FOLD_TYPE_LABEL_Y,
                              OPERATOR_MODE_LABEL_SIZE,
                              centerBaseline, 36, 37, 34, "FOLD TYPE", false});
            for (int row = 0; row < 4; ++row) {
                drawLabel(args, {parameterX[row][op], parameterLabelY[row],
                                  OPERATOR_LABEL_SIZE,
                                  centerBaseline, 36, 37, 34,
                                  parameterLabels[row], false});
            }
        }

        drawLabel(args, {VOCT_LABEL_X, SHARED_IO_LABEL_Y, GLOBAL_LABEL_SIZE, centerBaseline,
                         36, 37, 34, "V/OCT", false});
        drawLabel(args, {MAIN_OUTPUT_LABEL_X, MAIN_OUTPUT_LABEL_Y,
                         MAIN_OUTPUT_LABEL_SIZE, centerBaseline,
                         36, 37, 34, "MAIN OUT", false});

        Widget::drawLayer(args, layer);
    }
};

struct AlgorithmRoutingDisplay : Widget {
    FourV2* module = nullptr;

    AlgorithmRoutingDisplay()
    {
        using namespace four_v2_layout;
        box.size = mm2px(Vec(ROUTING_DISPLAY_WIDTH, ROUTING_DISPLAY_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return;

        using namespace four_v2_layout;
        const float strokeWidth = mm2px(0.35f);
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        nvgBeginPath(args.vg);
        nvgRoundedRect(
            args.vg, inset, inset,
            wintoid::ui::inset_extent(box.size.x, strokeWidth),
            wintoid::ui::inset_extent(box.size.y, strokeWidth),
            mm2px(1.0f));
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(85, 109, 128));
        nvgStrokeWidth(args.vg, strokeWidth);
        nvgStroke(args.vg);

        const float edgeStroke = mm2px(ROUTING_EDGE_STROKE_WIDTH);
        const float nodeStroke = mm2px(ROUTING_NODE_STROKE_WIDTH);
        const float nodeRadius = mm2px(ROUTING_NODE_RADIUS);

        const int algorithmIndex = four_v2::algorithm_index(
            module ? module->params[FourV2::ALGORITHM_PARAM].getValue() : 1.f);
        const four_v2::Algorithm& algorithm =
            four_v2::ALGORITHMS[algorithmIndex];
        const four_v2::RoutingLayout routing = four_v2::make_routing_layout(
            algorithm,
            ROUTING_DISPLAY_WIDTH,
            ROUTING_DISPLAY_HEIGHT,
            ROUTING_NODE_RADIUS,
            ROUTING_NODE_HORIZONTAL_MARGIN,
            ROUTING_NODE_VERTICAL_MARGIN);

        const float arrowLength = mm2px(ROUTING_ARROW_LENGTH);
        const float arrowWidth = mm2px(ROUTING_ARROW_WIDTH);
        const auto drawPath = [&](const four_v2::RoutingPath& path,
                                  NVGcolor color) {
            nvgBeginPath(args.vg);
            for (int pointIndex = 0;
                 pointIndex < path.pointCount; ++pointIndex) {
                const four_v2::RoutingPoint& point = path.points[pointIndex];
                const float x = mm2px(point.x);
                const float y = mm2px(point.y);
                if (pointIndex == 0)
                    nvgMoveTo(args.vg, x, y);
                else
                    nvgLineTo(args.vg, x, y);
            }
            nvgStrokeColor(args.vg, color);
            nvgStrokeWidth(args.vg, edgeStroke);
            nvgStroke(args.vg);

            if (!path.arrow)
                return;

            const four_v2::RoutingPoint& previous =
                path.points[path.pointCount - 2];
            const four_v2::RoutingPoint& tip =
                path.points[path.pointCount - 1];
            const float previousX = mm2px(previous.x);
            const float previousY = mm2px(previous.y);
            const float tipX = mm2px(tip.x);
            const float tipY = mm2px(tip.y);
            const float dx = tipX - previousX;
            const float dy = tipY - previousY;
            const float length = sqrtf(dx * dx + dy * dy);
            if (length <= 0.f)
                return;

            const float ux = dx / length;
            const float uy = dy / length;
            const float px = -uy;
            const float py = ux;
            const float baseX = tipX - ux * arrowLength;
            const float baseY = tipY - uy * arrowLength;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, tipX, tipY);
            nvgLineTo(args.vg,
                      baseX + px * arrowWidth,
                      baseY + py * arrowWidth);
            nvgLineTo(args.vg,
                      baseX - px * arrowWidth,
                      baseY - py * arrowWidth);
            nvgClosePath(args.vg);
            nvgFillColor(args.vg, color);
            nvgFill(args.vg);
        };

        // Orange paths are phase modulation and always point into their
        // destination. Gold paths are direct carriers and point to the right.
        const four_v2::RoutingPath* displayPaths = routing.displayPaths;
        for (int pathIndex = 0;
             pathIndex < routing.displayPathCount; ++pathIndex) {
            const four_v2::RoutingPath& path = displayPaths[pathIndex];
            if (!path.carrier)
                drawPath(path, nvgRGB(237, 91, 34));
        }
        for (int pathIndex = 0;
             pathIndex < routing.displayPathCount; ++pathIndex) {
            const four_v2::RoutingPath& path = displayPaths[pathIndex];
            if (path.carrier)
                drawPath(path, nvgRGB(224, 182, 73));
        }

        for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
            nvgBeginPath(args.vg);
            const float nodeX = mm2px(routing.nodes[op].x);
            const float nodeY = mm2px(routing.nodes[op].y);
            nvgCircle(args.vg, nodeX, nodeY, nodeRadius);
            nvgFillColor(args.vg, nvgRGB(36, 37, 34));
            nvgFill(args.vg);
            nvgStrokeColor(args.vg, algorithm.carrier[op]
                ? nvgRGB(224, 182, 73) : nvgRGB(237, 91, 34));
            nvgStrokeWidth(args.vg, nodeStroke);
            nvgStroke(args.vg);
        }

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, mm2px(ROUTING_NODE_LABEL_SIZE));
            nvgFillColor(args.vg, nvgRGB(236, 232, 217));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
                const char label[] = {static_cast<char>('1' + op), '\0'};
                nvgText(args.vg,
                        mm2px(routing.nodes[op].x),
                        mm2px(routing.nodes[op].y),
                        label, nullptr);
            }
        }

        Widget::drawLayer(args, layer);
    }
};

struct FourV2FrequencyControlGroups : Widget {
    FourV2FrequencyControlGroups()
    {
        using namespace four_v2_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        using namespace four_v2_layout;
        const float strokeWidth = mm2px(FREQUENCY_CONTROL_GROUP_STROKE_WIDTH);
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float groupX[] = {
            OP1_FREQUENCY_CONTROL_GROUP_X,
            OP2_FREQUENCY_CONTROL_GROUP_X,
            OP3_FREQUENCY_CONTROL_GROUP_X,
            OP4_FREQUENCY_CONTROL_GROUP_X
        };
        const float groupY[] = {
            OP1_FREQUENCY_CONTROL_GROUP_Y,
            OP2_FREQUENCY_CONTROL_GROUP_Y,
            OP3_FREQUENCY_CONTROL_GROUP_Y,
            OP4_FREQUENCY_CONTROL_GROUP_Y
        };
        const float groupWidth[] = {
            OP1_FREQUENCY_CONTROL_GROUP_WIDTH,
            OP2_FREQUENCY_CONTROL_GROUP_WIDTH,
            OP3_FREQUENCY_CONTROL_GROUP_WIDTH,
            OP4_FREQUENCY_CONTROL_GROUP_WIDTH
        };
        const float groupHeight[] = {
            OP1_FREQUENCY_CONTROL_GROUP_HEIGHT,
            OP2_FREQUENCY_CONTROL_GROUP_HEIGHT,
            OP3_FREQUENCY_CONTROL_GROUP_HEIGHT,
            OP4_FREQUENCY_CONTROL_GROUP_HEIGHT
        };

        nvgStrokeColor(args.vg, nvgRGB(85, 109, 128));
        nvgStrokeWidth(args.vg, strokeWidth);
        for (int op = 0; op < 4; ++op) {
            nvgBeginPath(args.vg);
            nvgRoundedRect(
                args.vg,
                mm2px(groupX[op]) + inset,
                mm2px(groupY[op]) + inset,
                wintoid::ui::inset_extent(mm2px(groupWidth[op]), strokeWidth),
                wintoid::ui::inset_extent(mm2px(groupHeight[op]), strokeWidth),
                mm2px(FREQUENCY_CONTROL_GROUP_RADIUS));
            nvgStroke(args.vg);
        }

        Widget::drawLayer(args, layer);
    }
};

struct OperatorFrequencyDisplay : Widget {
    FourV2* module = nullptr;
    int opIndex = 0;
    int coarseParamId = 0;
    int freqModeParamId = 0;
    int fineParamId = 0;

    OperatorFrequencyDisplay()
    {
        using namespace four_v2_layout;
        box.size = mm2px(Vec(FREQUENCY_DISPLAY_WIDTH,
                             FREQUENCY_DISPLAY_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        const float strokeWidth = mm2px(0.30f);
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        nvgBeginPath(args.vg);
        nvgRoundedRect(
            args.vg, inset, inset,
            wintoid::ui::inset_extent(box.size.x, strokeWidth),
            wintoid::ui::inset_extent(box.size.y, strokeWidth),
            mm2px(0.5f));
        nvgFillColor(args.vg, nvgRGB(36, 37, 34));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(85, 109, 128));
        nvgStrokeWidth(args.vg, strokeWidth);
        nvgStroke(args.vg);

        const int mode = four_v2::clamp_mode(
            module ? module->getParamQuantity(freqModeParamId)->getValue()
                   : (float)four_v2::RATIO_MODE);
        const float coarse = clamp(
            four_v2::finite_or(
                module ? module->getParamQuantity(coarseParamId)->getValue()
                       : (float)four_v2::DEFAULT_RATIO_INDEX,
                (float)four_v2::DEFAULT_RATIO_INDEX),
            four_v2::COARSE_MIN, four_v2::COARSE_MAX);
        const float fine = module
            ? module->getParamQuantity(fineParamId)->getValue()
            : 0.f;
        const std::string text = mode == four_v2::RATIO_MODE
            ? std::string(four_v2::ratio_label(coarse))
            : four_v2::frequency_label(coarse, mode, fine);

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, mm2px(
                four_v2_layout::FREQUENCY_DISPLAY_FONT_SIZE));
            nvgFillColor(args.vg, nvgRGB(236, 232, 217));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x * 0.5f, box.size.y * 0.5f,
                    text.c_str(), nullptr);
        }

        Widget::drawLayer(args, layer);
    }
};

struct FourV2Widget : ModuleWidget {
    FourV2Widget(FourV2* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FourV2.svg")));

        using namespace four_v2_layout;

        auto* routing = new AlgorithmRoutingDisplay();
        routing->module = module;
        routing->box.pos = mm2px(Vec(ROUTING_DISPLAY_X, ROUTING_DISPLAY_Y));
        addChild(routing);

        addChild(new FourV2FrequencyControlGroups());

        const int frequency_coarse_ids[] = {
            FourV2::OP1_COARSE_PARAM, FourV2::OP2_COARSE_PARAM,
            FourV2::OP3_COARSE_PARAM, FourV2::OP4_COARSE_PARAM
        };
        const int frequency_mode_ids[] = {
            FourV2::OP1_FREQ_MODE_PARAM, FourV2::OP2_FREQ_MODE_PARAM,
            FourV2::OP3_FREQ_MODE_PARAM, FourV2::OP4_FREQ_MODE_PARAM
        };
        const int frequency_fine_ids[] = {
            FourV2::OP1_FINE_PARAM, FourV2::OP2_FINE_PARAM,
            FourV2::OP3_FINE_PARAM, FourV2::OP4_FINE_PARAM
        };
        const float frequency_display_x[] = {
            OP1_FREQUENCY_DISPLAY_X, OP2_FREQUENCY_DISPLAY_X,
            OP3_FREQUENCY_DISPLAY_X, OP4_FREQUENCY_DISPLAY_X
        };
        const float frequency_display_y[] = {
            OP1_FREQUENCY_DISPLAY_Y, OP2_FREQUENCY_DISPLAY_Y,
            OP3_FREQUENCY_DISPLAY_Y, OP4_FREQUENCY_DISPLAY_Y
        };
        for (int op = 0; op < 4; ++op) {
            auto* display = new OperatorFrequencyDisplay();
            display->module = module;
            display->opIndex = op;
            display->coarseParamId = frequency_coarse_ids[op];
            display->freqModeParamId = frequency_mode_ids[op];
            display->fineParamId = frequency_fine_ids[op];
            display->box.pos = mm2px(Vec(
                frequency_display_x[op], frequency_display_y[op]));
            addChild(display);
        }

        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(ALGORITHM_KNOB_X, ALGORITHM_KNOB_Y)),
            module, FourV2::ALGORITHM_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(TUNE_KNOB_X, TUNE_KNOB_Y)),
            module, FourV2::TUNE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(PM_DEPTH_KNOB_X, PM_DEPTH_KNOB_Y)),
            module, FourV2::PM_DEPTH_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(
            mm2px(Vec(MASTER_KNOB_X, MASTER_KNOB_Y)),
            module, FourV2::MASTER_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(PM_DEPTH_CV_JACK_X, PM_DEPTH_CV_JACK_Y)),
            module, FourV2::PM_DEPTH_CV_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(PM_DEPTH_CV_ATTEN_X, PM_DEPTH_CV_ATTEN_Y)),
            module, FourV2::PM_DEPTH_CV_ATTEN_PARAM));
        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(EXTERNAL_PM_JACK_X, EXTERNAL_PM_JACK_Y)),
            module, FourV2::EXT_PM_INPUT));
        addParam(createParamCentered<Trimpot>(
            mm2px(Vec(EXTERNAL_PM_ATTEN_X, EXTERNAL_PM_ATTEN_Y)),
            module, FourV2::EXT_PM_ATTEN_PARAM));

        const int coarse_ids[] = {
            FourV2::OP1_COARSE_PARAM, FourV2::OP2_COARSE_PARAM,
            FourV2::OP3_COARSE_PARAM, FourV2::OP4_COARSE_PARAM
        };
        const int fine_ids[] = {
            FourV2::OP1_FINE_PARAM, FourV2::OP2_FINE_PARAM,
            FourV2::OP3_FINE_PARAM, FourV2::OP4_FINE_PARAM
        };
        const int output_ids[] = {
            FourV2::OP1_OUTPUT_PARAM, FourV2::OP2_OUTPUT_PARAM,
            FourV2::OP3_OUTPUT_PARAM, FourV2::OP4_OUTPUT_PARAM
        };
        const int warp_ids[] = {
            FourV2::OP1_WARP_PARAM, FourV2::OP2_WARP_PARAM,
            FourV2::OP3_WARP_PARAM, FourV2::OP4_WARP_PARAM
        };
        const int fold_ids[] = {
            FourV2::OP1_FOLD_PARAM, FourV2::OP2_FOLD_PARAM,
            FourV2::OP3_FOLD_PARAM, FourV2::OP4_FOLD_PARAM
        };
        const int feedback_ids[] = {
            FourV2::OP1_FEEDBACK_PARAM, FourV2::OP2_FEEDBACK_PARAM,
            FourV2::OP3_FEEDBACK_PARAM, FourV2::OP4_FEEDBACK_PARAM
        };
        const int freq_mode_ids[] = {
            FourV2::OP1_FREQ_MODE_PARAM, FourV2::OP2_FREQ_MODE_PARAM,
            FourV2::OP3_FREQ_MODE_PARAM, FourV2::OP4_FREQ_MODE_PARAM
        };
        const int fold_type_ids[] = {
            FourV2::OP1_FOLD_TYPE_PARAM, FourV2::OP2_FOLD_TYPE_PARAM,
            FourV2::OP3_FOLD_TYPE_PARAM, FourV2::OP4_FOLD_TYPE_PARAM
        };

        const float coarse_x[] = {
            OP1_COARSE_X, OP2_COARSE_X, OP3_COARSE_X, OP4_COARSE_X
        };
        const float freq_mode_x[] = {
            OP1_FREQ_MODE_X, OP2_FREQ_MODE_X,
            OP3_FREQ_MODE_X, OP4_FREQ_MODE_X
        };
        const float fine_x[] = {
            OP1_FINE_X, OP2_FINE_X, OP3_FINE_X, OP4_FINE_X
        };
        const float output_x[] = {
            OP1_OUTPUT_X, OP2_OUTPUT_X, OP3_OUTPUT_X, OP4_OUTPUT_X
        };
        const float warp_x[] = {
            OP1_WARP_X, OP2_WARP_X, OP3_WARP_X, OP4_WARP_X
        };
        const float fold_x[] = {
            OP1_FOLD_X, OP2_FOLD_X, OP3_FOLD_X, OP4_FOLD_X
        };
        const float fold_type_x[] = {
            OP1_FOLD_TYPE_X, OP2_FOLD_TYPE_X,
            OP3_FOLD_TYPE_X, OP4_FOLD_TYPE_X
        };
        const float feedback_x[] = {
            OP1_FEEDBACK_X, OP2_FEEDBACK_X,
            OP3_FEEDBACK_X, OP4_FEEDBACK_X
        };
        const float operator_coarse_mode_y = OPERATOR_COARSE_MODE_Y;
        const float operator_fine_fold_type_y = OPERATOR_FINE_FOLD_TYPE_Y;
        const float operator_output_y = OPERATOR_OUTPUT_Y;
        const float operator_warp_y = OPERATOR_WARP_Y;
        const float operator_fold_y = OPERATOR_FOLD_Y;
        const float operator_feedback_y = OPERATOR_FEEDBACK_Y;

        for (int op = 0; op < 4; ++op) {
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(coarse_x[op], operator_coarse_mode_y)),
                module, coarse_ids[op]));
            addParam(createParamCentered<FourV2FrequencyModeSwitch>(
                mm2px(Vec(freq_mode_x[op], operator_coarse_mode_y)),
                module, freq_mode_ids[op]));
            addParam(createParamCentered<Trimpot>(
                mm2px(Vec(fine_x[op], operator_fine_fold_type_y)),
                module, fine_ids[op]));
            addParam(createParamCentered<FourV2FoldTypeSwitch>(
                mm2px(Vec(fold_type_x[op], operator_fine_fold_type_y)),
                module, fold_type_ids[op]));

            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(output_x[op], operator_output_y)),
                module, output_ids[op]));
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(warp_x[op], operator_warp_y)),
                module, warp_ids[op]));
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(fold_x[op], operator_fold_y)),
                module, fold_ids[op]));
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(feedback_x[op], operator_feedback_y)),
                module, feedback_ids[op]));
        }

        const int cv_input_ids[4][4] = {
            {FourV2::OP1_OUTPUT_CV_INPUT, FourV2::OP2_OUTPUT_CV_INPUT,
             FourV2::OP3_OUTPUT_CV_INPUT, FourV2::OP4_OUTPUT_CV_INPUT},
            {FourV2::OP1_WARP_CV_INPUT, FourV2::OP2_WARP_CV_INPUT,
             FourV2::OP3_WARP_CV_INPUT, FourV2::OP4_WARP_CV_INPUT},
            {FourV2::OP1_FOLD_CV_INPUT, FourV2::OP2_FOLD_CV_INPUT,
             FourV2::OP3_FOLD_CV_INPUT, FourV2::OP4_FOLD_CV_INPUT},
            {FourV2::OP1_FEEDBACK_CV_INPUT, FourV2::OP2_FEEDBACK_CV_INPUT,
             FourV2::OP3_FEEDBACK_CV_INPUT, FourV2::OP4_FEEDBACK_CV_INPUT}
        };
        const int cv_atten_ids[4][4] = {
            {FourV2::OP1_OUTPUT_CV_ATTEN_PARAM, FourV2::OP2_OUTPUT_CV_ATTEN_PARAM,
             FourV2::OP3_OUTPUT_CV_ATTEN_PARAM, FourV2::OP4_OUTPUT_CV_ATTEN_PARAM},
            {FourV2::OP1_WARP_CV_ATTEN_PARAM, FourV2::OP2_WARP_CV_ATTEN_PARAM,
             FourV2::OP3_WARP_CV_ATTEN_PARAM, FourV2::OP4_WARP_CV_ATTEN_PARAM},
            {FourV2::OP1_FOLD_CV_ATTEN_PARAM, FourV2::OP2_FOLD_CV_ATTEN_PARAM,
             FourV2::OP3_FOLD_CV_ATTEN_PARAM, FourV2::OP4_FOLD_CV_ATTEN_PARAM},
            {FourV2::OP1_FEEDBACK_CV_ATTEN_PARAM, FourV2::OP2_FEEDBACK_CV_ATTEN_PARAM,
             FourV2::OP3_FEEDBACK_CV_ATTEN_PARAM, FourV2::OP4_FEEDBACK_CV_ATTEN_PARAM}
        };
        const float cv_input_x[4][4] = {
            {OP1_OUTPUT_CV_INPUT_X, OP2_OUTPUT_CV_INPUT_X,
             OP3_OUTPUT_CV_INPUT_X, OP4_OUTPUT_CV_INPUT_X},
            {OP1_WARP_CV_INPUT_X, OP2_WARP_CV_INPUT_X,
             OP3_WARP_CV_INPUT_X, OP4_WARP_CV_INPUT_X},
            {OP1_FOLD_CV_INPUT_X, OP2_FOLD_CV_INPUT_X,
             OP3_FOLD_CV_INPUT_X, OP4_FOLD_CV_INPUT_X},
            {OP1_FEEDBACK_CV_INPUT_X, OP2_FEEDBACK_CV_INPUT_X,
             OP3_FEEDBACK_CV_INPUT_X, OP4_FEEDBACK_CV_INPUT_X}
        };
        const float cv_atten_x[4][4] = {
            {OP1_OUTPUT_CV_ATTEN_X, OP2_OUTPUT_CV_ATTEN_X,
             OP3_OUTPUT_CV_ATTEN_X, OP4_OUTPUT_CV_ATTEN_X},
            {OP1_WARP_CV_ATTEN_X, OP2_WARP_CV_ATTEN_X,
             OP3_WARP_CV_ATTEN_X, OP4_WARP_CV_ATTEN_X},
            {OP1_FOLD_CV_ATTEN_X, OP2_FOLD_CV_ATTEN_X,
             OP3_FOLD_CV_ATTEN_X, OP4_FOLD_CV_ATTEN_X},
            {OP1_FEEDBACK_CV_ATTEN_X, OP2_FEEDBACK_CV_ATTEN_X,
             OP3_FEEDBACK_CV_ATTEN_X, OP4_FEEDBACK_CV_ATTEN_X}
        };
        const float cv_y[] = {
            PATCHBAY_OUTPUT_Y, PATCHBAY_WARP_Y,
            PATCHBAY_FOLD_Y, PATCHBAY_FEEDBACK_Y
        };
        for (int row = 0; row < 4; ++row) {
            for (int op = 0; op < 4; ++op) {
                addInput(createInputCentered<PJ301MPort>(
                    mm2px(Vec(cv_input_x[row][op], cv_y[row])),
                    module, cv_input_ids[row][op]));
                addParam(createParamCentered<Trimpot>(
                    mm2px(Vec(cv_atten_x[row][op], cv_y[row])),
                    module, cv_atten_ids[row][op]));
            }
        }

        addInput(createInputCentered<PJ301MPort>(
            mm2px(Vec(VOCT_JACK_X, VOCT_JACK_Y)),
            module, FourV2::VOCT_INPUT));
        addOutput(createOutputCentered<PJ301MPort>(
            mm2px(Vec(MAIN_OUTPUT_X, MAIN_OUTPUT_Y)),
            module, FourV2::MAIN_OUTPUT));
        addChild(createLightCentered<MediumLight<RedLight>>(
            mm2px(Vec(OVER_LIGHT_X, OVER_LIGHT_Y)),
            module, FourV2::OVER_LIGHT));

        auto* labels = new FourV2PanelLabels();
        addChild(labels);
    }
};

Model* modelFourV2 = createModel<FourV2, FourV2Widget>("FourV2");
