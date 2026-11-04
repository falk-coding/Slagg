#pragma once

#include "PluginProcessor.h"
#include "Skin.h"

namespace slagg::ui
{
// Exportområdet (DAW:ns markörer eller Från/Till takt).
struct ExportRange { double from = 0, to = 16; juce::String label, barsText; };

// En ljudremsa: rubrik (klick = provlyssna), lampa som blinkar vid slag, rattar i två kolumner.
class VoiceStrip : public juce::Component, public juce::SettableTooltipClient
{
public:
    VoiceStrip (SlaggProcessor& p, int voiceIndex);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void refresh();

private:
    SlaggProcessor& processor;
    const int voice;
    std::vector<std::unique_ptr<Control>> controls;
    uint32_t lastHits = 0;
    float flash = 0.0f;
};

// Dragruta: drar en MIDI-fil (ett ljud eller alla) till DAW:n.
class MidiDragSource : public juce::Component, public juce::SettableTooltipClient
{
public:
    MidiDragSource (SlaggProcessor& p, uint32_t voiceMask, bool big);
    void paint (juce::Graphics&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    std::function<ExportRange()> getRange;

private:
    SlaggProcessor& processor;
    const uint32_t mask;
    const bool big;
    bool dragging = false;
};

// Vad man redigerar när man drar på ett slag i rutnätet.
enum class EditMode { velocity = 0, probability, ratchet, nudge };

// Rutnätet: ett spår per ljud, 32 steg. Klick = på/av, dra på ett slag = värdet i valt läge,
// dubbelklick = tillbaka till standard.
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (SlaggProcessor& p);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void refresh();

    EditMode mode = EditMode::velocity;
    juce::Rectangle<float> cell (int voice, int step) const;
    int rowHeight() const { return getHeight() / slagg::dsp::numVoices; }

private:
    int stepAt (float x) const;
    SlaggProcessor& processor;
    int dragVoice = -1, dragStep = -1, lastPaintStep = -1;
    bool paintValue = true, wasOn = false, valueDrag = false;
    float startValue = 0.8f;
    double lastPpq = -2.0;
    int lastHash = 0, lastProgram = -1;
};

class SeqPanel : public juce::Component
{
public:
    explicit SeqPanel (SlaggProcessor& p);
    void paint (juce::Graphics&) override;
    void resized() override;
    void refresh();
    ExportRange computeRange() const;

private:
    struct Row
    {
        juce::TextButton seq, mute { "M" };
        juce::Slider len { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> seqA, muteA;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> lenA;
        std::unique_ptr<MidiDragSource> drag;
    };

    SlaggProcessor& processor;
    std::array<Row, slagg::dsp::numVoices> rows;
    StepGrid grid;
    MidiDragSource dragAll;
    juce::ComboBox rangeMode;
    juce::Slider fromBar { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Slider toBar { juce::Slider::IncDecButtons, juce::Slider::TextBoxLeft };
    juce::Label fromLabel, toLabel;
    juce::TextButton allOn { "Seq on" }, clearButton { "Clear" }, rerollButton { "Reroll" };
    juce::ComboBox kitBox;
    juce::TextButton prevKit { "<" }, nextKit { ">" };
    std::array<juce::TextButton, 4> modeButtons;
    std::vector<std::unique_ptr<Control>> grooveControls, songControls;
    juce::Rectangle<int> songArea;
    juce::String lastLabel;
    int lastProgram = -1;
    void selectKit (int index);
};
} // namespace slagg::ui

class SlaggEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SlaggEditor (SlaggProcessor&);
    ~SlaggEditor() override;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void refreshNow() { timerCallback(); }

    static constexpr int earW = 36, innerW = 1120, baseWidth = innerW + 2 * earW;
    static constexpr int voicesH = 430, seqH = 378, unitGap = 6;
    static constexpr int baseHeight = voicesH + unitGap + seqH;

private:
    void timerCallback() override;

    SlaggProcessor& processor;
    slagg::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltipWindow { this, 700 };
    juce::Component content;
    std::vector<std::unique_ptr<slagg::ui::VoiceStrip>> strips;
    std::vector<std::unique_ptr<slagg::ui::Control>> globals, rumbleControls;
    juce::Rectangle<int> rumbleArea;
    slagg::ui::SeqPanel seqPanel;
    juce::Image panelTexture, grimeOverlay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SlaggEditor)
};
