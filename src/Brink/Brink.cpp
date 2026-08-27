#include "../plugin.hpp"
#include "dsp.h"
#include "layout.h"

#include <algorithm>
#include <cmath>

struct Brink : Module {
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

    Brink() {
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
        // Rack's voltage accessors are read-only in practice but retain a
        // non-const signature in the supported SDK API.
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

struct BrinkTealLight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkTealLight() {
        addBaseColor(nvgRGB(45, 190, 180));
    }
};

struct BrinkOrangeLight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkOrangeLight() {
        addBaseColor(nvgRGB(238, 135, 54));
    }
};

struct BrinkVioletLight : SmallSimpleLight<GrayModuleLightWidget> {
    BrinkVioletLight() {
        addBaseColor(nvgRGB(172, 117, 230));
    }
};

struct BrinkPoint {
    float x;
    float y;
};

struct BrinkChannelLayout {
    BrinkPoint centerKnob;
    BrinkPoint widthKnob;
    BrinkPoint signal;
    BrinkPoint position;
    BrinkPoint centerCv;
    BrinkPoint centerAtten;
    BrinkPoint widthCv;
    BrinkPoint widthAtten;
    BrinkPoint inside;
    BrinkPoint outside;
    BrinkPoint lowUp;
    BrinkPoint highUp;
    BrinkPoint lowDown;
    BrinkPoint highDown;
    BrinkPoint positionRail;
};

static const BrinkChannelLayout brinkChannelLayouts[2] = {
    {
        {brink_layout::A_CENTER_KNOB_X, brink_layout::A_CENTER_KNOB_Y},
        {brink_layout::A_WIDTH_KNOB_X, brink_layout::A_WIDTH_KNOB_Y},
        {brink_layout::A_SIGNAL_X, brink_layout::A_SIGNAL_Y},
        {brink_layout::A_POSITION_X, brink_layout::A_POSITION_Y},
        {brink_layout::A_CENTER_CV_X, brink_layout::A_CENTER_CV_Y},
        {brink_layout::A_CENTER_ATTEN_X, brink_layout::A_CENTER_ATTEN_Y},
        {brink_layout::A_WIDTH_CV_X, brink_layout::A_WIDTH_CV_Y},
        {brink_layout::A_WIDTH_ATTEN_X, brink_layout::A_WIDTH_ATTEN_Y},
        {brink_layout::A_INSIDE_X, brink_layout::A_INSIDE_Y},
        {brink_layout::A_OUTSIDE_X, brink_layout::A_OUTSIDE_Y},
        {brink_layout::A_LOW_UP_X, brink_layout::A_LOW_UP_Y},
        {brink_layout::A_HIGH_UP_X, brink_layout::A_HIGH_UP_Y},
        {brink_layout::A_LOW_DOWN_X, brink_layout::A_LOW_DOWN_Y},
        {brink_layout::A_HIGH_DOWN_X, brink_layout::A_HIGH_DOWN_Y},
        {brink_layout::A_POSITION_RAIL_X, brink_layout::A_POSITION_RAIL_Y}
    },
    {
        {brink_layout::B_CENTER_KNOB_X, brink_layout::B_CENTER_KNOB_Y},
        {brink_layout::B_WIDTH_KNOB_X, brink_layout::B_WIDTH_KNOB_Y},
        {brink_layout::B_SIGNAL_X, brink_layout::B_SIGNAL_Y},
        {brink_layout::B_POSITION_X, brink_layout::B_POSITION_Y},
        {brink_layout::B_CENTER_CV_X, brink_layout::B_CENTER_CV_Y},
        {brink_layout::B_CENTER_ATTEN_X, brink_layout::B_CENTER_ATTEN_Y},
        {brink_layout::B_WIDTH_CV_X, brink_layout::B_WIDTH_CV_Y},
        {brink_layout::B_WIDTH_ATTEN_X, brink_layout::B_WIDTH_ATTEN_Y},
        {brink_layout::B_INSIDE_X, brink_layout::B_INSIDE_Y},
        {brink_layout::B_OUTSIDE_X, brink_layout::B_OUTSIDE_Y},
        {brink_layout::B_LOW_UP_X, brink_layout::B_LOW_UP_Y},
        {brink_layout::B_HIGH_UP_X, brink_layout::B_HIGH_UP_Y},
        {brink_layout::B_LOW_DOWN_X, brink_layout::B_LOW_DOWN_Y},
        {brink_layout::B_HIGH_DOWN_X, brink_layout::B_HIGH_DOWN_Y},
        {brink_layout::B_POSITION_RAIL_X, brink_layout::B_POSITION_RAIL_Y}
    }
};

static const BrinkPoint brinkLogicLayout[4] = {
    {brink_layout::AND_OUTPUT_X, brink_layout::AND_OUTPUT_Y},
    {brink_layout::OR_OUTPUT_X, brink_layout::OR_OUTPUT_Y},
    {brink_layout::XOR_OUTPUT_X, brink_layout::XOR_OUTPUT_Y},
    {brink_layout::STATE_OUTPUT_X, brink_layout::STATE_OUTPUT_Y}
};

struct WindowRail : Widget {
    Brink* module = nullptr;
    int channel = 0;

    WindowRail() {
        box.size = mm2px(Vec(8.f, brink_layout::POSITION_RAIL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if (layer != 1) {
            Widget::drawLayer(args, layer);
            return;
        }

        const float centreX = box.size.x / 2.f;
        const float trackWidth = mm2px(2.f);
        const float markerPathWidth = mm2px(
            brink_layout::SIGNAL_MARKER_PATH_WIDTH);
        const float markerStrokeWidth = mm2px(
            brink_layout::SIGNAL_MARKER_STROKE_WIDTH);
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
        const float markerY = std::max(markerStrokeWidth / 2.f,
            std::min(box.size.y - markerStrokeWidth / 2.f, signalY));
        const float bandTop = std::min(lowerY, upperY);
        const float bandBottom = std::max(lowerY, upperY);
        const int accentR = channel == 0
            ? brink_layout::CHANNEL_A_ACCENT_R
            : brink_layout::CHANNEL_B_ACCENT_R;
        const int accentG = channel == 0
            ? brink_layout::CHANNEL_A_ACCENT_G
            : brink_layout::CHANNEL_B_ACCENT_G;
        const int accentB = channel == 0
            ? brink_layout::CHANNEL_A_ACCENT_B
            : brink_layout::CHANNEL_B_ACCENT_B;
        const NVGcolor accent = nvgRGB(accentR, accentG, accentB);
        const NVGcolor bandColor = nvgRGBA(accentR, accentG, accentB, 75);
        const NVGcolor boundaryColor = nvgRGBA(
            accentR, accentG, accentB, 155);
        const NVGcolor markerColor = nvgRGB(
            brink_layout::SIGNAL_MARKER_R,
            brink_layout::SIGNAL_MARKER_G,
            brink_layout::SIGNAL_MARKER_B);

        nvgSave(args.vg);
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, centreX - trackWidth / 2.f, 0.f,
                       trackWidth, box.size.y, mm2px(0.8f));
        nvgFillColor(args.vg, nvgRGB(17, 21, 37));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(59, 70, 104));
        nvgStrokeWidth(args.vg, mm2px(0.3f));
        nvgStroke(args.vg);

        nvgBeginPath(args.vg);
        nvgRect(args.vg, centreX - trackWidth / 2.f, bandTop,
                trackWidth, bandBottom - bandTop);
        nvgFillColor(args.vg, bandColor);
        nvgFill(args.vg);

        nvgStrokeColor(args.vg, nvgRGBA(170, 180, 205, 190));
        nvgStrokeWidth(args.vg, mm2px(0.25f));
        const float tickHalfWidth = mm2px(2.f);
        const float tickY[] = {0.f, box.size.y / 2.f, box.size.y};
        for (int tick = 0; tick < 3; ++tick) {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, centreX - tickHalfWidth, tickY[tick]);
            nvgLineTo(args.vg, centreX + tickHalfWidth, tickY[tick]);
            nvgStroke(args.vg);
        }

        nvgStrokeColor(args.vg, boundaryColor);
        nvgStrokeWidth(args.vg, mm2px(0.3f));
        const float boundaryHalfWidth = mm2px(1.5f);
        const float boundaryY[] = {lowerY, upperY};
        for (int boundary = 0; boundary < 2; ++boundary) {
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, centreX - boundaryHalfWidth,
                      boundaryY[boundary]);
            nvgLineTo(args.vg, centreX + boundaryHalfWidth,
                      boundaryY[boundary]);
            nvgStroke(args.vg);
        }

        nvgStrokeColor(args.vg, accent);
        nvgStrokeWidth(args.vg, mm2px(0.35f));
        const float centerHalfWidth = mm2px(2.f);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, centreX - centerHalfWidth, centerY);
        nvgLineTo(args.vg, centreX + centerHalfWidth, centerY);
        nvgStroke(args.vg);

        nvgStrokeColor(args.vg, markerColor);
        nvgStrokeWidth(args.vg, markerStrokeWidth);
        nvgLineCap(args.vg, NVG_ROUND);
        nvgBeginPath(args.vg);
        nvgMoveTo(args.vg, centreX - markerPathWidth / 2.f, markerY);
        nvgLineTo(args.vg, centreX + markerPathWidth / 2.f, markerY);
        nvgStroke(args.vg);
        nvgRestore(args.vg);
    }
};

namespace {

static void drawDirectionArrow(NVGcontext* vg, float x, float y, bool up) {
    const float width = mm2px(1.2f);
    const float height = mm2px(1.5f);
    const float baseFactor = up ? brink_layout::ARROW_UP_BASE_FACTOR
                                : brink_layout::ARROW_DOWN_BASE_FACTOR;
    const float tipFactor = up ? brink_layout::ARROW_UP_TIP_FACTOR
                               : brink_layout::ARROW_DOWN_TIP_FACTOR;
    nvgBeginPath(vg);
    nvgMoveTo(vg, x - width, y + baseFactor * height);
    nvgLineTo(vg, x, y + tipFactor * height);
    nvgLineTo(vg, x + width, y + baseFactor * height);
    nvgStroke(vg);
}

struct PanelLabels : Widget {
    PanelLabels() {
        box.size = mm2px(Vec(brink_layout::PANEL_WIDTH, brink_layout::PANEL_HEIGHT));
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

        // Title.
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(220, 220, 220));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, mm2px(brink_layout::PANEL_WIDTH / 2.f),
                mm2px(brink_layout::TITLE_Y), "Brink", nullptr);

        // Channel headers and control labels.
        const char* channelNames[] = {"CHANNEL A", "CHANNEL B"};
        nvgFontSize(args.vg, 8);
        nvgFillColor(args.vg, nvgRGB(190, 198, 216));
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const float headerX = (layout.centerKnob.x + layout.widthKnob.x) / 2.f;
            nvgText(args.vg, mm2px(headerX), mm2px(layout.centerKnob.y - 9.f),
                    channelNames[channel], nullptr);
        }

        nvgFontSize(args.vg, 6.5f);
        nvgFillColor(args.vg, nvgRGB(172, 182, 201));
        const char* knobLabels[] = {"CENTER", "WIDTH"};
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const BrinkPoint knobPoints[] = {layout.centerKnob, layout.widthKnob};
            for (int knob = 0; knob < 2; ++knob) {
                nvgText(args.vg, mm2px(knobPoints[knob].x),
                        mm2px(knobPoints[knob].y - brink_layout::KNOB_LABEL_OFFSET),
                        knobLabels[knob], nullptr);
            }
        }

        const char* signalLabels[] = {"SIGNAL", "POSITION"};
        const char* cvLabels[] = {"CTR CV", "WID CV"};
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const BrinkPoint signalPoints[] = {layout.signal, layout.position};
            const BrinkPoint cvPoints[] = {layout.centerCv, layout.widthCv};
            for (int point = 0; point < 2; ++point) {
                nvgText(args.vg, mm2px(signalPoints[point].x),
                        mm2px(signalPoints[point].y - brink_layout::PORT_LABEL_OFFSET),
                        signalLabels[point], nullptr);
                nvgText(args.vg, mm2px(cvPoints[point].x),
                        mm2px(cvPoints[point].y - brink_layout::PORT_LABEL_OFFSET),
                        cvLabels[point], nullptr);
            }
        }

        const char* gateLabels[] = {"INSIDE", "OUTSIDE"};
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const BrinkPoint gatePoints[] = {layout.inside, layout.outside};
            for (int gate = 0; gate < 2; ++gate) {
                nvgText(args.vg, mm2px(gatePoints[gate].x),
                        mm2px(gatePoints[gate].y - brink_layout::PORT_LABEL_OFFSET),
                        gateLabels[gate], nullptr);
            }
        }

        const BrinkPoint BrinkChannelLayout::*eventPoints[] = {
            &BrinkChannelLayout::lowUp, &BrinkChannelLayout::highUp,
            &BrinkChannelLayout::lowDown, &BrinkChannelLayout::highDown
        };
        const char* eventLabels[] = {"LOW", "HIGH", "LOW", "HIGH"};
        const bool eventUp[] = {true, true, false, false};
        nvgFontSize(args.vg, 6.f);
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            for (int event = 0; event < 4; ++event) {
                const BrinkPoint& point = layout.*eventPoints[event];
                const float labelY = point.y - brink_layout::EVENT_LABEL_OFFSET;
                nvgText(args.vg, mm2px(point.x), mm2px(labelY),
                        eventLabels[event], nullptr);
                nvgStrokeColor(args.vg, nvgRGB(170, 180, 205));
                nvgStrokeWidth(args.vg, mm2px(0.25f));
                drawDirectionArrow(args.vg, mm2px(point.x + 4.f),
                                    mm2px(labelY), eventUp[event]);
            }
        }

        // Shared logic labels.
        nvgFontSize(args.vg, 7.f);
        const char* logicLabels[] = {"AND", "OR", "XOR", "TOGGLE"};
        nvgFillColor(args.vg, nvgRGB(190, 198, 216));
        for (int logic = 0; logic < 4; ++logic) {
            nvgText(args.vg, mm2px(brinkLogicLayout[logic].x),
                    mm2px(brinkLogicLayout[logic].y - 6.f), logicLabels[logic], nullptr);
        }

        // Subtle A-to-B normalisation marks.
        nvgStrokeColor(args.vg, nvgRGB(86, 98, 125));
        nvgStrokeWidth(args.vg, mm2px(0.3f));
        const BrinkChannelLayout& channelA = brinkChannelLayouts[0];
        const BrinkChannelLayout& channelB = brinkChannelLayouts[1];
        const BrinkPoint normalisationPoints[] = {
            channelA.signal, channelA.centerCv, channelA.widthCv
        };
        const BrinkPoint normalisationTargets[] = {
            channelB.signal, channelB.centerCv, channelB.widthCv
        };
        for (int mark = 0; mark < 3; ++mark) {
            const float y = normalisationPoints[mark].y;
            const float startX = normalisationPoints[mark].x + 2.f;
            const float endX = normalisationTargets[mark].x - 2.f;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, mm2px(startX), mm2px(y));
            nvgLineTo(args.vg, mm2px(endX), mm2px(normalisationTargets[mark].y));
            nvgStroke(args.vg);
            const float arrowX = (startX + endX) / 2.f + 1.5f;
            nvgBeginPath(args.vg);
            nvgMoveTo(args.vg, mm2px(arrowX - 1.2f), mm2px(y - 1.f));
            nvgLineTo(args.vg, mm2px(arrowX), mm2px(y));
            nvgLineTo(args.vg, mm2px(arrowX - 1.2f), mm2px(y + 1.f));
            nvgStroke(args.vg);
        }

        // The portfolio logo uses the exact measured two-colour treatment.
        nvgFontSize(args.vg, 10);
        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        nvgTextAlign(args.vg, NVG_ALIGN_LEFT | NVG_ALIGN_MIDDLE);
        float wintBounds[4];
        nvgTextBounds(args.vg, 0, 0, "wint", nullptr, wintBounds);
        float wintWidth = wintBounds[2] - wintBounds[0];
        float oidBounds[4];
        nvgTextBounds(args.vg, 0, 0, "oid", nullptr, oidBounds);
        float oidWidth = oidBounds[2] - oidBounds[0];
        float totalWidth = wintWidth + oidWidth;
        float logoX = mm2px(brink_layout::PANEL_WIDTH / 2.f) - totalWidth / 2.f;
        float logoY = mm2px(brink_layout::LOGO_BASELINE_Y);

        nvgFillColor(args.vg, nvgRGB(255, 255, 255));
        nvgText(args.vg, logoX, logoY, "wint", nullptr);
        nvgFillColor(args.vg, nvgRGB(255, 77, 0));
        nvgText(args.vg, logoX + wintWidth, logoY, "oid", nullptr);

        float lineY = mm2px(brink_layout::LOGO_UNDERLINE_Y);
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

        Widget::drawLayer(args, layer);
    }
};

} // anonymous namespace

struct BrinkWidget : ModuleWidget {
    BrinkWidget(Brink* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/Brink.svg")));

        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH,
                                               RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        PanelLabels* labels = new PanelLabels();
        addChild(labels);

        const float railY[] = {
            brink_layout::A_POSITION_RAIL_Y,
            brink_layout::B_POSITION_RAIL_Y
        };
        for (int channel = 0; channel < 2; ++channel) {
            WindowRail* rail = new WindowRail();
            rail->module = module;
            rail->channel = channel;
            rail->box.pos = mm2px(Vec(brinkChannelLayouts[channel].positionRail.x - 4.f,
                                      railY[channel]
                                      - brink_layout::POSITION_RAIL_HEIGHT / 2.f));
            addChild(rail);
        }

        const int centerParams[] = {Brink::A_CENTER_PARAM, Brink::B_CENTER_PARAM};
        const int widthParams[] = {Brink::A_WIDTH_PARAM, Brink::B_WIDTH_PARAM};
        const int centerAttenParams[] = {Brink::A_CENTER_ATTEN_PARAM, Brink::B_CENTER_ATTEN_PARAM};
        const int widthAttenParams[] = {Brink::A_WIDTH_ATTEN_PARAM, Brink::B_WIDTH_ATTEN_PARAM};
        const int signalInputs[] = {Brink::A_SIGNAL_INPUT, Brink::B_SIGNAL_INPUT};
        const int centerCvInputs[] = {Brink::A_CENTER_CV_INPUT, Brink::B_CENTER_CV_INPUT};
        const int widthCvInputs[] = {Brink::A_WIDTH_CV_INPUT, Brink::B_WIDTH_CV_INPUT};
        const int channelOutputs[2][brink::EVENT_COUNT + 3] = {
            {Brink::A_INSIDE_OUTPUT, Brink::A_OUTSIDE_OUTPUT, Brink::A_POSITION_OUTPUT,
             Brink::A_LOW_UP_OUTPUT, Brink::A_HIGH_UP_OUTPUT, Brink::A_LOW_DOWN_OUTPUT, Brink::A_HIGH_DOWN_OUTPUT},
            {Brink::B_INSIDE_OUTPUT, Brink::B_OUTSIDE_OUTPUT, Brink::B_POSITION_OUTPUT,
             Brink::B_LOW_UP_OUTPUT, Brink::B_HIGH_UP_OUTPUT, Brink::B_LOW_DOWN_OUTPUT, Brink::B_HIGH_DOWN_OUTPUT}
        };

        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const int paramIds[] = {centerParams[channel], widthParams[channel]};
            const BrinkPoint knobPoints[] = {layout.centerKnob, layout.widthKnob};
            for (int knob = 0; knob < 2; ++knob) {
                addParam(createParamCentered<RoundSmallBlackKnob>(
                    mm2px(Vec(knobPoints[knob].x, knobPoints[knob].y)), module, paramIds[knob]));
            }

            const int inputIds[] = {
                signalInputs[channel], centerCvInputs[channel], widthCvInputs[channel]
            };
            const BrinkPoint inputPoints[] = {layout.signal, layout.centerCv, layout.widthCv};
            for (int input = 0; input < 3; ++input) {
                addInput(createInputCentered<PJ301MPort>(
                    mm2px(Vec(inputPoints[input].x, inputPoints[input].y)), module, inputIds[input]));
            }

            const int attenIds[] = {centerAttenParams[channel], widthAttenParams[channel]};
            const BrinkPoint attenPoints[] = {layout.centerAtten, layout.widthAtten};
            for (int atten = 0; atten < 2; ++atten) {
                addParam(createParamCentered<Trimpot>(
                    mm2px(Vec(attenPoints[atten].x, attenPoints[atten].y)), module, attenIds[atten]));
            }

            const BrinkPoint outputPoints[] = {
                layout.inside, layout.outside, layout.position,
                layout.lowUp, layout.highUp, layout.lowDown, layout.highDown
            };
            for (int output = 0; output < brink::EVENT_COUNT + 3; ++output) {
                addOutput(createOutputCentered<PJ301MPort>(
                    mm2px(Vec(outputPoints[output].x, outputPoints[output].y)),
                    module, channelOutputs[channel][output]));
            }
        }

        const int logicOutputs[] = {Brink::AND_OUTPUT, Brink::OR_OUTPUT,
                                    Brink::XOR_OUTPUT, Brink::STATE_OUTPUT};
        for (int logic = 0; logic < 4; ++logic)
            addOutput(createOutputCentered<PJ301MPort>(
                mm2px(Vec(brinkLogicLayout[logic].x, brinkLogicLayout[logic].y)),
                module, logicOutputs[logic]));

        const int gateLights[2][2] = {
            {Brink::A_INSIDE_LIGHT, Brink::A_OUTSIDE_LIGHT},
            {Brink::B_INSIDE_LIGHT, Brink::B_OUTSIDE_LIGHT}
        };
        const int eventLights[2][brink::EVENT_COUNT] = {
            {Brink::A_LOW_UP_LIGHT, Brink::A_HIGH_UP_LIGHT, Brink::A_LOW_DOWN_LIGHT, Brink::A_HIGH_DOWN_LIGHT},
            {Brink::B_LOW_UP_LIGHT, Brink::B_HIGH_UP_LIGHT, Brink::B_LOW_DOWN_LIGHT, Brink::B_HIGH_DOWN_LIGHT}
        };
        for (int channel = 0; channel < 2; ++channel) {
            const BrinkChannelLayout& layout = brinkChannelLayouts[channel];
            const BrinkPoint gatePoints[] = {layout.inside, layout.outside};
            for (int gate = 0; gate < 2; ++gate) {
                const float lightX = gate == 0
                    ? gatePoints[gate].x + brink_layout::OUTPUT_LIGHT_OFFSET
                    : gatePoints[gate].x - brink_layout::OUTPUT_LIGHT_OFFSET;
                if (channel == 0) {
                    addChild(createLightCentered<BrinkTealLight>(
                        mm2px(Vec(lightX, gatePoints[gate].y)), module, gateLights[channel][gate]));
                }
                else {
                    addChild(createLightCentered<BrinkOrangeLight>(
                        mm2px(Vec(lightX, gatePoints[gate].y)), module, gateLights[channel][gate]));
                }
            }
            const BrinkPoint eventPoints[] = {
                layout.lowUp, layout.highUp, layout.lowDown, layout.highDown
            };
            for (int event = 0; event < brink::EVENT_COUNT; ++event) {
                const int side = event % 2;
                const float lightX = side == 0
                    ? eventPoints[event].x + brink_layout::OUTPUT_LIGHT_OFFSET
                    : eventPoints[event].x - brink_layout::OUTPUT_LIGHT_OFFSET;
                if (channel == 0) {
                    addChild(createLightCentered<BrinkTealLight>(
                        mm2px(Vec(lightX, eventPoints[event].y)), module,
                        eventLights[channel][event]));
                }
                else {
                    addChild(createLightCentered<BrinkOrangeLight>(
                        mm2px(Vec(lightX, eventPoints[event].y)), module,
                        eventLights[channel][event]));
                }
            }
        }

        const int logicLights[] = {Brink::AND_LIGHT, Brink::OR_LIGHT,
                                   Brink::XOR_LIGHT, Brink::STATE_LIGHT};
        for (int logic = 0; logic < 4; ++logic)
            addChild(createLightCentered<BrinkVioletLight>(
                mm2px(Vec(brinkLogicLayout[logic].x + brink_layout::OUTPUT_LIGHT_OFFSET,
                          brinkLogicLayout[logic].y)),
                module, logicLights[logic]));
    }
};

Model* modelBrink = createModel<Brink, BrinkWidget>("Brink");
