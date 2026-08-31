#include "../plugin.hpp"
#include "../Brink/dsp.h"
#include "layout.h"
#include "../ui_geometry.h"

#include <algorithm>
#include <cmath>

struct BrinkV2 : Module {
    enum ParamId {
        A_CENTER_PARAM, A_WIDTH_PARAM, A_CENTER_ATTEN_PARAM, A_WIDTH_ATTEN_PARAM,
        B_CENTER_PARAM, B_WIDTH_PARAM, B_CENTER_ATTEN_PARAM, B_WIDTH_ATTEN_PARAM,
        PARAMS_LEN
    };

    enum InputId {
        A_SIGNAL_INPUT, A_CENTER_CV_INPUT, A_WIDTH_CV_INPUT,
        B_SIGNAL_INPUT, B_CENTER_CV_INPUT, B_WIDTH_CV_INPUT,
        INPUTS_LEN
    };

    enum OutputId {
        A_INSIDE_OUTPUT, A_OUTSIDE_OUTPUT, A_POSITION_OUTPUT,
        A_LOW_UP_OUTPUT, A_HIGH_UP_OUTPUT, A_LOW_DOWN_OUTPUT, A_HIGH_DOWN_OUTPUT,
        B_INSIDE_OUTPUT, B_OUTSIDE_OUTPUT, B_POSITION_OUTPUT,
        B_LOW_UP_OUTPUT, B_HIGH_UP_OUTPUT, B_LOW_DOWN_OUTPUT, B_HIGH_DOWN_OUTPUT,
        AND_OUTPUT, OR_OUTPUT, XOR_OUTPUT, STATE_OUTPUT,
        OUTPUTS_LEN
    };

    enum LightId {
        A_INSIDE_LIGHT, A_OUTSIDE_LIGHT, A_LOW_UP_LIGHT, A_HIGH_UP_LIGHT,
        A_LOW_DOWN_LIGHT, A_HIGH_DOWN_LIGHT,
        B_INSIDE_LIGHT, B_OUTSIDE_LIGHT, B_LOW_UP_LIGHT, B_HIGH_UP_LIGHT,
        B_LOW_DOWN_LIGHT, B_HIGH_DOWN_LIGHT,
        AND_LIGHT, OR_LIGHT, XOR_LIGHT, STATE_LIGHT,
        LIGHTS_LEN
    };

    brink::WindowState windowStates[2][brink::MAX_CHANNELS];
    brink::LogicState logicStates[brink::MAX_CHANNELS];
    bool insideStates[2][brink::MAX_CHANNELS];
    brink::AtomicDisplayFrame displayFrames[2];
    brink::DisplayRateLimiter displayRateLimiter;
    int previousSignalChannels[2];
    int previousLogicChannels;
    float previousSampleRate;

    BrinkV2() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        const int centerParams[] = {A_CENTER_PARAM, B_CENTER_PARAM};
        const int widthParams[] = {A_WIDTH_PARAM, B_WIDTH_PARAM};
        const int centerAttenParams[] = {A_CENTER_ATTEN_PARAM, B_CENTER_ATTEN_PARAM};
        const int widthAttenParams[] = {A_WIDTH_ATTEN_PARAM, B_WIDTH_ATTEN_PARAM};

        configParam(centerParams[0], -5.f, 5.f, 0.f, "Channel A center", " V");
        configParam(widthParams[0], 0.f, 10.f, 5.f, "Channel A width", " V");
        configParam(centerAttenParams[0], -1.f, 1.f, 0.f,
                    "Channel A center CV attenuverter", "%", 0.f, 100.f);
        configParam(widthAttenParams[0], -1.f, 1.f, 0.f,
                    "Channel A width CV attenuverter", "%", 0.f, 100.f);
        configParam(centerParams[1], -5.f, 5.f, 0.f, "Channel B center", " V");
        configParam(widthParams[1], 0.f, 10.f, 5.f, "Channel B width", " V");
        configParam(centerAttenParams[1], -1.f, 1.f, 0.f,
                    "Channel B center CV attenuverter", "%", 0.f, 100.f);
        configParam(widthAttenParams[1], -1.f, 1.f, 0.f,
                    "Channel B width CV attenuverter", "%", 0.f, 100.f);

        configInput(A_SIGNAL_INPUT, "Channel A signal");
        configInput(A_CENTER_CV_INPUT, "Channel A center CV");
        configInput(A_WIDTH_CV_INPUT, "Channel A width CV");
        configInput(B_SIGNAL_INPUT, "Channel B signal");
        configInput(B_CENTER_CV_INPUT, "Channel B center CV");
        configInput(B_WIDTH_CV_INPUT, "Channel B width CV");

        const char* channelNames[] = {"A", "B"};
        const int channelOutputIds[2][brink::EVENT_COUNT + 3] = {
            {A_INSIDE_OUTPUT, A_OUTSIDE_OUTPUT, A_POSITION_OUTPUT,
             A_LOW_UP_OUTPUT, A_HIGH_UP_OUTPUT, A_LOW_DOWN_OUTPUT, A_HIGH_DOWN_OUTPUT},
            {B_INSIDE_OUTPUT, B_OUTSIDE_OUTPUT, B_POSITION_OUTPUT,
             B_LOW_UP_OUTPUT, B_HIGH_UP_OUTPUT, B_LOW_DOWN_OUTPUT, B_HIGH_DOWN_OUTPUT}
        };
        const char* outputDescriptions[] = {
            "inside gate", "outside gate", "position voltage",
            "low boundary upward crossing", "high boundary upward crossing",
            "low boundary downward crossing", "high boundary downward crossing"
        };
        for (int channel = 0; channel < 2; ++channel) {
            for (int output = 0; output < brink::EVENT_COUNT + 3; ++output) {
                configOutput(channelOutputIds[channel][output],
                             std::string("Channel ") + channelNames[channel] + " " + outputDescriptions[output]);
            }
        }
        configOutput(AND_OUTPUT, "A and B inside logical AND");
        configOutput(OR_OUTPUT, "A or B inside logical OR");
        configOutput(XOR_OUTPUT, "A and B inside exclusive OR");
        configOutput(STATE_OUTPUT,
                     "Toggle: flips on each A/B XOR rising edge");

        const int channelLightIds[2][brink::EVENT_COUNT + 2] = {
            {A_INSIDE_LIGHT, A_OUTSIDE_LIGHT, A_LOW_UP_LIGHT, A_HIGH_UP_LIGHT,
             A_LOW_DOWN_LIGHT, A_HIGH_DOWN_LIGHT},
            {B_INSIDE_LIGHT, B_OUTSIDE_LIGHT, B_LOW_UP_LIGHT, B_HIGH_UP_LIGHT,
             B_LOW_DOWN_LIGHT, B_HIGH_DOWN_LIGHT}
        };
        const char* lightDescriptions[] = {
            "inside activity", "outside activity",
            "low boundary upward crossing activity", "high boundary upward crossing activity",
            "low boundary downward crossing activity", "high boundary downward crossing activity"
        };
        for (int channel = 0; channel < 2; ++channel) {
            for (int light = 0; light < brink::EVENT_COUNT + 2; ++light) {
                configLight(channelLightIds[channel][light],
                            std::string("Channel ") + channelNames[channel] + " " + lightDescriptions[light]);
            }
        }
        configLight(AND_LIGHT, "AND activity");
        configLight(OR_LIGHT, "OR activity");
        configLight(XOR_LIGHT, "XOR activity");
        configLight(STATE_LIGHT, "Toggle activity");

        clearRuntimeState();
    }

    void clearRuntimeState() {
        for (int channel = 0; channel < 2; ++channel) {
            displayFrames[channel].store(brink::make_window(0.f, 0.f, 5.f));
            previousSignalChannels[channel] = 0;
            for (int lane = 0; lane < brink::MAX_CHANNELS; ++lane) {
                brink::reset(windowStates[channel][lane]);
                insideStates[channel][lane] = false;
            }
        }
        for (int lane = 0; lane < brink::MAX_CHANNELS; ++lane)
            brink::reset(logicStates[lane]);
        previousLogicChannels = 0;
        previousSampleRate = 0.f;
        displayRateLimiter.reset(0.f);
    }

    void onReset() override {
        clearRuntimeState();
    }

    static float readBroadcast(const Input& input, int lane)
    {
        Input& mutableInput = const_cast<Input&>(input);
        int channels = mutableInput.getChannels();
        if (channels <= 0) return 0.f;
        int sourceLane = brink::broadcast_lane(lane, channels);
        return mutableInput.getVoltage(sourceLane);
    }

    static void resetWindowLane(brink::WindowState& state, bool& inside)
    {
        brink::reset(state);
        inside = false;
    }

    void prepareWindowLanes(int channel, int channels) {
        int previous = previousSignalChannels[channel];
        if (channels < previous) {
            for (int lane = channels; lane < previous; ++lane)
                resetWindowLane(windowStates[channel][lane], insideStates[channel][lane]);
        }
        else if (channels > previous) {
            for (int lane = previous; lane < channels; ++lane)
                resetWindowLane(windowStates[channel][lane], insideStates[channel][lane]);
        }
        previousSignalChannels[channel] = channels;
    }

    void prepareLogicLanes(int channels) {
        int previous = previousLogicChannels;
        if (channels < previous) {
            for (int lane = channels; lane < previous; ++lane)
                brink::reset(logicStates[lane]);
        }
        else if (channels > previous) {
            for (int lane = previous; lane < channels; ++lane)
                brink::reset(logicStates[lane]);
        }
        previousLogicChannels = channels;
    }

    void process(const ProcessArgs& args) override {
        if (args.sampleRate != previousSampleRate) {
            clearRuntimeState();
            previousSampleRate = args.sampleRate;
            displayRateLimiter.reset(args.sampleRate);
        }

        const bool publishDisplay = displayRateLimiter.should_publish();

        const Input& aSignal = inputs[A_SIGNAL_INPUT];
        const Input& aCenterCv = inputs[A_CENTER_CV_INPUT];
        const Input& aWidthCv = inputs[A_WIDTH_CV_INPUT];
        const Input& bSignal = inputs[B_SIGNAL_INPUT].isConnected()
            ? inputs[B_SIGNAL_INPUT] : inputs[A_SIGNAL_INPUT];
        const Input& bCenterCv = inputs[B_CENTER_CV_INPUT].isConnected()
            ? inputs[B_CENTER_CV_INPUT] : inputs[A_CENTER_CV_INPUT];
        const Input& bWidthCv = inputs[B_WIDTH_CV_INPUT].isConnected()
            ? inputs[B_WIDTH_CV_INPUT] : inputs[A_WIDTH_CV_INPUT];

        const Input* signalPorts[] = {&aSignal, &bSignal};
        const Input* centerCvPorts[] = {&aCenterCv, &bCenterCv};
        const Input* widthCvPorts[] = {&aWidthCv, &bWidthCv};
        const int centerParams[] = {A_CENTER_PARAM, B_CENTER_PARAM};
        const int widthParams[] = {A_WIDTH_PARAM, B_WIDTH_PARAM};
        const int centerAttenParams[] = {A_CENTER_ATTEN_PARAM, B_CENTER_ATTEN_PARAM};
        const int widthAttenParams[] = {A_WIDTH_ATTEN_PARAM, B_WIDTH_ATTEN_PARAM};
        const int channelOutputIds[2][brink::EVENT_COUNT + 3] = {
            {A_INSIDE_OUTPUT, A_OUTSIDE_OUTPUT, A_POSITION_OUTPUT,
             A_LOW_UP_OUTPUT, A_HIGH_UP_OUTPUT, A_LOW_DOWN_OUTPUT, A_HIGH_DOWN_OUTPUT},
            {B_INSIDE_OUTPUT, B_OUTSIDE_OUTPUT, B_POSITION_OUTPUT,
             B_LOW_UP_OUTPUT, B_HIGH_UP_OUTPUT, B_LOW_DOWN_OUTPUT, B_HIGH_DOWN_OUTPUT}
        };
        const int channelLightIds[2][brink::EVENT_COUNT + 2] = {
            {A_INSIDE_LIGHT, A_OUTSIDE_LIGHT, A_LOW_UP_LIGHT, A_HIGH_UP_LIGHT,
             A_LOW_DOWN_LIGHT, A_HIGH_DOWN_LIGHT},
            {B_INSIDE_LIGHT, B_OUTSIDE_LIGHT, B_LOW_UP_LIGHT, B_HIGH_UP_LIGHT,
             B_LOW_DOWN_LIGHT, B_HIGH_DOWN_LIGHT}
        };

        int signalChannels[2];
        bool gateActivity[2][2] = {{false, false}, {false, false}};
        bool eventActivity[2][brink::EVENT_COUNT] = {};
        for (int channel = 0; channel < 2; ++channel) {
            Input& signalInput = const_cast<Input&>(*signalPorts[channel]);
            signalChannels[channel] = brink::effective_channels(signalInput.getChannels());
            prepareWindowLanes(channel, signalChannels[channel]);

            for (int output = 0; output < brink::EVENT_COUNT + 3; ++output)
                outputs[channelOutputIds[channel][output]].setChannels(signalChannels[channel]);

            for (int lane = 0; lane < signalChannels[channel]; ++lane) {
                float signal = readBroadcast(*signalPorts[channel], lane);
                float center = params[centerParams[channel]].getValue()
                    + readBroadcast(*centerCvPorts[channel], lane)
                    * params[centerAttenParams[channel]].getValue();
                float width = params[widthParams[channel]].getValue()
                    + readBroadcast(*widthCvPorts[channel], lane)
                    * params[widthAttenParams[channel]].getValue();
                brink::WindowOutput out = brink::process_window(
                    windowStates[channel][lane], signal, center, width, args.sampleTime);

                outputs[channelOutputIds[channel][0]].setVoltage(out.inside ? 10.f : 0.f, lane);
                outputs[channelOutputIds[channel][1]].setVoltage(out.inside ? 0.f : 10.f, lane);
                outputs[channelOutputIds[channel][2]].setVoltage(out.position, lane);
                for (int event = 0; event < brink::EVENT_COUNT; ++event) {
                    bool active = out.eventHigh[event];
                    outputs[channelOutputIds[channel][event + 3]].setVoltage(active ? 10.f : 0.f, lane);
                    eventActivity[channel][event] = eventActivity[channel][event] || active;
                }

                insideStates[channel][lane] = out.inside;
                gateActivity[channel][0] = gateActivity[channel][0] || out.inside;
                gateActivity[channel][1] = gateActivity[channel][1] || !out.inside;
                if (lane == 0 && publishDisplay)
                    displayFrames[channel].store(out.frame);
            }

            lights[channelLightIds[channel][0]].setBrightness(gateActivity[channel][0] ? 1.f : 0.f);
            lights[channelLightIds[channel][1]].setBrightness(gateActivity[channel][1] ? 1.f : 0.f);
            for (int event = 0; event < brink::EVENT_COUNT; ++event) {
                lights[channelLightIds[channel][event + 2]].setSmoothBrightness(
                    eventActivity[channel][event] ? 1.f : 0.f, args.sampleTime);
            }
        }

        int logicChannels = brink::logic_channels(
            const_cast<Input&>(aSignal).getChannels(),
            const_cast<Input&>(bSignal).getChannels());
        prepareLogicLanes(logicChannels);
        const int logicOutputIds[] = {AND_OUTPUT, OR_OUTPUT, XOR_OUTPUT, STATE_OUTPUT};
        for (int output = 0; output < 4; ++output)
            outputs[logicOutputIds[output]].setChannels(logicChannels);

        bool logicActivity[4] = {false, false, false, false};
        for (int lane = 0; lane < logicChannels; ++lane) {
            bool aInside = insideStates[0][brink::broadcast_lane(lane, signalChannels[0])];
            bool bInside = insideStates[1][brink::broadcast_lane(lane, signalChannels[1])];
            brink::LogicOutput out = brink::process_logic(logicStates[lane], aInside, bInside);
            const bool values[] = {out.andGate, out.orGate, out.xorGate, out.stateGate};
            for (int output = 0; output < 4; ++output) {
                outputs[logicOutputIds[output]].setVoltage(values[output] ? 10.f : 0.f, lane);
                logicActivity[output] = logicActivity[output] || values[output];
            }
        }
        lights[AND_LIGHT].setBrightness(logicActivity[0] ? 1.f : 0.f);
        lights[OR_LIGHT].setBrightness(logicActivity[1] ? 1.f : 0.f);
        lights[XOR_LIGHT].setBrightness(logicActivity[2] ? 1.f : 0.f);
        lights[STATE_LIGHT].setBrightness(logicActivity[3] ? 1.f : 0.f);
    }
};

struct BrinkV2ChannelALight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkV2ChannelALight() {
        addBaseColor(nvgRGB(brink_v2_layout::CHANNEL_A_ACCENT_R,
                            brink_v2_layout::CHANNEL_A_ACCENT_G,
                            brink_v2_layout::CHANNEL_A_ACCENT_B));
    }
};

struct BrinkV2ChannelBLight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkV2ChannelBLight() {
        addBaseColor(nvgRGB(brink_v2_layout::CHANNEL_B_ACCENT_R,
                            brink_v2_layout::CHANNEL_B_ACCENT_G,
                            brink_v2_layout::CHANNEL_B_ACCENT_B));
    }
};

struct BrinkV2LogicLight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkV2LogicLight() {
        addBaseColor(nvgRGB(brink_v2_layout::FUNCTION_ORANGE_R,
                            brink_v2_layout::FUNCTION_ORANGE_G,
                            brink_v2_layout::FUNCTION_ORANGE_B));
    }
};

struct BrinkV2Point {
    float x;
    float y;
};

struct BrinkV2ChannelLayout {
    BrinkV2Point centerKnob;
    BrinkV2Point widthKnob;
    BrinkV2Point signal;
    BrinkV2Point position;
    BrinkV2Point centerCv;
    BrinkV2Point centerAtten;
    BrinkV2Point widthCv;
    BrinkV2Point widthAtten;
    BrinkV2Point inside;
    BrinkV2Point outside;
    BrinkV2Point lowUp;
    BrinkV2Point highUp;
    BrinkV2Point lowDown;
    BrinkV2Point highDown;
    BrinkV2Point positionRail;
};

static const BrinkV2ChannelLayout brinkV2ChannelLayouts[2] = {
    {
        {brink_v2_layout::A_CENTER_KNOB_X, brink_v2_layout::A_CENTER_KNOB_Y},
        {brink_v2_layout::A_WIDTH_KNOB_X, brink_v2_layout::A_WIDTH_KNOB_Y},
        {brink_v2_layout::A_SIGNAL_X, brink_v2_layout::A_SIGNAL_Y},
        {brink_v2_layout::A_POSITION_X, brink_v2_layout::A_POSITION_Y},
        {brink_v2_layout::A_CENTER_CV_X, brink_v2_layout::A_CENTER_CV_Y},
        {brink_v2_layout::A_CENTER_ATTEN_X, brink_v2_layout::A_CENTER_ATTEN_Y},
        {brink_v2_layout::A_WIDTH_CV_X, brink_v2_layout::A_WIDTH_CV_Y},
        {brink_v2_layout::A_WIDTH_ATTEN_X, brink_v2_layout::A_WIDTH_ATTEN_Y},
        {brink_v2_layout::A_INSIDE_X, brink_v2_layout::A_INSIDE_Y},
        {brink_v2_layout::A_OUTSIDE_X, brink_v2_layout::A_OUTSIDE_Y},
        {brink_v2_layout::A_LOW_UP_X, brink_v2_layout::A_LOW_UP_Y},
        {brink_v2_layout::A_HIGH_UP_X, brink_v2_layout::A_HIGH_UP_Y},
        {brink_v2_layout::A_LOW_DOWN_X, brink_v2_layout::A_LOW_DOWN_Y},
        {brink_v2_layout::A_HIGH_DOWN_X, brink_v2_layout::A_HIGH_DOWN_Y},
        {brink_v2_layout::A_POSITION_RAIL_X, brink_v2_layout::A_POSITION_RAIL_Y}
    },
    {
        {brink_v2_layout::B_CENTER_KNOB_X, brink_v2_layout::B_CENTER_KNOB_Y},
        {brink_v2_layout::B_WIDTH_KNOB_X, brink_v2_layout::B_WIDTH_KNOB_Y},
        {brink_v2_layout::B_SIGNAL_X, brink_v2_layout::B_SIGNAL_Y},
        {brink_v2_layout::B_POSITION_X, brink_v2_layout::B_POSITION_Y},
        {brink_v2_layout::B_CENTER_CV_X, brink_v2_layout::B_CENTER_CV_Y},
        {brink_v2_layout::B_CENTER_ATTEN_X, brink_v2_layout::B_CENTER_ATTEN_Y},
        {brink_v2_layout::B_WIDTH_CV_X, brink_v2_layout::B_WIDTH_CV_Y},
        {brink_v2_layout::B_WIDTH_ATTEN_X, brink_v2_layout::B_WIDTH_ATTEN_Y},
        {brink_v2_layout::B_INSIDE_X, brink_v2_layout::B_INSIDE_Y},
        {brink_v2_layout::B_OUTSIDE_X, brink_v2_layout::B_OUTSIDE_Y},
        {brink_v2_layout::B_LOW_UP_X, brink_v2_layout::B_LOW_UP_Y},
        {brink_v2_layout::B_HIGH_UP_X, brink_v2_layout::B_HIGH_UP_Y},
        {brink_v2_layout::B_LOW_DOWN_X, brink_v2_layout::B_LOW_DOWN_Y},
        {brink_v2_layout::B_HIGH_DOWN_X, brink_v2_layout::B_HIGH_DOWN_Y},
        {brink_v2_layout::B_POSITION_RAIL_X, brink_v2_layout::B_POSITION_RAIL_Y}
    }
};

static const BrinkV2Point brinkV2LogicLayout[4] = {
    {brink_v2_layout::AND_OUTPUT_X, brink_v2_layout::AND_OUTPUT_Y},
    {brink_v2_layout::OR_OUTPUT_X, brink_v2_layout::OR_OUTPUT_Y},
    {brink_v2_layout::XOR_OUTPUT_X, brink_v2_layout::XOR_OUTPUT_Y},
    {brink_v2_layout::STATE_OUTPUT_X, brink_v2_layout::STATE_OUTPUT_Y}
};

static const BrinkV2Point
brinkV2ChannelLightLayouts[2][brink::EVENT_COUNT + 2] = {
    {
        {brink_v2_layout::A_INSIDE_LIGHT_X, brink_v2_layout::A_INSIDE_LIGHT_Y},
        {brink_v2_layout::A_OUTSIDE_LIGHT_X, brink_v2_layout::A_OUTSIDE_LIGHT_Y},
        {brink_v2_layout::A_LOW_UP_LIGHT_X, brink_v2_layout::A_LOW_UP_LIGHT_Y},
        {brink_v2_layout::A_HIGH_UP_LIGHT_X, brink_v2_layout::A_HIGH_UP_LIGHT_Y},
        {brink_v2_layout::A_LOW_DOWN_LIGHT_X, brink_v2_layout::A_LOW_DOWN_LIGHT_Y},
        {brink_v2_layout::A_HIGH_DOWN_LIGHT_X, brink_v2_layout::A_HIGH_DOWN_LIGHT_Y}
    },
    {
        {brink_v2_layout::B_INSIDE_LIGHT_X, brink_v2_layout::B_INSIDE_LIGHT_Y},
        {brink_v2_layout::B_OUTSIDE_LIGHT_X, brink_v2_layout::B_OUTSIDE_LIGHT_Y},
        {brink_v2_layout::B_LOW_UP_LIGHT_X, brink_v2_layout::B_LOW_UP_LIGHT_Y},
        {brink_v2_layout::B_HIGH_UP_LIGHT_X, brink_v2_layout::B_HIGH_UP_LIGHT_Y},
        {brink_v2_layout::B_LOW_DOWN_LIGHT_X, brink_v2_layout::B_LOW_DOWN_LIGHT_Y},
        {brink_v2_layout::B_HIGH_DOWN_LIGHT_X, brink_v2_layout::B_HIGH_DOWN_LIGHT_Y}
    }
};

static const BrinkV2Point brinkV2LogicLightLayout[4] = {
    {brink_v2_layout::AND_LIGHT_X, brink_v2_layout::AND_LIGHT_Y},
    {brink_v2_layout::OR_LIGHT_X, brink_v2_layout::OR_LIGHT_Y},
    {brink_v2_layout::XOR_LIGHT_X, brink_v2_layout::XOR_LIGHT_Y},
    {brink_v2_layout::STATE_LIGHT_X, brink_v2_layout::STATE_LIGHT_Y}
};

struct BrinkV2WindowRail : Widget {
    BrinkV2* module = nullptr;
    int channel = 0;

    BrinkV2WindowRail() {
        const float railBoxWidth = brink_v2_layout::RAIL_WIDTH + 6.f;
        box.size = mm2px(Vec(railBoxWidth,
                             brink_v2_layout::POSITION_RAIL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        const float centreX = box.size.x / 2.f;
        const float trackWidth = mm2px(brink_v2_layout::RAIL_WIDTH);
        const float markerPathWidth = mm2px(
            brink_v2_layout::RAIL_WIDTH * 2.f);
        const float markerStrokeWidth = mm2px(0.5f);
        brink::WindowDisplayFrame frame = module
            ? module->displayFrames[channel].load()
            : brink::make_display_frame(0.f, 0.f, 5.f);
        const float signalY = box.size.y
            * (1.f - brink::normalize_display_voltage(frame.signal));
        const float centerY = box.size.y
            * (1.f - brink::normalize_display_voltage(frame.center));
        const float lowerY = box.size.y
            * (1.f - brink::normalize_display_voltage(frame.lower));
        const float upperY = box.size.y
            * (1.f - brink::normalize_display_voltage(frame.upper));
        const float markerY = wintoid::ui::clamp_stroke_center(
            signalY, box.size.y, markerStrokeWidth);
        const float bandTop = std::min(lowerY, upperY);
        const float bandBottom = std::max(lowerY, upperY);
        const int accentR = channel == 0
            ? brink_v2_layout::CHANNEL_A_ACCENT_R
            : brink_v2_layout::CHANNEL_B_ACCENT_R;
        const int accentG = channel == 0
            ? brink_v2_layout::CHANNEL_A_ACCENT_G
            : brink_v2_layout::CHANNEL_B_ACCENT_G;
        const int accentB = channel == 0
            ? brink_v2_layout::CHANNEL_A_ACCENT_B
            : brink_v2_layout::CHANNEL_B_ACCENT_B;
        const NVGcolor accent = nvgRGB(accentR, accentG, accentB);
        const NVGcolor bandColor = nvgRGBA(accentR, accentG, accentB, 54);
        const NVGcolor boundaryColor = nvgRGBA(accentR, accentG, accentB, 170);
        const NVGcolor markerColor = nvgRGB(
            brink_v2_layout::FUNCTION_ORANGE_R,
            brink_v2_layout::FUNCTION_ORANGE_G,
            brink_v2_layout::FUNCTION_ORANGE_B);

        const float trackStroke = mm2px(0.3f);
        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, centreX - trackWidth / 2.f,
                       wintoid::ui::stroke_inset(trackStroke),
                       trackWidth,
                       wintoid::ui::inset_extent(box.size.y, trackStroke),
                       mm2px(0.6f));
        nvgFillColor(args.vg, nvgRGB(236, 232, 217));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(36, 37, 34));
        nvgStrokeWidth(args.vg, trackStroke);
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, centreX - trackWidth / 2.f, bandTop,
                trackWidth, bandBottom - bandTop);
        nvgFillColor(args.vg, bandColor);
        nvgFill(args.vg);

        const float tickStroke = mm2px(0.25f);
        const float tickHalfWidth = mm2px(brink_v2_layout::RAIL_WIDTH);
        const float tickY[] = {0.f, box.size.y / 2.f, box.size.y};
        nvgStrokeColor(args.vg, nvgRGBA(36, 37, 34, 170));
        nvgStrokeWidth(args.vg, tickStroke);
        for (int tick = 0; tick < 3; ++tick) {
            const float y = wintoid::ui::clamp_stroke_center(
                tickY[tick], box.size.y, tickStroke);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, centreX - tickHalfWidth, y);
            nvgLineTo(args.vg, centreX + tickHalfWidth, y);
            nvgStroke(args.vg);
        }

        const float boundaryStroke = mm2px(0.3f);
        const float boundaryHalfWidth = mm2px(
            brink_v2_layout::RAIL_WIDTH * 0.75f);
        const float boundaryY[] = {lowerY, upperY};
        nvgStrokeColor(args.vg, boundaryColor);
        nvgStrokeWidth(args.vg, boundaryStroke);
        for (int boundary = 0; boundary < 2; ++boundary) {
            const float y = wintoid::ui::clamp_stroke_center(
                boundaryY[boundary], box.size.y, boundaryStroke);
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, centreX - boundaryHalfWidth, y);
            nvgLineTo(args.vg, centreX + boundaryHalfWidth, y);
            nvgStroke(args.vg);
        }

        const float centerStroke = mm2px(0.35f);
        const float centerYClamped = wintoid::ui::clamp_stroke_center(
            centerY, box.size.y, centerStroke);
        const float centerHalfWidth = mm2px(brink_v2_layout::RAIL_WIDTH);
        nvgStrokeColor(args.vg, accent);
        nvgStrokeWidth(args.vg, centerStroke);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, centreX - centerHalfWidth, centerYClamped);
        nvgLineTo(args.vg, centreX + centerHalfWidth, centerYClamped);
        nvgStroke(args.vg);

        nvgStrokeColor(args.vg, markerColor);
        nvgStrokeWidth(args.vg, markerStrokeWidth);
        nvgLineCap(args.vg, NVG_ROUND);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, centreX - markerPathWidth / 2.f, markerY);
        nvgLineTo(args.vg, centreX + markerPathWidth / 2.f, markerY);
        nvgStroke(args.vg);
        nvgRestore(args.vg);

        Widget::drawLayer(args, layer);
    }
};

struct BrinkV2PanelLabels : Widget {
    BrinkV2PanelLabels() {
        box.size = mm2px(Vec(brink_v2_layout::PANEL_WIDTH,
                             brink_v2_layout::PANEL_HEIGHT));
    }

    void drawLabel(const DrawArgs& args,
                   const brink_v2_layout::LabelSpec& label) const
    {
        const float strokeWidth = 0.f;
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float width = wintoid::ui::inset_extent(
            box.size.x, strokeWidth);
        const float height = wintoid::ui::inset_extent(
            box.size.y, strokeWidth);
        const float textX = wintoid::ui::clamp_stroke_center(
            mm2px(label.x) + inset, width, strokeWidth);
        const float textY = wintoid::ui::clamp_stroke_center(
            mm2px(label.y) + inset, height, strokeWidth);
        const int horizontalAlign =
            label.align == brink_v2_layout::LABEL_ALIGN_LEFT
                ? NVG_ALIGN_LEFT : NVG_ALIGN_CENTER;
        const int verticalAlign =
            label.vertical == brink_v2_layout::LABEL_VERTICAL_BASELINE
                ? NVG_ALIGN_BASELINE : NVG_ALIGN_MIDDLE;
        nvgFontSize(args.vg, mm2px(label.size));
        nvgFillColor(args.vg, nvgRGB(label.red, label.green, label.blue));
        nvgTextAlign(args.vg, horizontalAlign | verticalAlign);
        if (label.bold) {
            const float weightOffset = mm2px(0.10f);
            nvgText(args.vg, textX - weightOffset, textY, label.text, nullptr);
            nvgText(args.vg, textX + weightOffset, textY, label.text, nullptr);
        }
        nvgText(args.vg, textX, textY, label.text, nullptr);
    }

    void drawLine(const DrawArgs& args,
                  const brink_v2_layout::LineSpec& line) const
    {
        const float strokeWidth = mm2px(line.strokeWidth);
        const float width = wintoid::ui::inset_extent(
            box.size.x, strokeWidth);
        const float height = wintoid::ui::inset_extent(
            box.size.y, strokeWidth);
        const float inset = wintoid::ui::stroke_inset(strokeWidth);
        const float x1 = wintoid::ui::clamp_stroke_center(
            mm2px(line.x1) - inset, width, strokeWidth) + inset;
        const float y1 = wintoid::ui::clamp_stroke_center(
            mm2px(line.y1) - inset, height, strokeWidth) + inset;
        const float x2 = wintoid::ui::clamp_stroke_center(
            mm2px(line.x2) - inset, width, strokeWidth) + inset;
        const float y2 = wintoid::ui::clamp_stroke_center(
            mm2px(line.y2) - inset, height, strokeWidth) + inset;
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, x1, y1);
        nvgLineTo(args.vg, x2, y2);
        nvgStrokeColor(args.vg, nvgRGB(line.red, line.green, line.blue));
        nvgStrokeWidth(args.vg, strokeWidth);
        nvgStroke(args.vg);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
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
        for (int line = 0; line < brink_v2_layout::PANEL_LINE_COUNT; ++line)
            drawLine(args, brink_v2_layout::PANEL_LINES[line]);
        for (int label = 0; label < brink_v2_layout::PANEL_LABEL_COUNT; ++label)
            drawLabel(args, brink_v2_layout::PANEL_LABELS[label]);

        Widget::drawLayer(args, layer);
    }
};

struct BrinkV2Widget : ModuleWidget {
    BrinkV2Widget(BrinkV2* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/BrinkV2.svg")));

        addChild(new BrinkV2PanelLabels());

        for (int channel = 0; channel < 2; ++channel) {
            BrinkV2WindowRail* rail = new BrinkV2WindowRail();
            rail->module = module;
            rail->channel = channel;
            rail->box.pos = mm2px(Vec(
                brinkV2ChannelLayouts[channel].positionRail.x
                    - (brink_v2_layout::RAIL_WIDTH + 6.f) / 2.f,
                brinkV2ChannelLayouts[channel].positionRail.y
                    - brink_v2_layout::POSITION_RAIL_HEIGHT / 2.f));
            addChild(rail);
        }

        const int centerParams[] = {BrinkV2::A_CENTER_PARAM, BrinkV2::B_CENTER_PARAM};
        const int widthParams[] = {BrinkV2::A_WIDTH_PARAM, BrinkV2::B_WIDTH_PARAM};
        const int centerAttenParams[] = {BrinkV2::A_CENTER_ATTEN_PARAM, BrinkV2::B_CENTER_ATTEN_PARAM};
        const int widthAttenParams[] = {BrinkV2::A_WIDTH_ATTEN_PARAM, BrinkV2::B_WIDTH_ATTEN_PARAM};
        const int signalInputs[] = {BrinkV2::A_SIGNAL_INPUT, BrinkV2::B_SIGNAL_INPUT};
        const int centerCvInputs[] = {BrinkV2::A_CENTER_CV_INPUT, BrinkV2::B_CENTER_CV_INPUT};
        const int widthCvInputs[] = {BrinkV2::A_WIDTH_CV_INPUT, BrinkV2::B_WIDTH_CV_INPUT};
        const int channelOutputs[2][brink::EVENT_COUNT + 3] = {
            {BrinkV2::A_INSIDE_OUTPUT, BrinkV2::A_OUTSIDE_OUTPUT, BrinkV2::A_POSITION_OUTPUT,
             BrinkV2::A_LOW_UP_OUTPUT, BrinkV2::A_HIGH_UP_OUTPUT, BrinkV2::A_LOW_DOWN_OUTPUT, BrinkV2::A_HIGH_DOWN_OUTPUT},
            {BrinkV2::B_INSIDE_OUTPUT, BrinkV2::B_OUTSIDE_OUTPUT, BrinkV2::B_POSITION_OUTPUT,
             BrinkV2::B_LOW_UP_OUTPUT, BrinkV2::B_HIGH_UP_OUTPUT, BrinkV2::B_LOW_DOWN_OUTPUT, BrinkV2::B_HIGH_DOWN_OUTPUT}
        };

        for (int channel = 0; channel < 2; ++channel) {
            const BrinkV2ChannelLayout& layout = brinkV2ChannelLayouts[channel];
            const int paramIds[] = {centerParams[channel], widthParams[channel]};
            const BrinkV2Point knobPoints[] = {layout.centerKnob, layout.widthKnob};
            for (int knob = 0; knob < 2; ++knob) {
                addParam(createParamCentered<RoundSmallBlackKnob>(
                    mm2px(Vec(knobPoints[knob].x, knobPoints[knob].y)),
                    module, paramIds[knob]));
            }

            const int inputIds[] = {
                signalInputs[channel], centerCvInputs[channel], widthCvInputs[channel]
            };
            const BrinkV2Point inputPoints[] = {
                layout.signal, layout.centerCv, layout.widthCv
            };
            for (int input = 0; input < 3; ++input) {
                addInput(createInputCentered<PJ301MPort>(
                    mm2px(Vec(inputPoints[input].x, inputPoints[input].y)),
                    module, inputIds[input]));
            }

            const int attenIds[] = {centerAttenParams[channel], widthAttenParams[channel]};
            const BrinkV2Point attenPoints[] = {layout.centerAtten, layout.widthAtten};
            for (int atten = 0; atten < 2; ++atten) {
                addParam(createParamCentered<Trimpot>(
                    mm2px(Vec(attenPoints[atten].x, attenPoints[atten].y)),
                    module, attenIds[atten]));
            }

            const BrinkV2Point outputPoints[] = {
                layout.inside, layout.outside, layout.position,
                layout.lowUp, layout.highUp, layout.lowDown, layout.highDown
            };
            for (int output = 0; output < brink::EVENT_COUNT + 3; ++output) {
                addOutput(createOutputCentered<PJ301MPort>(
                    mm2px(Vec(outputPoints[output].x, outputPoints[output].y)),
                    module, channelOutputs[channel][output]));
            }
        }

        const int logicOutputs[] = {
            BrinkV2::AND_OUTPUT, BrinkV2::OR_OUTPUT,
            BrinkV2::XOR_OUTPUT, BrinkV2::STATE_OUTPUT
        };
        for (int logic = 0; logic < 4; ++logic) {
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(brinkV2LogicLayout[logic].x,
                          brinkV2LogicLayout[logic].y)),
                module, logicOutputs[logic]));
        }

        const int gateLights[2][2] = {
            {BrinkV2::A_INSIDE_LIGHT, BrinkV2::A_OUTSIDE_LIGHT},
            {BrinkV2::B_INSIDE_LIGHT, BrinkV2::B_OUTSIDE_LIGHT}
        };
        const int eventLights[2][brink::EVENT_COUNT] = {
            {BrinkV2::A_LOW_UP_LIGHT, BrinkV2::A_HIGH_UP_LIGHT,
             BrinkV2::A_LOW_DOWN_LIGHT, BrinkV2::A_HIGH_DOWN_LIGHT},
            {BrinkV2::B_LOW_UP_LIGHT, BrinkV2::B_HIGH_UP_LIGHT,
             BrinkV2::B_LOW_DOWN_LIGHT, BrinkV2::B_HIGH_DOWN_LIGHT}
        };
        for (int channel = 0; channel < 2; ++channel) {
            for (int gate = 0; gate < 2; ++gate) {
                const BrinkV2Point& lightPoint =
                    brinkV2ChannelLightLayouts[channel][gate];
                if (channel == 0) {
                    addChild(createLightCentered<BrinkV2ChannelALight>(
                        mm2px(Vec(lightPoint.x, lightPoint.y)), module,
                        gateLights[channel][gate]));
                }
                else {
                    addChild(createLightCentered<BrinkV2ChannelBLight>(
                        mm2px(Vec(lightPoint.x, lightPoint.y)), module,
                        gateLights[channel][gate]));
                }
            }

            for (int event = 0; event < brink::EVENT_COUNT; ++event) {
                const BrinkV2Point& lightPoint =
                    brinkV2ChannelLightLayouts[channel][event + 2];
                if (channel == 0) {
                    addChild(createLightCentered<BrinkV2ChannelALight>(
                        mm2px(Vec(lightPoint.x, lightPoint.y)), module,
                        eventLights[channel][event]));
                }
                else {
                    addChild(createLightCentered<BrinkV2ChannelBLight>(
                        mm2px(Vec(lightPoint.x, lightPoint.y)), module,
                        eventLights[channel][event]));
                }
            }
        }

        const int logicLights[] = {
            BrinkV2::AND_LIGHT, BrinkV2::OR_LIGHT,
            BrinkV2::XOR_LIGHT, BrinkV2::STATE_LIGHT
        };
        for (int logic = 0; logic < 4; ++logic) {
            const BrinkV2Point& lightPoint = brinkV2LogicLightLayout[logic];
            addChild(createLightCentered<BrinkV2LogicLight>(
                mm2px(Vec(lightPoint.x, lightPoint.y)),
                module, logicLights[logic]));
        }
    }
};

Model* modelBrinkV2 = createModel<BrinkV2, BrinkV2Widget>("BrinkV2");
