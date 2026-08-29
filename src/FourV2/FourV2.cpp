#include "../plugin.hpp"
#include "../polyphony.h"
#include "../ui_geometry.h"
#include "engine.h"
#include "layout.h"

#include <cmath>
#include <string>

namespace {

inline float patch_value(float knob, float cv, float atten)
{
    knob = four_v2::finite_or(knob, 0.f);
    cv = four_v2::finite_or(cv, 0.f);
    atten = four_v2::finite_or(atten, 0.f);
    return clamp(knob + cv * atten / 10.f, 0.f, 1.f);
}

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
    cents = four_v2::finite_or(cents, 0.f);
    return four_v2::finite_or(exp2f(cents / 1200.f), 1.f);
}

inline int fold_type_index(float value)
{
    value = four_v2::finite_or(value, 0.f);
    value = fmaxf(0.f, fminf(2.f, value));
    return static_cast<int>(floorf(value + 0.5f));
}

struct CoarseParamQuantity : ParamQuantity {
    int freqModeParamId = 0;

    std::string getDisplayValueString() override {
        int mode = four_v2::RATIO_MODE;
        if (module)
            mode = four_v2::clamp_mode(
                module->params[freqModeParamId].getValue());
        return four_v2::frequency_label(getValue(), mode);
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
        return input.getVoltage(
            wintoid::polyphony::broadcast_lane(lane, channels));
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

        configParam(ALGORITHM_PARAM, 0.f, 10.f, 0.f, "Algorithm");
        getParamQuantity(ALGORITHM_PARAM)->snapEnabled = true;
        configParam(TUNE_PARAM, -100.f, 100.f, 0.f, "Tune", " cents");
        configParam(PM_DEPTH_PARAM, 0.f, 1.f, 1.f, "PM Depth", "%", 0.f, 100.f);
        configParam(PM_DEPTH_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "PM Depth CV Attenuverter", "%", 0.f, 100.f);
        configParam(MASTER_PARAM, 0.f, 1.f, 1.f, "Master", "%", 0.f, 100.f);
        configParam(EXT_PM_ATTEN_PARAM, -1.f, 1.f, 0.f, "External PM Attenuverter", "%", 0.f, 100.f);

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

            // The typed quantity changes display text only; the parameter
            // range, default, and snapping remain ordinary Rack metadata.
            auto* coarse_quantity = configParam<CoarseParamQuantity>(
                coarse_ids[op], 0.f, 14.f, 5.f,
                name + " Coarse");
            coarse_quantity->freqModeParamId = freq_mode_ids[op];
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
        const float external_pm_atten = bipolar_param(
            params[EXT_PM_ATTEN_PARAM].getValue());

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
            const float pm_cv = four_v2::finite_or(
                pm_cv_voltage * pm_cv_atten / 10.f, 0.f);
            ep.pmDepth = clamp(pm_depth + pm_cv, 0.f, 1.f);

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
                ep.opOutput[op] = patch_value(
                    output_knob, output_cv, output_atten);

                const float warp_knob = four_v2::finite_or(
                    params[warp_ids[op]].getValue(), 0.f);
                const float warp_cv = four_v2::finite_or(readBroadcast(
                    inputs[warp_cv_input_ids[op]], lane), 0.f);
                const float warp_atten = bipolar_param(
                    params[warp_cv_atten_ids[op]].getValue());
                ep.opWarp[op] = patch_value(
                    warp_knob, warp_cv, warp_atten);

                const float fold_knob = four_v2::finite_or(
                    params[fold_ids[op]].getValue(), 0.f);
                const float fold_cv = four_v2::finite_or(readBroadcast(
                    inputs[fold_cv_input_ids[op]], lane), 0.f);
                const float fold_atten = bipolar_param(
                    params[fold_cv_atten_ids[op]].getValue());
                ep.opFold[op] = patch_value(
                    fold_knob, fold_cv, fold_atten);

                const float feedback_knob = four_v2::finite_or(
                    params[feedback_ids[op]].getValue(), 0.f);
                const float feedback_cv = four_v2::finite_or(readBroadcast(
                    inputs[feedback_cv_input_ids[op]], lane), 0.f);
                const float feedback_atten = bipolar_param(
                    params[feedback_cv_atten_ids[op]].getValue());
                ep.opFeedback[op] = patch_value(
                    feedback_knob, feedback_cv, feedback_atten);
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
        const int rightBaseline = NVG_ALIGN_RIGHT | NVG_ALIGN_BASELINE;

        const Label labels[] = {
            {TITLE_X, TITLE_Y, TITLE_FONT_SIZE, leftBaseline,
             36, 37, 34, "FourV2"},
            {7.5f, 12.0f, 2.0f, leftBaseline,
             36, 37, 34, "ROUTING"},
            {ALGORITHM_KNOB_X, 14.2f, 1.75f, centerBaseline,
             36, 37, 34, "ALGORITHM"},
            {TUNE_KNOB_X, 13.9f, 1.55f, centerBaseline,
             36, 37, 34, "TUNE"},
            {PM_DEPTH_KNOB_X, 13.9f, 1.55f, centerBaseline,
             36, 37, 34, "PM DEPTH"},
            {MASTER_KNOB_X, 13.9f, 1.55f, centerBaseline,
             36, 37, 34, "MASTER"},
            {PM_DEPTH_CV_JACK_X, 13.9f, 1.55f, centerBaseline,
             36, 37, 34, "PM CV"},
            {PM_DEPTH_CV_ATTEN_X, 13.9f, 1.55f, centerBaseline,
             36, 37, 34, "PM CV"},
            {EXTERNAL_PM_JACK_X, 33.0f, 1.65f, centerBaseline,
             36, 37, 34, "EXT PM"},
            {EXTERNAL_PM_ATTEN_X, 33.0f, 1.55f, centerBaseline,
             36, 37, 34, "ATTEN"},
        };
        for (const Label& label : labels)
            drawLabel(args, label);

        const float operatorCenters[] = {
            OP1_CENTER_X, OP2_CENTER_X, OP3_CENTER_X, OP4_CENTER_X
        };
        const char* operatorHeadings[] = {"OP1", "OP2", "OP3", "OP4"};
        const char* firstRow[] = {"COARSE", "FINE"};
        const char* secondRow[] = {"OUTPUT", "WARP"};
        const char* thirdRow[] = {"FOLD", "TYPE"};
        for (int op = 0; op < 4; ++op) {
            drawLabel(args, {operatorCenters[op], 40.8f, 2.4f,
                              centerBaseline, 36, 37, 34,
                              operatorHeadings[op]});
            drawLabel(args, {operatorCenters[op] - 10.5f, 53.9f, 1.35f,
                              centerBaseline, 36, 37, 34, firstRow[0]});
            drawLabel(args, {operatorCenters[op] + 10.5f, 53.9f, 1.35f,
                              centerBaseline, 36, 37, 34, firstRow[1]});
            drawLabel(args, {operatorCenters[op] - 9.0f, 63.0f, 1.25f,
                              centerBaseline, 36, 37, 34, secondRow[0]});
            drawLabel(args, {operatorCenters[op] + 9.0f, 63.0f, 1.25f,
                              centerBaseline, 36, 37, 34, secondRow[1]});
            drawLabel(args, {operatorCenters[op] - 9.0f, 72.1f, 1.25f,
                              centerBaseline, 36, 37, 34, thirdRow[0]});
            drawLabel(args, {operatorCenters[op] + 9.0f, 72.1f, 1.25f,
                              centerBaseline, 36, 37, 34, thirdRow[1]});
            drawLabel(args, {operatorCenters[op], 81.2f, 1.25f,
                              centerBaseline, 36, 37, 34, "FEEDBACK"});
        }

        drawLabel(args, {7.5f, 86.5f, 2.0f, leftBaseline,
                         36, 37, 34, "CV PATCHBAY"});
        for (int op = 0; op < 4; ++op) {
            drawLabel(args, {operatorCenters[op], 86.5f, 1.55f,
                              centerBaseline, 85, 109, 128,
                              operatorHeadings[op]});
        }
        const char* patchbayRows[] = {"Output", "Warp", "Fold", "Feedback"};
        const float patchbayY[] = {
            PATCHBAY_OUTPUT_Y, PATCHBAY_WARP_Y,
            PATCHBAY_FOLD_Y, PATCHBAY_FEEDBACK_Y
        };
        for (int row = 0; row < 4; ++row) {
            const bool output = row == 0;
            drawLabel(args, {PATCHBAY_LABEL_RIGHT_X, patchbayY[row] + 0.8f,
                              1.8f, rightBaseline,
                              output ? 183 : 36,
                              output ? 105 : 37,
                              output ? 60 : 34,
                              patchbayRows[row]});
        }

        drawLabel(args, {VOCT_JACK_X, 124.0f, 1.55f, centerBaseline,
                         36, 37, 34, "V/OCT"});
        drawLabel(args, {MAIN_OUTPUT_X, 124.0f, 1.55f, centerBaseline,
                         36, 37, 34, "MAIN OUT"});
        drawLabel(args, {OVER_LIGHT_X, 124.0f, 1.55f, centerBaseline,
                         183, 105, 60, "OVER"});

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

        const float edgeStroke = mm2px(0.45f);
        const float nodeStroke = mm2px(0.35f);
        const float nodeRadius = mm2px(1.35f);
        const float nodeInset = nodeRadius + nodeStroke * 0.5f;
        const float left = inset + mm2px(2.4f) + nodeInset;
        const float right = box.size.x - inset - mm2px(2.4f) - nodeInset;
        const float spacing = (right - left) / 3.f;
        const float nodeX[] = {
            left, left + spacing, left + spacing * 2.f, right
        };
        const float nodeY = inset + mm2px(8.5f);
        const float railY = wintoid::ui::clamp_stroke_center(
            box.size.y - mm2px(3.4f), box.size.y, edgeStroke);
        const float arrowLength = mm2px(1.35f);
        const float arrowWidth = mm2px(0.75f);

        const int algorithmIndex = four_v2::algorithm_index(
            module ? module->params[FourV2::ALGORITHM_PARAM].getValue() : 0.f);
        const four_v2::Algorithm& algorithm =
            four_v2::ALGORITHMS[algorithmIndex];

        // Orange edges run from each modulator to its destination. Curves
        // separate the serial and fan-in routes while staying inside the
        // display's clipped drawing box.
        nvgStrokeColor(args.vg, nvgRGB(237, 91, 34));
        nvgStrokeWidth(args.vg, edgeStroke);
        for (int source = 0; source < four_v2::OPERATOR_COUNT; ++source) {
            for (int destination = 0;
                 destination < four_v2::OPERATOR_COUNT; ++destination) {
                if (!algorithm.mod[source][destination] || source == destination)
                    continue;

                const float direction = destination > source ? 1.f : -1.f;
                const float startX = nodeX[source]
                    + direction * (nodeRadius + edgeStroke);
                const float endX = nodeX[destination]
                    - direction * (nodeRadius + edgeStroke);
                const int span = source > destination
                    ? source - destination : destination - source;
                const float controlX = mm2px(2.4f);
                const float controlY = nodeY
                    - mm2px(2.2f + 1.3f * static_cast<float>(span));

                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, startX, nodeY);
                nvgBezierTo(
                    args.vg,
                    startX + direction * controlX, controlY,
                    endX - direction * controlX, controlY,
                    endX, nodeY);
                nvgStroke(args.vg);

                const float arrowBaseX = endX - direction * arrowLength;
                nvgBeginPath(args.vg);
                nvgMoveTo(args.vg, endX, nodeY);
                nvgLineTo(args.vg, arrowBaseX, nodeY - arrowWidth);
                nvgMoveTo(args.vg, endX, nodeY);
                nvgLineTo(args.vg, arrowBaseX, nodeY + arrowWidth);
                nvgStroke(args.vg);
            }
        }

        // Gold carrier paths drop separately into one shared output rail.
        nvgStrokeColor(args.vg, nvgRGB(224, 182, 73));
        nvgStrokeWidth(args.vg, edgeStroke);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, nodeX[0], railY);
        nvgLineTo(args.vg, nodeX[3], railY);
        nvgStroke(args.vg);
        for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
            if (!algorithm.carrier[op])
                continue;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, nodeX[op], nodeY + nodeRadius + edgeStroke);
            nvgLineTo(args.vg, nodeX[op], railY);
            nvgStroke(args.vg);
        }

        for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
            nvgBeginPath(args.vg);
            nvgCircle(args.vg, nodeX[op], nodeY, nodeRadius);
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
            nvgFontSize(args.vg, mm2px(1.8f));
            nvgFillColor(args.vg, nvgRGB(236, 232, 217));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            for (int op = 0; op < four_v2::OPERATOR_COUNT; ++op) {
                const char label[] = {static_cast<char>('1' + op), '\0'};
                nvgText(args.vg, nodeX[op], nodeY, label, nullptr);
            }
        }

        Widget::drawLayer(args, layer);
    }
};

struct OperatorFrequencyDisplay : Widget {
    FourV2* module = nullptr;
    int opIndex = 0;
    int coarseParamId = 0;
    int freqModeParamId = 0;

    OperatorFrequencyDisplay()
    {
        using namespace four_v2_layout;
        box.size = mm2px(Vec(FREQUENCY_DISPLAY_WIDTH,
                             FREQUENCY_DISPLAY_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override
    {
        if (layer != 1)
            return;

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
            module ? module->params[freqModeParamId].getValue()
                   : (float)four_v2::RATIO_MODE);
        const float coarse = clamp(
            four_v2::finite_or(
                module ? module->params[coarseParamId].getValue()
                       : (float)four_v2::DEFAULT_RATIO_INDEX,
                (float)four_v2::DEFAULT_RATIO_INDEX),
            four_v2::COARSE_MIN, four_v2::COARSE_MAX);
        const std::string text = mode == four_v2::RATIO_MODE
            ? std::string(four_v2::ratio_label(coarse))
            : four_v2::frequency_label(coarse, mode);

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if (font) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, mm2px(1.45f));
            nvgFillColor(args.vg, nvgRGB(85, 109, 128));
            nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_BASELINE);
            nvgText(args.vg, mm2px(1.5f), mm2px(2.7f), "FREQ", nullptr);

            nvgFontSize(args.vg, mm2px(1.8f));
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

        const int frequency_coarse_ids[] = {
            FourV2::OP1_COARSE_PARAM, FourV2::OP2_COARSE_PARAM,
            FourV2::OP3_COARSE_PARAM, FourV2::OP4_COARSE_PARAM
        };
        const int frequency_mode_ids[] = {
            FourV2::OP1_FREQ_MODE_PARAM, FourV2::OP2_FREQ_MODE_PARAM,
            FourV2::OP3_FREQ_MODE_PARAM, FourV2::OP4_FREQ_MODE_PARAM
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
        const float operator_frequency_y = OPERATOR_FREQUENCY_Y;
        const float operator_output_warp_y = OPERATOR_OUTPUT_WARP_Y;
        const float operator_fold_y = OPERATOR_FOLD_Y;
        const float operator_feedback_y = OPERATOR_FEEDBACK_Y;

        for (int op = 0; op < 4; ++op) {
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(coarse_x[op], operator_frequency_y)),
                module, coarse_ids[op]));
            addParam(createParamCentered<CKSS>(
                mm2px(Vec(freq_mode_x[op], operator_frequency_y)),
                module, freq_mode_ids[op]));
            addParam(createParamCentered<Trimpot>(
                mm2px(Vec(fine_x[op], operator_frequency_y)),
                module, fine_ids[op]));

            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(output_x[op], operator_output_warp_y)),
                module, output_ids[op]));
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(warp_x[op], operator_output_warp_y)),
                module, warp_ids[op]));
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(fold_x[op], operator_fold_y)),
                module, fold_ids[op]));
            addParam(createParamCentered<CKSSThree>(
                mm2px(Vec(fold_type_x[op], operator_fold_y)),
                module, fold_type_ids[op]));
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
