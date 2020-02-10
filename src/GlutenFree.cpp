#include "RJModules.hpp"
#include "dsp/digital.hpp"
#include "osdialog.h"
#include "common.hpp"
#include "plugin.hpp"
#include "Granulate.h"

#include <iostream>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <mutex>

using namespace std;
#define HISTORY_SIZE (1<<21)

/*
Display
*/

struct GlutenFreeSmallStringDisplayWidget : TransparentWidget {

  std::string *value;
  std::shared_ptr<Font> font;

  GlutenFreeSmallStringDisplayWidget() {
    font = Font::load(assetPlugin(pluginInstance, "res/Pokemon.ttf"));
  };

  void draw(NVGcontext *vg) override
  {

    // Shadow
    NVGcolor backgroundColorS = nvgRGB(0xA0, 0xA0, 0xA0);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.0, 0.0, box.size.x, box.size.y + 2.0, 4.0);
    nvgFillColor(vg, backgroundColorS);
    nvgFill(vg);

    // Background
    NVGcolor backgroundColor = nvgRGB(0xC0, 0xC0, 0xC0);
    nvgBeginPath(vg);
    nvgRoundedRect(vg, 0.0, 0.0, box.size.x, box.size.y, 4.0);
    nvgFillColor(vg, backgroundColor);
    nvgFill(vg);

    // text
    nvgFontSize(vg, 20);
    nvgFontFaceId(vg, font->handle);
    nvgTextLetterSpacing(vg, 0.4);

    std::stringstream to_display;
    to_display << std::setw(3) << *value;

    Vec textPos = Vec(12.0f, 28.0f);
    NVGcolor textColor = nvgRGB(0x00, 0x00, 0x00);
    nvgFillColor(vg, textColor);
    nvgText(vg, textPos.x, textPos.y, to_display.str().c_str(), NULL);
  }
};

struct GlutenFreeRoundLargeBlackKnob : RoundLargeBlackKnob
{
    GlutenFreeRoundLargeBlackKnob()
    {
        setSVG(SVG::load(assetPlugin(pluginInstance, "res/KTFRoundLargeBlackKnob.svg")));
    }
};

struct GlutenFreeRoundBlackSnapKnob : RoundBlackKnob
{
    GlutenFreeRoundBlackSnapKnob()
    {
        setSVG(SVG::load(assetPlugin(pluginInstance, "res/KTFRoundLargeBlackKnob.svg")));
        minAngle = -0.83 * M_PI;
        maxAngle = 0.83 * M_PI;
        snap = true;
    }
};

/*
Widget
*/

struct GlutenFree : Module {
    enum ParamIds {
        GlutenFree_PARAM,
        PARAM_1,
        PARAM_2,
        PARAM_3,
        PARAM_4,
        PARAM_5,
        PARAM_6,
        NUM_PARAMS
    };
    enum InputIds {
        IN_INPUT,
        GATE_INPUT,

        PARAM_1_CV,
        PARAM_2_CV,
        PARAM_3_CV,
        PARAM_4_CV,
        PARAM_5_CV,
        PARAM_6_CV,

        NUM_INPUTS
    };
    enum OutputIds {
        LEFT_OUTPUT,
        RIGHT_OUTPUT,

        COLOR_SEND,
        COLOR_SEND_RIGHT,

        NUM_OUTPUTS
    };
    enum LightIds {
        NUM_LIGHTS
    };

    // Display
    std::string voice_display = "Load! ->";

    // State
    bool note_on = false;

    // GlutenFrees
    stk::Granulate granulate = stk::Granulate();

    GlutenFree() {

        config(NUM_PARAMS, NUM_INPUTS, NUM_OUTPUTS, NUM_LIGHTS);
        configParam(GlutenFree::GlutenFree_PARAM, 0, 23, 0, "Instrument");
        configParam(GlutenFree::PARAM_1, 0, .97, .1, "Param 1");
        configParam(GlutenFree::PARAM_2, 0, 100, 0, "Param 2");
        configParam(GlutenFree::PARAM_3, 5, 100, 5, "Param 3");
        configParam(GlutenFree::PARAM_4, 5, 100, 50, "Param 4");
        configParam(GlutenFree::PARAM_5, 0, 100, 1, "Param 5");
        configParam(GlutenFree::PARAM_6, 0, 100, 1, "Param 6");
    }

    // Pitchies
    float referenceFrequency = 261.626; // C4; frequency at which Rack 1v/octave CVs are zero.
    float referenceSemitone = 60.0; // C4; value of C4 in semitones is arbitrary here, so have it match midi note numbers when rounded to integer.
    float twelfthRootTwo = 1.0594630943592953;
    float logTwelfthRootTwo = logf(1.0594630943592953);
    int referencePitch = 0;
    int referenceOctave = 4;

    // State
    bool fileLoaded = false;

    float cvToFrequency(float cv) {
        return powf(2.0, cv) * referenceFrequency;
    }

    void loadFile(std::string path){
        granulate.openFile(path);
        granulate.setVoices(3);
        voice_display = path;
        fileLoaded = true;
    }

    void process(const ProcessArgs &args) override {

        if(!fileLoaded){
            return;
        }

        float voct = inputs[IN_INPUT].value;
        float processed = 0.0;
        int  instrument_choice = params[GlutenFree_PARAM].value;

        // parameters
        float param_1 = params[PARAM_1].value * rescale(inputs[PARAM_1_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);
        float param_2 = params[PARAM_2].value * rescale(inputs[PARAM_2_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);
        float param_3 = params[PARAM_3].value * rescale(inputs[PARAM_3_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);
        float param_4 = params[PARAM_4].value * rescale(inputs[PARAM_4_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);
        float param_5 = params[PARAM_5].value * rescale(inputs[PARAM_5_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);
        float param_6 = params[PARAM_6].value * rescale(inputs[PARAM_6_CV].normalize(1.0f), 0.f, 5.f, 0.f, 1.f);

        // gate
        bool gate_connected = inputs[GATE_INPUT].isConnected();
        float gate_value = inputs[GATE_INPUT].value;

        // Settings
        granulate.setRandomFactor( param_1 );
        granulate.setStretch( param_2 );

        // unsigned int    duration = 30,
        // unsigned int    rampPercent = 50,
        // int     offset = 0,
        // unsigned int    delay = 0
        granulate.setGrainParameters( param_3, param_4, param_5, param_6 );

        // Tick
        // granulate.setFrequency(voct);
        processed = granulate.tick();

        outputs[RIGHT_OUTPUT].value = processed * 3; // Boost as default volumes are too low

    }
};

/*
Button
*/

struct LoadWavButton : SvgSwitch {
    LoadWavButton() {
        momentary = true;
        addFrame(APP->window->loadSvg(asset::plugin(pluginInstance, "res/LilLEDButton.svg")));
    }

    void onDragStart(const event::DragStart &e) override {
        GlutenFree *module = dynamic_cast<GlutenFree*>(paramQuantity->module);
        if (module){
            std::string dir = "";
            char *path = osdialog_file(OSDIALOG_OPEN, dir.c_str(), NULL, NULL);
            if (path) {
                module->loadFile(path);
                // module->last_path = path;
                // module->file_chosen = true;
                free(path);
            }
        }

        SvgSwitch::onDragStart(e);
    }
};

struct GlutenFreeWidget : ModuleWidget {
  GlutenFreeWidget(GlutenFree *module) {
    setModule(module);
    box.size = Vec(15*10, 380);

    SVGPanel *panel = new SVGPanel();
    panel->box.size = box.size;
    panel->setBackground(SVG::load(assetPlugin(pluginInstance, "res/GlutenFree.svg")));
    addChild(panel);

    // Displays
    if(module != NULL){
        GlutenFreeSmallStringDisplayWidget *fileDisplay = new GlutenFreeSmallStringDisplayWidget();
        fileDisplay->box.pos = Vec(20, 50);
        fileDisplay->box.size = Vec(70, 40);
        fileDisplay->value = &module->voice_display;
        addChild(fileDisplay);
    }

    // Knobs
    int LEFT = 14;
    int RIGHT = 65;
    int DIST = 82;
    int BASE = 115;
    addParam(createParam<LoadWavButton>(Vec(100, 50), module, GlutenFree::GlutenFree_PARAM));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT, BASE), module, GlutenFree::PARAM_1));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT + RIGHT, BASE), module, GlutenFree::PARAM_2));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT, BASE + DIST), module, GlutenFree::PARAM_3));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT + RIGHT, BASE + DIST), module, GlutenFree::PARAM_4));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT, BASE + DIST + DIST), module, GlutenFree::PARAM_5));
    addParam(createParam<GlutenFreeRoundLargeBlackKnob>(Vec(LEFT + RIGHT, BASE + DIST + DIST), module, GlutenFree::PARAM_6));

    // Inputs and Knobs
    addInput(createPort<PJ301MPort>(Vec(11, 277), PortWidget::INPUT, module, GlutenFree::PARAM_1_CV));
    addInput(createPort<PJ301MPort>(Vec(45, 277), PortWidget::INPUT, module, GlutenFree::PARAM_2_CV));
    addInput(createPort<PJ301MPort>(Vec(80, 277), PortWidget::INPUT, module, GlutenFree::PARAM_3_CV));
    addInput(createPort<PJ301MPort>(Vec(112.5, 277), PortWidget::INPUT, module, GlutenFree::PARAM_4_CV));
    addInput(createPort<PJ301MPort>(Vec(80, 277), PortWidget::INPUT, module, GlutenFree::PARAM_5_CV));
    addInput(createPort<PJ301MPort>(Vec(112.5, 277), PortWidget::INPUT, module, GlutenFree::PARAM_6_CV));

    addInput(createPort<PJ301MPort>(Vec(11, 320), PortWidget::INPUT, module, GlutenFree::IN_INPUT));
    addInput(createPort<PJ301MPort>(Vec(45, 320), PortWidget::INPUT, module, GlutenFree::GATE_INPUT));
    // addOutput(createPort<PJ301MPort>(Vec(80, 320), PortWidget::OUTPUT, module, GlutenFree::LEFT_OUTPUT));
    addOutput(createPort<PJ301MPort>(Vec(112.5, 320), PortWidget::OUTPUT, module, GlutenFree::RIGHT_OUTPUT));
    }

    json_t *toJson() override {
        json_t *rootJ = ModuleWidget::toJson();
        GlutenFree *module = dynamic_cast<GlutenFree *>(this->module);
        json_object_set_new(rootJ, "wavef", json_string(module->voice_display.c_str()));
        return rootJ;
    }

    void fromJson(json_t *rootJ) override {
        ModuleWidget::fromJson(rootJ);
        json_t *waveJ = json_object_get(rootJ, "wavef");
        GlutenFree *module = dynamic_cast<GlutenFree *>(this->module);
        if (waveJ)
            module->loadFile(json_string_value(waveJ));
    }

};

Model *modelGlutenFree = createModel<GlutenFree, GlutenFreeWidget>("GlutenFree");