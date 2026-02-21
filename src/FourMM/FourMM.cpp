#include "../plugin.hpp"
#include "engine.h"

struct CoarseParamQuantity : ParamQuantity {
    int freqModeParamId = 0;

    std::string getDisplayValueString() override {
        float val = getValue();
        if (module) {
            int freqMode = (int)module->params[freqModeParamId].getValue();
            if (freqMode == 1) {
                float hz = fourmm::coarse_fixed_from_param(val);
                if (hz >= 1000.f)
                    return string::f("%.1f kHz", hz / 1000.f);
                return string::f("%.1f Hz", hz);
            }
        }
        int idx = (int)roundf(val);
        float ratio = fourmm::coarse_ratio_from_index(idx);
        if (ratio == (float)(int)ratio)
            return string::f("%d:1", (int)ratio);
        return string::f("%.2g:1", ratio);
    }
};

struct FourMM : Module {
    enum ParamId {
        // Global
        ALGO_PARAM,
        XM_PARAM,
        FINE_TUNE_PARAM,
        VCA_PARAM,

        // Per-operator knobs: OP1..OP4
        OP1_COARSE_PARAM, OP2_COARSE_PARAM, OP3_COARSE_PARAM, OP4_COARSE_PARAM,
        OP1_FINE_PARAM,   OP2_FINE_PARAM,   OP3_FINE_PARAM,   OP4_FINE_PARAM,
        OP1_LEVEL_PARAM,  OP2_LEVEL_PARAM,  OP3_LEVEL_PARAM,  OP4_LEVEL_PARAM,
        OP1_WARP_PARAM,   OP2_WARP_PARAM,   OP3_WARP_PARAM,   OP4_WARP_PARAM,
        OP1_FOLD_PARAM,   OP2_FOLD_PARAM,   OP3_FOLD_PARAM,   OP4_FOLD_PARAM,
        OP1_FB_PARAM,     OP2_FB_PARAM,     OP3_FB_PARAM,     OP4_FB_PARAM,

        // Global attenuverters
        XM_CV_ATTEN_PARAM,
        EXT_PM_CV_ATTEN_PARAM,

        // Per-operator CV attenuverters
        OP1_LEVEL_CV_ATTEN_PARAM, OP2_LEVEL_CV_ATTEN_PARAM, OP3_LEVEL_CV_ATTEN_PARAM, OP4_LEVEL_CV_ATTEN_PARAM,
        OP1_WARP_CV_ATTEN_PARAM,  OP2_WARP_CV_ATTEN_PARAM,  OP3_WARP_CV_ATTEN_PARAM,  OP4_WARP_CV_ATTEN_PARAM,
        OP1_FOLD_CV_ATTEN_PARAM,  OP2_FOLD_CV_ATTEN_PARAM,  OP3_FOLD_CV_ATTEN_PARAM,  OP4_FOLD_CV_ATTEN_PARAM,
        OP1_FB_CV_ATTEN_PARAM,    OP2_FB_CV_ATTEN_PARAM,    OP3_FB_CV_ATTEN_PARAM,    OP4_FB_CV_ATTEN_PARAM,

        // Hidden per-operator params (right-click menu / MetaModule)
        OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM, OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM,
        OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM, OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM,

        PARAMS_LEN
    };
    enum InputId {
        // Global
        VOCT_INPUT,
        EXT_PM_CV_INPUT,
        XM_CV_INPUT,

        // Per-operator CV inputs
        OP1_LEVEL_CV_INPUT, OP2_LEVEL_CV_INPUT, OP3_LEVEL_CV_INPUT, OP4_LEVEL_CV_INPUT,
        OP1_WARP_CV_INPUT,  OP2_WARP_CV_INPUT,  OP3_WARP_CV_INPUT,  OP4_WARP_CV_INPUT,
        OP1_FOLD_CV_INPUT,  OP2_FOLD_CV_INPUT,  OP3_FOLD_CV_INPUT,  OP4_FOLD_CV_INPUT,
        OP1_FB_CV_INPUT,    OP2_FB_CV_INPUT,    OP3_FB_CV_INPUT,    OP4_FB_CV_INPUT,

        INPUTS_LEN
    };
    enum OutputId {
        MAIN_OUTPUT,
        OUTPUTS_LEN
    };
    enum LightId {
        LIGHTS_LEN
    };

    fourmm::EngineState engineState;

    FourMM() {
        config(PARAMS_LEN, INPUTS_LEN, OUTPUTS_LEN, LIGHTS_LEN);

        // Global params
        configParam(ALGO_PARAM, 0.f, 10.f, 0.f, "Algorithm");
        getParamQuantity(ALGO_PARAM)->snapEnabled = true;
        configParam(XM_PARAM, 0.f, 1.f, 1.f, "Modulation", "%", 0.f, 100.f);
        configParam(FINE_TUNE_PARAM, -100.f, 100.f, 0.f, "Fine Tune", " cents");
        configParam(VCA_PARAM, 0.f, 1.f, 1.f, "Global VCA", "%", 0.f, 100.f);

        // Per-operator params
        const int coarseIds[] = { OP1_COARSE_PARAM, OP2_COARSE_PARAM, OP3_COARSE_PARAM, OP4_COARSE_PARAM };
        const int fineIds[]   = { OP1_FINE_PARAM, OP2_FINE_PARAM, OP3_FINE_PARAM, OP4_FINE_PARAM };
        const int levelIds[]  = { OP1_LEVEL_PARAM, OP2_LEVEL_PARAM, OP3_LEVEL_PARAM, OP4_LEVEL_PARAM };
        const int warpIds[]   = { OP1_WARP_PARAM, OP2_WARP_PARAM, OP3_WARP_PARAM, OP4_WARP_PARAM };
        const int foldIds[]   = { OP1_FOLD_PARAM, OP2_FOLD_PARAM, OP3_FOLD_PARAM, OP4_FOLD_PARAM };
        const int fbIds[]     = { OP1_FB_PARAM, OP2_FB_PARAM, OP3_FB_PARAM, OP4_FB_PARAM };
        const int freqModeIds[] = { OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM, OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM };
        const int foldTypeIds[] = { OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM, OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM };

        // CV attenuverter IDs
        const int levelCvAIds[] = { OP1_LEVEL_CV_ATTEN_PARAM, OP2_LEVEL_CV_ATTEN_PARAM, OP3_LEVEL_CV_ATTEN_PARAM, OP4_LEVEL_CV_ATTEN_PARAM };
        const int warpCvAIds[]  = { OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM, OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM };
        const int foldCvAIds[]  = { OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM, OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM };
        const int fbCvAIds[]    = { OP1_FB_CV_ATTEN_PARAM, OP2_FB_CV_ATTEN_PARAM, OP3_FB_CV_ATTEN_PARAM, OP4_FB_CV_ATTEN_PARAM };

        // CV input IDs
        const int levelCvIds[] = { OP1_LEVEL_CV_INPUT, OP2_LEVEL_CV_INPUT, OP3_LEVEL_CV_INPUT, OP4_LEVEL_CV_INPUT };
        const int warpCvIds[]  = { OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT, OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT };
        const int foldCvIds[]  = { OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT, OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT };
        const int fbCvIds[]    = { OP1_FB_CV_INPUT, OP2_FB_CV_INPUT, OP3_FB_CV_INPUT, OP4_FB_CV_INPUT };

        for ( int i = 0; i < 4; i++ )
        {
            std::string n = std::to_string( i + 1 );
            auto* cpq = configParam<CoarseParamQuantity>(coarseIds[i], 0.f, 64.f, 3.f, "Op " + n + " Coarse");
            cpq->freqModeParamId = freqModeIds[i];
            configParam(fineIds[i], -100.f, 100.f, 0.f, "Op " + n + " Fine", " cents");
            float levelDefault = (i == 0) ? 1.f : 0.f;
            configParam(levelIds[i], 0.f, 1.f, levelDefault, "Op " + n + " Level", "%", 0.f, 100.f);
            configParam(warpIds[i], 0.f, 1.f, 0.f, "Op " + n + " Warp", "%", 0.f, 100.f);
            configParam(foldIds[i], 0.f, 1.f, 0.f, "Op " + n + " Fold", "%", 0.f, 100.f);
            configParam(fbIds[i], 0.f, 1.f, 0.f, "Op " + n + " Feedback", "%", 0.f, 100.f);

            configSwitch(freqModeIds[i], 0.f, 1.f, 0.f, "Op " + n + " Freq Mode", {"Ratio", "Fixed"});
            configSwitch(foldTypeIds[i], 0.f, 2.f, 0.f, "Op " + n + " Fold Type", {"Symmetric", "Asymmetric", "Soft Clip"});

            configParam(levelCvAIds[i], -1.f, 1.f, 0.f, "Op " + n + " Level CV", "%", 0.f, 100.f);
            configParam(warpCvAIds[i], -1.f, 1.f, 0.f, "Op " + n + " Warp CV", "%", 0.f, 100.f);
            configParam(foldCvAIds[i], -1.f, 1.f, 0.f, "Op " + n + " Fold CV", "%", 0.f, 100.f);
            configParam(fbCvAIds[i], -1.f, 1.f, 0.f, "Op " + n + " Feedback CV", "%", 0.f, 100.f);

            configInput(levelCvIds[i], "Op " + n + " Level CV");
            configInput(warpCvIds[i], "Op " + n + " Warp CV");
            configInput(foldCvIds[i], "Op " + n + " Fold CV");
            configInput(fbCvIds[i], "Op " + n + " Feedback CV");
        }

        // Global attenuverters
        configParam(XM_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "Mod CV", "%", 0.f, 100.f);
        configParam(EXT_PM_CV_ATTEN_PARAM, -1.f, 1.f, 0.f, "Ext PM CV", "%", 0.f, 100.f);

        // Global inputs
        configInput(VOCT_INPUT, "V/OCT");
        configInput(EXT_PM_CV_INPUT, "Ext PM");
        configInput(XM_CV_INPUT, "Mod CV");

        // Output
        configOutput(MAIN_OUTPUT, "Main");
    }

    void process(const ProcessArgs& args) override {
        fourmm::EngineParams ep;

        // --- Global params ---
        ep.algorithm = (int)params[ALGO_PARAM].getValue();
        ep.globalVCA = params[VCA_PARAM].getValue();

        float globalFineCents = params[FINE_TUNE_PARAM].getValue();
        float globalFineMult = exp2f( globalFineCents / 1200.f );

        // V/OCT: base voltage
        float voct = inputs[VOCT_INPUT].getVoltage();
        ep.baseFreq = fourmm::voct_to_freq( voct ) * globalFineMult;

        // Mod: knob + attenuated CV
        float modCv = inputs[XM_CV_INPUT].getVoltage() * params[XM_CV_ATTEN_PARAM].getValue() / 10.f;
        ep.modMaster = clamp( params[XM_PARAM].getValue() + modCv, 0.f, 1.f );

        // Ext PM: attenuated CV only (no depth knob)
        float extPmCv = inputs[EXT_PM_CV_INPUT].getVoltage() * params[EXT_PM_CV_ATTEN_PARAM].getValue();
        ep.extPmDepth = clamp(extPmCv, 0.f, 1.f);

        // --- Per-operator params ---
        const int coarseIds[] = { OP1_COARSE_PARAM, OP2_COARSE_PARAM, OP3_COARSE_PARAM, OP4_COARSE_PARAM };
        const int fineIds[]   = { OP1_FINE_PARAM, OP2_FINE_PARAM, OP3_FINE_PARAM, OP4_FINE_PARAM };
        const int levelIds[]  = { OP1_LEVEL_PARAM, OP2_LEVEL_PARAM, OP3_LEVEL_PARAM, OP4_LEVEL_PARAM };
        const int warpIds[]   = { OP1_WARP_PARAM, OP2_WARP_PARAM, OP3_WARP_PARAM, OP4_WARP_PARAM };
        const int foldIds[]   = { OP1_FOLD_PARAM, OP2_FOLD_PARAM, OP3_FOLD_PARAM, OP4_FOLD_PARAM };
        const int fbIds[]     = { OP1_FB_PARAM, OP2_FB_PARAM, OP3_FB_PARAM, OP4_FB_PARAM };
        const int freqModeIds[] = { OP1_FREQ_MODE_PARAM, OP2_FREQ_MODE_PARAM, OP3_FREQ_MODE_PARAM, OP4_FREQ_MODE_PARAM };
        const int foldTypeIds[] = { OP1_FOLD_TYPE_PARAM, OP2_FOLD_TYPE_PARAM, OP3_FOLD_TYPE_PARAM, OP4_FOLD_TYPE_PARAM };

        const int levelCvIds[] = { OP1_LEVEL_CV_INPUT, OP2_LEVEL_CV_INPUT, OP3_LEVEL_CV_INPUT, OP4_LEVEL_CV_INPUT };
        const int warpCvIds[]  = { OP1_WARP_CV_INPUT, OP2_WARP_CV_INPUT, OP3_WARP_CV_INPUT, OP4_WARP_CV_INPUT };
        const int foldCvIds[]  = { OP1_FOLD_CV_INPUT, OP2_FOLD_CV_INPUT, OP3_FOLD_CV_INPUT, OP4_FOLD_CV_INPUT };
        const int fbCvIds[]    = { OP1_FB_CV_INPUT, OP2_FB_CV_INPUT, OP3_FB_CV_INPUT, OP4_FB_CV_INPUT };

        const int levelCvAIds[] = { OP1_LEVEL_CV_ATTEN_PARAM, OP2_LEVEL_CV_ATTEN_PARAM, OP3_LEVEL_CV_ATTEN_PARAM, OP4_LEVEL_CV_ATTEN_PARAM };
        const int warpCvAIds[]  = { OP1_WARP_CV_ATTEN_PARAM, OP2_WARP_CV_ATTEN_PARAM, OP3_WARP_CV_ATTEN_PARAM, OP4_WARP_CV_ATTEN_PARAM };
        const int foldCvAIds[]  = { OP1_FOLD_CV_ATTEN_PARAM, OP2_FOLD_CV_ATTEN_PARAM, OP3_FOLD_CV_ATTEN_PARAM, OP4_FOLD_CV_ATTEN_PARAM };
        const int fbCvAIds[]    = { OP1_FB_CV_ATTEN_PARAM, OP2_FB_CV_ATTEN_PARAM, OP3_FB_CV_ATTEN_PARAM, OP4_FB_CV_ATTEN_PARAM };

        for ( int i = 0; i < 4; i++ )
        {
            int freqMode = (int)params[freqModeIds[i]].getValue();
            ep.opFreqMode[i] = freqMode;
            ep.opFoldType[i] = (int)params[foldTypeIds[i]].getValue();

            // Coarse: index->ratio in ratio mode, index->Hz in fixed mode
            float coarseParam = params[coarseIds[i]].getValue();
            if ( freqMode == 0 )
                ep.opCoarse[i] = fourmm::coarse_ratio_from_index( (int)roundf(coarseParam) );
            else
                ep.opCoarse[i] = fourmm::coarse_fixed_from_param( coarseParam );

            // Fine: cents -> multiplier
            ep.opFine[i] = exp2f( params[fineIds[i]].getValue() / 1200.f );

            // Level + CV
            float levelCv = inputs[levelCvIds[i]].getVoltage() * params[levelCvAIds[i]].getValue() / 10.f;
            ep.opLevel[i] = clamp( params[levelIds[i]].getValue() + levelCv, 0.f, 1.f );

            // Warp + CV
            float warpCv = inputs[warpCvIds[i]].getVoltage() * params[warpCvAIds[i]].getValue() / 10.f;
            ep.opWarp[i] = clamp( params[warpIds[i]].getValue() + warpCv, 0.f, 1.f );

            // Fold + CV
            float foldCv = inputs[foldCvIds[i]].getVoltage() * params[foldCvAIds[i]].getValue() / 10.f;
            ep.opFold[i] = clamp( params[foldIds[i]].getValue() + foldCv, 0.f, 1.f );

            // Feedback + CV
            float fbCv = inputs[fbCvIds[i]].getVoltage() * params[fbCvAIds[i]].getValue() / 10.f;
            ep.opFeedback[i] = clamp( params[fbIds[i]].getValue() + fbCv, 0.f, 1.f );
        }

        // --- Run engine ---
        float extPm = inputs[EXT_PM_CV_INPUT].getVoltage();  // Audio-rate PM input
        float out = fourmm::engine_process( engineState, ep, args.sampleTime, extPm );

        // Scale to +/-5V
        outputs[MAIN_OUTPUT].setVoltage( out * 5.f );
    }

};

#include "layout.h"

struct AlgoDisplay : Widget {
    FourMM* module = nullptr;

    AlgoDisplay() {
        using namespace fourmm_layout;
        float w = PANEL_WIDTH - 32;
        box.size = mm2px(Vec(w, 8));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if ( layer != 1 ) return;

        // Background
        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(1));
        nvgFillColor(args.vg, nvgRGB(10, 10, 26));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(64, 64, 96));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        // Text
        int algo = 0;
        if ( module )
            algo = (int)module->params[FourMM::ALGO_PARAM].getValue();

        const char* text = fourmm::algorithmStrings[algo];
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(128, 255, 128));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, box.size.x / 2, box.size.y / 2, text, nullptr);

        Widget::drawLayer(args, layer);
    }

    void onButton(const ButtonEvent& e) override {
        if ( !module ) return;

        if ( e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT )
        {
            int algo = (int)module->params[FourMM::ALGO_PARAM].getValue();
            algo = ( algo + 1 ) % 11;
            module->params[FourMM::ALGO_PARAM].setValue( (float)algo );
            e.consume(this);
        }
        else if ( e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT )
        {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Algorithm"));
            for ( int i = 0; i < 11; i++ )
            {
                int algoIdx = i;
                menu->addChild(createMenuItem(fourmm::algorithmStrings[i], "",
                    [=]() { module->params[FourMM::ALGO_PARAM].setValue((float)algoIdx); }));
            }
            e.consume(this);
        }
    }
};

struct FoldTypeDisplay : Widget {
    FourMM* module = nullptr;
    int opIndex = 0;  // 0-3

    static constexpr float W_MM = 8.0f;
    static constexpr float H_MM = 4.0f;

    static const char* shortNames[3];
    static const char* longNames[3];

    FoldTypeDisplay() {
        box.size = mm2px(Vec(W_MM, H_MM));
    }

    int getFoldType() {
        if ( !module ) return 0;
        const int ids[] = { FourMM::OP1_FOLD_TYPE_PARAM, FourMM::OP2_FOLD_TYPE_PARAM,
                            FourMM::OP3_FOLD_TYPE_PARAM, FourMM::OP4_FOLD_TYPE_PARAM };
        return (int)module->params[ids[opIndex]].getValue();
    }

    void setFoldType(int v) {
        if ( !module ) return;
        const int ids[] = { FourMM::OP1_FOLD_TYPE_PARAM, FourMM::OP2_FOLD_TYPE_PARAM,
                            FourMM::OP3_FOLD_TYPE_PARAM, FourMM::OP4_FOLD_TYPE_PARAM };
        module->params[ids[opIndex]].setValue((float)v);
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if ( layer != 1 ) return;

        nvgBeginPath(args.vg);
        nvgRoundedRect(args.vg, 0, 0, box.size.x, box.size.y, mm2px(0.5f));
        nvgFillColor(args.vg, nvgRGB(10, 10, 26));
        nvgFill(args.vg);
        nvgStrokeColor(args.vg, nvgRGB(64, 64, 96));
        nvgStrokeWidth(args.vg, 0.5f);
        nvgStroke(args.vg);

        int ft = getFoldType();
        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if ( font ) {
            nvgFontFaceId(args.vg, font->handle);
            nvgFontSize(args.vg, 9);
            nvgFillColor(args.vg, nvgRGB(180, 200, 180));
            nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
            nvgText(args.vg, box.size.x / 2, box.size.y / 2, shortNames[ft], nullptr);
        }

        Widget::drawLayer(args, layer);
    }

    void onButton(const ButtonEvent& e) override {
        if ( !module ) return;

        if ( e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_LEFT )
        {
            int ft = ( getFoldType() + 1 ) % 3;
            setFoldType(ft);
            e.consume(this);
        }
        else if ( e.action == GLFW_PRESS && e.button == GLFW_MOUSE_BUTTON_RIGHT )
        {
            ui::Menu* menu = createMenu();
            menu->addChild(createMenuLabel("Fold Type"));
            for ( int i = 0; i < 3; i++ )
            {
                int idx = i;
                menu->addChild(createMenuItem(longNames[i], "",
                    [=]() { setFoldType(idx); }));
            }
            e.consume(this);
        }
    }

    void onHover(const HoverEvent& e) override {
        if ( !tooltip ) {
            int ft = getFoldType();
            std::string tipText = "Fold: " + std::string(longNames[ft]) + " (click to cycle)";
            tooltip = new ui::Tooltip;
            tooltip->text = tipText;
            // Position tooltip near the widget
            tooltip->box.pos = getAbsoluteOffset(box.size);
            APP->scene->addChild(tooltip);
        }
        e.consume(this);
        Widget::onHover(e);
    }

    void onLeave(const LeaveEvent& e) override {
        if ( tooltip ) {
            APP->scene->removeChild(tooltip);
            delete tooltip;
            tooltip = nullptr;
        }
        Widget::onLeave(e);
    }

    ~FoldTypeDisplay() {
        if ( tooltip ) {
            APP->scene->removeChild(tooltip);
            delete tooltip;
        }
    }

private:
    ui::Tooltip* tooltip = nullptr;
};

const char* FoldTypeDisplay::shortNames[3] = { "Sym", "Asym", "Soft" };
const char* FoldTypeDisplay::longNames[3] = { "Symmetric", "Asymmetric", "Soft Clip" };

namespace {
struct PanelLabels : Widget {
    PanelLabels() {
        using namespace fourmm_layout;
        box.size = mm2px(Vec(PANEL_WIDTH, PANEL_HEIGHT));
    }

    void drawLayer(const DrawArgs& args, int layer) override {
        if ( layer != 1 ) return;

        using namespace fourmm_layout;

        std::shared_ptr<Font> font = APP->window->loadFont(
            asset::system("res/fonts/DejaVuSans.ttf"));
        if ( !font ) return;
        nvgFontFaceId(args.vg, font->handle);

        // ── Title ──
        nvgFontSize(args.vg, 14);
        nvgFillColor(args.vg, nvgRGB(220, 220, 220));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);
        nvgText(args.vg, mm2px(PANEL_WIDTH / 2), mm2px(8.0f), "FourMM", nullptr);

        // ── wintoid logo (bottom center, between screws) ──
        nvgFontFaceId(args.vg, font->handle);
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

        // ── Global control labels (alongside controls, right-aligned) ──
        nvgFontSize(args.vg, 9);
        nvgFillColor(args.vg, nvgRGB(180, 180, 180));
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

        float knobOff = mm2px(4.0f);   // small knob radius + 1.5mm gap
        nvgText(args.vg, mm2px(FINE_TUNE_KNOB_X) - knobOff, mm2px(FINE_TUNE_KNOB_Y), "Fine", nullptr);
        nvgText(args.vg, mm2px(VCA_KNOB_X) - knobOff, mm2px(VCA_KNOB_Y), "VCA", nullptr);

        float jackOff = mm2px(4.7f);   // jack radius + 1.5mm gap
        nvgText(args.vg, mm2px(VOCT_JACK_X) - jackOff, mm2px(VOCT_JACK_Y), "V/Oct", nullptr);
        nvgText(args.vg, mm2px(XM_KNOB_X) - knobOff, mm2px(XM_KNOB_Y), "XMod", nullptr);
        // Ext PM label: positioned left of jack
        nvgText(args.vg, mm2px(FM_CV_JACK_X) - jackOff, mm2px(FM_CV_JACK_Y), "Ext PM", nullptr);

        // ── Operator column headers (centered over middle sub-column) ──
        nvgFontSize(args.vg, 11);
        nvgFillColor(args.vg, nvgRGB(220, 220, 220));
        nvgTextAlign(args.vg, NVG_ALIGN_CENTER | NVG_ALIGN_MIDDLE);

        const char* opLabels[] = { "OP1", "OP2", "OP3", "OP4" };
        const float opX[] = { OP1_MID_X, OP2_MID_X, OP3_MID_X, OP4_MID_X };
        for ( int i = 0; i < 4; i++ )
            nvgText(args.vg, mm2px(opX[i]), mm2px(OP_HEADER_Y), opLabels[i], nullptr);

        // ── Row labels (left side) ──
        nvgFontSize(args.vg, 9);
        nvgFillColor(args.vg, nvgRGB(160, 160, 180));
        nvgTextAlign(args.vg, NVG_ALIGN_RIGHT | NVG_ALIGN_MIDDLE);

        const char* rowLabels[] = { "Freq", "Level", "Warp", "Fold", "FB" };
        const float rowY[] = { OP1_KNOB0_Y, OP1_KNOB1_Y, OP1_KNOB2_Y,
                                OP1_KNOB3_Y, OP1_KNOB4_Y };
        for ( int i = 0; i < 5; i++ )
            nvgText(args.vg, mm2px(LABEL_COL_X), mm2px(rowY[i]), rowLabels[i], nullptr);

        Widget::drawLayer(args, layer);
    }
};
} // anonymous namespace

struct FourMMWidget : ModuleWidget {
    FourMMWidget(FourMM* module) {
        setModule(module);
        setPanel(createPanel(asset::plugin(pluginInstance, "res/FourMM.svg")));

        // Screws
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, 0)));
        addChild(createWidget<ScrewSilver>(Vec(RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));
        addChild(createWidget<ScrewSilver>(Vec(box.size.x - 2 * RACK_GRID_WIDTH, RACK_GRID_HEIGHT - RACK_GRID_WIDTH)));

        using namespace fourmm_layout;

        // Panel labels (NanoVG-drawn text overlay)
        {
            PanelLabels* labels = new PanelLabels();
            addChild(labels);
        }

        // Algorithm display
        {
            AlgoDisplay* display = new AlgoDisplay();
            display->module = module;
            float algoW = PANEL_WIDTH - 32;
            display->box.pos = mm2px(Vec(ALGO_DISPLAY_X - algoW / 2, ALGO_DISPLAY_Y - 4));
            addChild(display);
        }

        // --- Global controls ---
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(VOCT_JACK_X, VOCT_JACK_Y)), module, FourMM::VOCT_INPUT));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(FINE_TUNE_KNOB_X, FINE_TUNE_KNOB_Y)), module, FourMM::FINE_TUNE_PARAM));
        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(VCA_KNOB_X, VCA_KNOB_Y)), module, FourMM::VCA_PARAM));
        addOutput(createOutputCentered<PJ301MPort>(mm2px(Vec(MAIN_OUTPUT_X, MAIN_OUTPUT_Y)), module, FourMM::MAIN_OUTPUT));

        addParam(createParamCentered<RoundSmallBlackKnob>(mm2px(Vec(XM_KNOB_X, XM_KNOB_Y)), module, FourMM::XM_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(XM_CV_JACK_X, XM_CV_JACK_Y)), module, FourMM::XM_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(XM_CV_ATTEN_X, XM_CV_ATTEN_Y)), module, FourMM::XM_CV_ATTEN_PARAM));
        addInput(createInputCentered<PJ301MPort>(mm2px(Vec(FM_CV_JACK_X, FM_CV_JACK_Y)), module, FourMM::EXT_PM_CV_INPUT));
        addParam(createParamCentered<Trimpot>(mm2px(Vec(FM_CV_ATTEN_X, FM_CV_ATTEN_Y)), module, FourMM::EXT_PM_CV_ATTEN_PARAM));

        // --- Per-operator: Row 0 = Coarse + Toggle + Fine ---
        const int coarseIds[] = { FourMM::OP1_COARSE_PARAM, FourMM::OP2_COARSE_PARAM, FourMM::OP3_COARSE_PARAM, FourMM::OP4_COARSE_PARAM };
        const int fineIds[]   = { FourMM::OP1_FINE_PARAM, FourMM::OP2_FINE_PARAM, FourMM::OP3_FINE_PARAM, FourMM::OP4_FINE_PARAM };
        const int freqToggleIds[] = { FourMM::OP1_FREQ_MODE_PARAM, FourMM::OP2_FREQ_MODE_PARAM, FourMM::OP3_FREQ_MODE_PARAM, FourMM::OP4_FREQ_MODE_PARAM };

        const float coarseX[4] = { OP1_KNOB0_X, OP2_KNOB0_X, OP3_KNOB0_X, OP4_KNOB0_X };
        const float toggleX[4] = { OP1_TOGGLE_X, OP2_TOGGLE_X, OP3_TOGGLE_X, OP4_TOGGLE_X };
        const float fineX[4]   = { OP1_FINE_X, OP2_FINE_X, OP3_FINE_X, OP4_FINE_X };
        float row0Y = OP1_KNOB0_Y;

        for ( int op = 0; op < 4; op++ )
        {
            addParam(createParamCentered<RoundSmallBlackKnob>(
                mm2px(Vec(coarseX[op], row0Y)), module, coarseIds[op]));
            addParam(createParamCentered<CKSS>(
                mm2px(Vec(toggleX[op], row0Y)), module, freqToggleIds[op]));
            addParam(createParamCentered<Trimpot>(
                mm2px(Vec(fineX[op], row0Y)), module, fineIds[op]));
        }

        // --- Per-operator: Rows 1-4 = Level, Warp, Fold, FB knobs ---
        const int levelIds[]  = { FourMM::OP1_LEVEL_PARAM, FourMM::OP2_LEVEL_PARAM, FourMM::OP3_LEVEL_PARAM, FourMM::OP4_LEVEL_PARAM };
        const int warpIds[]   = { FourMM::OP1_WARP_PARAM, FourMM::OP2_WARP_PARAM, FourMM::OP3_WARP_PARAM, FourMM::OP4_WARP_PARAM };
        const int foldIds[]   = { FourMM::OP1_FOLD_PARAM, FourMM::OP2_FOLD_PARAM, FourMM::OP3_FOLD_PARAM, FourMM::OP4_FOLD_PARAM };
        const int fbIds[]     = { FourMM::OP1_FB_PARAM, FourMM::OP2_FB_PARAM, FourMM::OP3_FB_PARAM, FourMM::OP4_FB_PARAM };

        const float knobX[4] = { OP1_KNOB0_X, OP2_KNOB0_X, OP3_KNOB0_X, OP4_KNOB0_X };
        const float knobY[4] = { OP1_KNOB1_Y, OP1_KNOB2_Y, OP1_KNOB3_Y, OP1_KNOB4_Y };

        for ( int op = 0; op < 4; op++ )
        {
            const int* ids[] = { levelIds, warpIds, foldIds, fbIds };
            for ( int row = 0; row < 4; row++ )
            {
                addParam(createParamCentered<RoundSmallBlackKnob>(
                    mm2px(Vec(knobX[op], knobY[row])), module, ids[row][op]));
            }
        }

        // --- Per-operator CV jacks + attenuverters (beside knob rows 1-4) ---
        const int levelCvIds[] = { FourMM::OP1_LEVEL_CV_INPUT, FourMM::OP2_LEVEL_CV_INPUT, FourMM::OP3_LEVEL_CV_INPUT, FourMM::OP4_LEVEL_CV_INPUT };
        const int warpCvIds[]  = { FourMM::OP1_WARP_CV_INPUT, FourMM::OP2_WARP_CV_INPUT, FourMM::OP3_WARP_CV_INPUT, FourMM::OP4_WARP_CV_INPUT };
        const int foldCvIds[]  = { FourMM::OP1_FOLD_CV_INPUT, FourMM::OP2_FOLD_CV_INPUT, FourMM::OP3_FOLD_CV_INPUT, FourMM::OP4_FOLD_CV_INPUT };
        const int fbCvIds[]    = { FourMM::OP1_FB_CV_INPUT, FourMM::OP2_FB_CV_INPUT, FourMM::OP3_FB_CV_INPUT, FourMM::OP4_FB_CV_INPUT };
        const int* cvInputIds[] = { levelCvIds, warpCvIds, foldCvIds, fbCvIds };

        const int levelCvAIds[] = { FourMM::OP1_LEVEL_CV_ATTEN_PARAM, FourMM::OP2_LEVEL_CV_ATTEN_PARAM, FourMM::OP3_LEVEL_CV_ATTEN_PARAM, FourMM::OP4_LEVEL_CV_ATTEN_PARAM };
        const int warpCvAIds[]  = { FourMM::OP1_WARP_CV_ATTEN_PARAM, FourMM::OP2_WARP_CV_ATTEN_PARAM, FourMM::OP3_WARP_CV_ATTEN_PARAM, FourMM::OP4_WARP_CV_ATTEN_PARAM };
        const int foldCvAIds[]  = { FourMM::OP1_FOLD_CV_ATTEN_PARAM, FourMM::OP2_FOLD_CV_ATTEN_PARAM, FourMM::OP3_FOLD_CV_ATTEN_PARAM, FourMM::OP4_FOLD_CV_ATTEN_PARAM };
        const int fbCvAIds[]    = { FourMM::OP1_FB_CV_ATTEN_PARAM, FourMM::OP2_FB_CV_ATTEN_PARAM, FourMM::OP3_FB_CV_ATTEN_PARAM, FourMM::OP4_FB_CV_ATTEN_PARAM };
        const int* cvAttenIds[] = { levelCvAIds, warpCvAIds, foldCvAIds, fbCvAIds };

        const float cvJackX[4]  = { OP1_CV0_JACK_X, OP2_CV0_JACK_X, OP3_CV0_JACK_X, OP4_CV0_JACK_X };
        const float cvAttenX[4] = { OP1_CV0_ATTEN_X, OP2_CV0_ATTEN_X, OP3_CV0_ATTEN_X, OP4_CV0_ATTEN_X };
        const float cvY[4] = { OP1_CV0_JACK_Y, OP1_CV1_JACK_Y, OP1_CV2_JACK_Y, OP1_CV3_JACK_Y };

        for ( int op = 0; op < 4; op++ )
        {
            for ( int cv = 0; cv < 4; cv++ )
            {
                addInput(createInputCentered<PJ301MPort>(
                    mm2px(Vec(cvJackX[op], cvY[cv])), module, cvInputIds[cv][op]));
                addParam(createParamCentered<Trimpot>(
                    mm2px(Vec(cvAttenX[op], cvY[cv])), module, cvAttenIds[cv][op]));
            }
        }

        // --- Per-operator fold type displays (below fold knob) ---
        const float foldTypeX[4] = { OP1_FOLD_TYPE_X, OP2_FOLD_TYPE_X, OP3_FOLD_TYPE_X, OP4_FOLD_TYPE_X };
        const float foldTypeY[4] = { OP1_FOLD_TYPE_Y, OP2_FOLD_TYPE_Y, OP3_FOLD_TYPE_Y, OP4_FOLD_TYPE_Y };

        for ( int op = 0; op < 4; op++ )
        {
            FoldTypeDisplay* ftd = new FoldTypeDisplay();
            ftd->module = module;
            ftd->opIndex = op;
            ftd->box.pos = mm2px(Vec(foldTypeX[op] - FoldTypeDisplay::W_MM / 2,
                                     foldTypeY[op] - FoldTypeDisplay::H_MM / 2));
            addChild(ftd);
        }
    }
};

Model* modelFourMM = createModel<FourMM, FourMMWidget>("FourMM");
