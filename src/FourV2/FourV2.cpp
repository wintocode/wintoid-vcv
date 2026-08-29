#include "../plugin.hpp"
#include "../polyphony.h"
#include "engine.h"
#include "layout.h"

#include <cmath>
#include <string>

namespace {

inline float patch_value(float knob, float cv, float atten)
{
    return clamp(knob + cv * atten / 10.f, 0.f, 1.f);
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

            // The plain form documents the frozen range/default contract;
            // the typed form installs the display-only quantity below.
            // configParam(coarse_ids[op], 0.f, 14.f, 5.f, "Coarse");
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
        common.algorithm = static_cast<int>(
            params[ALGORITHM_PARAM].getValue());
        common.master = params[MASTER_PARAM].getValue();

        const float global_tune = exp2f(params[TUNE_PARAM].getValue() / 1200.f);
        const float pm_depth = params[PM_DEPTH_PARAM].getValue();
        const float pm_cv_atten = params[PM_DEPTH_CV_ATTEN_PARAM].getValue();
        const float external_pm_atten = params[EXT_PM_ATTEN_PARAM].getValue();

        for (int op = 0; op < 4; ++op) {
            common.opCoarse[op] = params[coarse_ids[op]].getValue();
            common.opFine[op] = exp2f(params[fine_ids[op]].getValue() / 1200.f);
            common.opFreqMode[op] = static_cast<int>(
                params[freq_mode_ids[op]].getValue());
            common.opFoldType[op] = static_cast<int>(
                params[fold_type_ids[op]].getValue());
        }

        float peakVolts = 0.f;
        for (int lane = 0; lane < channels; ++lane) {
            four_v2::EngineParams ep = common;
            ep.baseFreq = four_v2::voct_to_freq(
                readBroadcast(inputs[VOCT_INPUT], lane)) * global_tune;

            const float pm_cv = readBroadcast(inputs[PM_DEPTH_CV_INPUT], lane)
                * pm_cv_atten / 10.f;
            ep.pmDepth = clamp(pm_depth + pm_cv, 0.f, 1.f);

            const float external_pm_volts =
                readBroadcast(inputs[EXT_PM_INPUT], lane);
            const float external_pm_cycles =
                external_pm_volts * external_pm_atten * 0.1f;

            for (int op = 0; op < 4; ++op) {
                const float output_knob =
                    params[output_ids[op]].getValue();
                const float output_cv = readBroadcast(
                    inputs[output_cv_input_ids[op]], lane);
                const float output_atten =
                    params[output_cv_atten_ids[op]].getValue();
                ep.opOutput[op] = patch_value(
                    output_knob, output_cv, output_atten);

                const float warp_knob = params[warp_ids[op]].getValue();
                const float warp_cv = readBroadcast(
                    inputs[warp_cv_input_ids[op]], lane);
                const float warp_atten =
                    params[warp_cv_atten_ids[op]].getValue();
                ep.opWarp[op] = patch_value(
                    warp_knob, warp_cv, warp_atten);

                const float fold_knob = params[fold_ids[op]].getValue();
                const float fold_cv = readBroadcast(
                    inputs[fold_cv_input_ids[op]], lane);
                const float fold_atten =
                    params[fold_cv_atten_ids[op]].getValue();
                ep.opFold[op] = patch_value(
                    fold_knob, fold_cv, fold_atten);

                const float feedback_knob =
                    params[feedback_ids[op]].getValue();
                const float feedback_cv = readBroadcast(
                    inputs[feedback_cv_input_ids[op]], lane);
                const float feedback_atten =
                    params[feedback_cv_atten_ids[op]].getValue();
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

struct FourV2Widget : ModuleWidget {
    FourV2Widget(FourV2* module)
    {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FourV2.svg")));

        using namespace four_v2_layout;

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
    }
};

Model* modelFourV2 = createModel<FourV2, FourV2Widget>("FourV2");
