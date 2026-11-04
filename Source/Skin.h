#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_utils/juce_audio_utils.h>

// Skin (v1.7): mörk, sliten apparat – nästan svart plåt med sprickor, tunna graverade linjer,
// rattar med borstad aluminiumtopp, ljusgrå text, röda och bärnstensfärgade signallampor.
namespace slagg::ui
{
namespace colours
{
    inline const juce::Colour metal        { 0xff141618 };   // panelens grundfärg (kall, nästan svart)
    inline const juce::Colour metalDark    { 0xff0b0c0e };
    inline const juce::Colour engraveDark  { 0xff040506 };
    inline const juce::Colour engraveLight { 0xff33373b };
    inline const juce::Colour text         { 0xffc3c8ca };   // kall, blekt tryckfärg (läsbar även i skuggan)
    inline const juce::Colour dimText      { 0xff838a90 };
    inline const juce::Colour bakelite     { 0xff08090a };
    inline const juce::Colour hairline     { 0xff8a9095 };   // tunna linjer mellan sektionerna
    inline const juce::Colour lampRed      { 0xffe2482c };
    inline const juce::Colour lampRedOff   { 0xff3d1e17 };
    inline const juce::Colour lampAmber    { 0xffe4a43c };
    inline const juce::Colour lampAmberOff { 0xff3a2e18 };

    // Äldre namn som resten av GUI-koden använder.
    inline const juce::Colour background = metalDark;
    inline const juce::Colour panel      = metal;
    inline const juce::Colour panelEdge  = engraveLight;
    inline const juce::Colour accent     = lampRed;
    inline const juce::Colour accent2    = lampAmber;
    inline const juce::Colour knobBody   { 0xff2a2a27 };
}

// Inbäddat typsnitt (PT Sans Narrow). bold = rubriker/stencil-känsla.
juce::Font panelFont (float height, bool bold = true);

// Graverad ram: mörk linje upptill/vänster, ljus nedtill/höger (nedsänkt i plåten).
void drawEngravedBox (juce::Graphics&, juce::Rectangle<float> r, float alpha = 1.0f);
// Stjärnskruv/spårskruv.
void drawScrew (juce::Graphics&, juce::Point<float> centre, float radius, float slotAngle);
// Signallampa (glöder när den är tänd).
void drawLamp (juce::Graphics&, juce::Rectangle<float> r, juce::Colour lit, juce::Colour off, bool on, float alpha = 1.0f);
// Sektionsrubrik: kyrilliska + latin, med graverad linje.
// note = kort text längst till höger i rubriken (t.ex. röster i bruk).
void drawSectionFrame (juce::Graphics&, juce::Rectangle<float> r, const juce::String& title, const juce::String& note = {});
// Varumärkesloggan "falk elektro" längst upp till vänster och produktnamnet efter. Returnerar använd bredd.
float drawBrandHeader (juce::Graphics&, juce::Rectangle<float> area, const juce::String& product);
// Rackfäste (19") med ovala hål och skruvar.
void drawRackEar (juce::Graphics&, juce::Rectangle<float> r);
// Grynig, repig plåttextur (genereras en gång).
juce::Image makePanelTexture (int width, int height);
// Korn, damm och smuts som läggs ÖVER allt (även rattar och text) – ARGB.
juce::Image makeGrimeOverlay (int width, int height);
// Punktbelysning: ljuskägla uppifrån, kanterna försvinner i mörker. Ritas över allt.
void drawSpotLight (juce::Graphics&, juce::Rectangle<float> area, juce::Point<float> spot);

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();

    juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawComboBox (juce::Graphics&, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool highlighted, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int width, int height) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;
    void drawLabel (juce::Graphics&, juce::Label&) override;
};

// En ratt eller en väljare med namn under.
class Control : public juce::Component
{
public:
    Control (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& label,
             juce::Colour colour, bool isChoice);

    void resized() override;

private:
    juce::Label label;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::ComboBox> combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
};

// Titel skrivs "SVENSKA|English" (engelskan visas inom parentes).
class Section : public juce::Component
{
public:
    // title är UTF-8 (å, ä, ö).
    Section (const char* titleUtf8, juce::Colour colour);

    void add (juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, const juce::String& label,
              bool isChoice = false);
    void paint (juce::Graphics&) override;
    void resized() override;
    void setHeaderNote (const juce::String& n) { if (n != note) { note = n; repaint(); } }
    // Rattarna från och med index `first` ramas in som en grupp med en liten rubrik (t.ex. undertonen).
    void setGroup (int first, const juce::String& caption) { groupFirst = first; groupCaption = caption; repaint(); }

private:
    int groupFirst = -1;
    juce::String groupCaption;
    juce::String title, note;
    juce::Colour colour;
    std::vector<std::unique_ptr<Control>> controls;
};
} // namespace slagg::ui
