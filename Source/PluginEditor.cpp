#include "PluginEditor.h"

using namespace slagg;

namespace slagg::ui
{
namespace
{
    void markDirty (SlaggProcessor& p)
    {
        p.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    }

    juce::String formatBar (double ppq, double barPpq)
    {
        const double b = ppq / barPpq + 1.0;
        return std::abs (b - std::round (b)) < 0.01 ? juce::String (juce::roundToInt (b)) : juce::String (b, 2);
    }

    juce::String swedishName (int v)
    {
        return juce::String::fromUTF8 (voiceInfo[(size_t) v].titleUtf8).upToFirstOccurrenceOf ("|", false, false);
    }
}

//==============================================================================
VoiceStrip::VoiceStrip (SlaggProcessor& p, int v) : processor (p), voice (v)
{
    const auto& info = voiceInfo[(size_t) v];
    auto add = [this] (const char* suffix, const juce::String& label, bool choice = false)
    {
        controls.push_back (std::make_unique<Control> (processor.apvts, pid::id (voice, suffix), label, colours::accent2, choice));
        addAndMakeVisible (*controls.back());
    };
    add (pid::sTune, "Tune");
    add (pid::sDecay, "Decay");
    add (pid::sTone, info.toneLabel);
    add (pid::sSnap, info.snapLabel);
    add (pid::sDrive, "Drive");
    add (pid::sLevel, "Level");
    add (pid::sCutoff, "Cutoff");
    add (pid::sReso, "Reso");
    add (pid::sPan, "Pan");
    add (pid::sOut, "Out", true);
    setTooltip (juce::String::fromUTF8 ("Klicka på rubriken för att provlyssna (click the title to audition)"));
}

void VoiceStrip::paint (juce::Graphics& g)
{
    const auto& info = voiceInfo[(size_t) voice];
    // Rubrik: svenska överst, engelska under (remsorna är smala).
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    g.setColour (colours::hairline.withAlpha (0.22f));
    g.drawLine (r.getRight() + 2.5f, r.getY() + 6.0f, r.getRight() + 2.5f, r.getBottom() - 6.0f, 1.0f);

    auto head = r.removeFromTop (34.0f).reduced (6.0f, 2.0f);
    const auto lamp = head.removeFromRight (12.0f).withSizeKeepingCentre (10.0f, 10.0f);
    drawLamp (g, lamp, colours::lampRed, colours::lampRedOff, flash > 0.05f, 0.4f + 0.6f * flash);

    const auto sv = swedishName (voice);
    const auto en = juce::String::fromUTF8 (info.titleUtf8).fromFirstOccurrenceOf ("|", false, false).toUpperCase();
    float size = 13.0f;
    while (size > 9.0f && juce::GlyphArrangement::getStringWidth (panelFont (size, false).withExtraKerningFactor (0.12f), sv) > head.getWidth())
        size -= 0.5f;
    g.setFont (panelFont (size, false).withExtraKerningFactor (0.12f));
    g.setColour (colours::text);
    g.drawText (sv, head.removeFromTop (head.getHeight() * 0.55f), juce::Justification::bottomLeft);
    g.setFont (panelFont (9.5f, false).withExtraKerningFactor (0.14f));
    g.setColour (colours::dimText);
    g.drawText ("(" + en + ")", head, juce::Justification::topLeft);
}

void VoiceStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    r.removeFromTop (34);
    const int rowsN = 5;
    const int rowH = r.getHeight() / rowsN;
    for (int i = 0; i < (int) controls.size(); ++i)
    {
        const int row = i / 2, col = i % 2;
        const int w = r.getWidth() / 2;
        controls[(size_t) i]->setBounds (r.getX() + col * w, r.getY() + row * rowH, w, rowH);
    }
}

void VoiceStrip::mouseDown (const juce::MouseEvent& e)
{
    if (e.y < 38)
        processor.audition (voice);
}

void VoiceStrip::refresh()
{
    const auto h = processor.getHitCounter (voice);
    if (h != lastHits)
    {
        lastHits = h;
        flash = 1.0f;
        repaint (getLocalBounds().removeFromTop (40));
    }
    else if (flash > 0.0f)
    {
        flash = flash < 0.06f ? 0.0f : flash * 0.7f;
        repaint (getLocalBounds().removeFromTop (40));
    }
}

//==============================================================================
MidiDragSource::MidiDragSource (SlaggProcessor& p, uint32_t m, bool b) : processor (p), mask (m), big (b)
{
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    setTooltip (big ? juce::String::fromUTF8 ("Dra alla ljud till DAW:n – ett spår per ljud. Sekvensern slutar sedan spela dem. "
                                              "(Drag all sounds to your DAW – one track per sound.)")
                    : juce::String::fromUTF8 ("Dra det här ljudet till ett eget MIDI-spår. Sekvensern slutar sedan spela det. "
                                              "(Drag this sound to its own MIDI track.)"));
}

void MidiDragSource::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (big ? 2.0f : 1.5f);
    g.setColour (dragging ? colours::lampAmber.withAlpha (0.2f) : juce::Colours::black.withAlpha (0.25f));
    g.fillRect (r);
    drawEngravedBox (g, r, 0.8f);

    if (! big)
    {
        g.setColour (colours::text.withAlpha (0.85f));
        g.setFont (panelFont (12.0f));
        g.drawText (juce::String::fromUTF8 ("⇢"), r, juce::Justification::centred);
        return;
    }

    juce::Path border;
    border.addRectangle (r.reduced (5.0f));
    const float dashes[] { 7.0f, 4.0f };
    juce::Path dashed;
    juce::PathStrokeType (1.2f).createDashedStroke (dashed, border, dashes, 2);
    g.setColour (colours::text.withAlpha (0.45f));
    g.fillPath (dashed);

    const auto range = getRange ? getRange() : ExportRange {};
    auto t = r.reduced (10.0f);
    g.setColour (colours::text);
    g.setFont (panelFont (16.0f).withExtraKerningFactor (0.1f));
    g.drawText (juce::String::fromUTF8 ("⇢  DRA ALLA TILL DAW"), t.removeFromTop (t.getHeight() * 0.4f), juce::Justification::centredBottom);
    g.setColour (colours::dimText);
    g.setFont (panelFont (11.0f).withExtraKerningFactor (0.1f));
    g.drawText ("(DRAG ALL TO DAW)", t.removeFromTop (16.0f), juce::Justification::centred);
    g.setFont (panelFont (12.5f, false));
    g.drawFittedText (range.label, t.toNearestInt().withTrimmedTop (2), juce::Justification::centredTop, 3);
}

void MidiDragSource::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 5 || ! getRange)
        return;
    dragging = true;
    repaint();

    const auto range = getRange();
    juce::String name = "Slagg";
    for (int v = 0; v < dsp::numVoices && mask != 0xFFFFFFFFu; ++v)
        if (mask == (1u << v))
            name << " " << voiceInfo[(size_t) v].shortName;
    if (mask == 0xFFFFFFFFu)
        name << " (alla)";
    name << " " << range.barsText << ".mid";

    const auto file = processor.writeMidiExport (range.from, range.to, mask, name);
    if (! file.existsAsFile())
    {
        dragging = false;
        repaint();
        return;
    }

    auto* proc = &processor;
    const auto m = mask;
    juce::Component::SafePointer<MidiDragSource> safe (this);
    juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this, [proc, safe, m]
    {
        proc->voicesExported (m);
        if (safe != nullptr)
        {
            safe->dragging = false;
            safe->repaint();
        }
    });
}

void MidiDragSource::mouseUp (const juce::MouseEvent&)
{
    dragging = false;
    repaint();
}

//==============================================================================
StepGrid::StepGrid (SlaggProcessor& p) : processor (p)
{
    setRepaintsOnMouseActivity (false);
}

juce::Rectangle<float> StepGrid::cell (int v, int step) const
{
    const float w = (float) getWidth() / (float) seq::maxSteps;
    const float h = (float) rowHeight();
    return { (float) step * w, (float) v * h, w, h };
}

int StepGrid::stepAt (float x) const
{
    return juce::jlimit (0, seq::maxSteps - 1, (int) (x / ((float) getWidth() / (float) seq::maxSteps)));
}

void StepGrid::paint (juce::Graphics& g)
{
    auto& pat = processor.getPattern();
    const auto s = processor.currentSettings();
    const double ppq = processor.getCurrentPpq();

    for (int v = 0; v < dsp::numVoices; ++v)
    {
        const int len = juce::jlimit (1, seq::maxSteps, s.length[(size_t) v]);
        const bool enabled = s.enabled[(size_t) v];
        const int play = ppq >= 0.0 ? (int) (((int64_t) std::floor (ppq / s.stepPpq)) % len) : -1;

        for (int i = 0; i < seq::maxSteps; ++i)
        {
            const auto vi = (size_t) v, si = (size_t) i;
            const auto r = cell (v, i).reduced (1.2f);
            const bool inLane = i < len;
            const float alpha = (inLane ? 1.0f : 0.25f) * (enabled ? 1.0f : 0.45f);
            g.setColour (colours::bakelite.withAlpha ((i / 4) % 2 == 0 ? 0.8f : 0.55f));
            g.fillRect (r);
            drawEngravedBox (g, r, 0.5f);

            if (pat.on[vi][si].load())
            {
                const float vel = pat.velocity[vi][si].load();
                const float prob = pat.probability[vi][si].load();
                const int rat = pat.ratchet[vi][si].load();
                const float nudge = pat.nudge[vi][si].load();
                const auto lit = colours::lampRed.interpolatedWith (colours::lampAmber, 1.0f - vel);
                // Nudge flyttar slaget synligt åt sidan.
                auto bar = r.reduced (2.5f, 2.0f).translated (nudge * r.getWidth() * 0.6f, 0.0f);
                // Låg sannolikhet = blekare slag.
                const float probAlpha = 0.35f + 0.65f * prob;

                switch (mode)
                {
                    case EditMode::probability:
                    {
                        g.setColour (lit.withAlpha (0.18f * alpha));
                        g.fillRect (bar);
                        g.setColour (juce::Colour (0xff86b4d4).withAlpha (0.9f * alpha));
                        g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - prob)));
                        break;
                    }
                    case EditMode::ratchet:
                    {
                        g.setColour (lit.withAlpha (0.25f * alpha * probAlpha));
                        g.fillRect (bar);
                        g.setColour (lit.withAlpha (0.95f * alpha));
                        const float w = bar.getWidth() / (float) rat;
                        for (int k = 0; k < rat; ++k)
                            g.fillRect (juce::Rectangle<float> (bar.getX() + (float) k * w + 0.5f, bar.getY(), w - 1.5f, bar.getHeight()));
                        break;
                    }
                    case EditMode::nudge:
                    {
                        g.setColour (lit.withAlpha (0.3f * alpha * probAlpha));
                        g.fillRect (r.reduced (2.5f, 2.0f));
                        g.setColour (colours::text.withAlpha (0.95f * alpha));
                        const float x = r.getCentreX() + nudge * r.getWidth();
                        g.drawLine (x, r.getY() + 2.0f, x, r.getBottom() - 2.0f, 2.0f);
                        break;
                    }
                    case EditMode::velocity:
                    default:
                        g.setColour (lit.withAlpha (0.25f * alpha * probAlpha));
                        g.fillRect (bar);
                        g.setColour (lit.withAlpha (0.9f * alpha * probAlpha));
                        g.fillRect (bar.withTrimmedTop (bar.getHeight() * (1.0f - vel)));
                        break;
                }

                // Små markeringar som syns i alla lägen: prick = probability < 100 %, streck = ratchet.
                if (prob < 0.995f && mode != EditMode::probability)
                {
                    g.setColour (juce::Colour (0xff86b4d4).withAlpha (alpha));
                    g.fillEllipse (r.getRight() - 5.5f, r.getY() + 2.0f, 3.5f, 3.5f);
                }
                if (rat > 1 && mode != EditMode::ratchet)
                {
                    g.setColour (colours::text.withAlpha (0.8f * alpha));
                    for (int k = 0; k < rat; ++k)
                        g.fillRect (r.getX() + 2.0f + (float) k * 3.0f, r.getY() + 2.0f, 2.0f, 2.0f);
                }
            }
            if (i == play && inLane)
            {
                g.setColour (colours::text.withAlpha (0.85f));
                g.drawRect (r.expanded (0.5f), 1.2f);
            }
        }
    }
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    dragVoice = juce::jlimit (0, dsp::numVoices - 1, (int) (e.position.y / (float) rowHeight()));
    dragStep = lastPaintStep = stepAt (e.position.x);
    auto& pat = processor.getPattern();
    const auto vi = (size_t) dragVoice, si = (size_t) dragStep;
    wasOn = pat.on[vi][si].load();
    valueDrag = false;
    switch (mode)
    {
        case EditMode::probability: startValue = pat.probability[vi][si].load(); break;
        case EditMode::ratchet:     startValue = (float) pat.ratchet[vi][si].load(); break;
        case EditMode::nudge:       startValue = pat.nudge[vi][si].load(); break;
        case EditMode::velocity:
        default:                    startValue = pat.velocity[vi][si].load(); break;
    }
    if (! wasOn)
    {
        paintValue = true;
        pat.on[vi][si] = true;
        pat.velocity[vi][si] = 0.8f;
        pat.probability[vi][si] = 1.0f;
        pat.ratchet[vi][si] = 1;
        pat.nudge[vi][si] = 0.0f;
        markDirty (processor);
        repaint();
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragVoice < 0)
        return;
    auto& pat = processor.getPattern();
    const auto vi = (size_t) dragVoice, si = (size_t) dragStep;
    const float dy = (float) e.getDistanceFromDragStartY(), dx = (float) e.getDistanceFromDragStartX();

    // Dra på ett befintligt slag = ändra värdet i valt läge. Dra åt sidan från ett tomt = måla fler slag.
    if (wasOn && (valueDrag || std::abs (dy) > 3.0f || std::abs (dx) > 3.0f))
    {
        valueDrag = true;
        switch (mode)
        {
            case EditMode::probability: pat.probability[vi][si] = juce::jlimit (0.0f, 1.0f, startValue - dy / 80.0f); break;
            case EditMode::ratchet:     pat.ratchet[vi][si] = juce::jlimit (1, seq::maxRatchet, juce::roundToInt (startValue - dy / 14.0f)); break;
            case EditMode::nudge:       pat.nudge[vi][si] = juce::jlimit (-0.5f, 0.5f, startValue + (std::abs (dx) > std::abs (dy) ? dx : -dy) / 80.0f); break;
            case EditMode::velocity:
            default:                    pat.velocity[vi][si] = juce::jlimit (0.05f, 1.0f, startValue - dy / 80.0f); break;
        }
    }
    else if (! wasOn)
    {
        const int step = stepAt (e.position.x);
        if (step != lastPaintStep)
        {
            lastPaintStep = step;
            pat.on[vi][(size_t) step] = paintValue;
        }
    }
    markDirty (processor);
    repaint();
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    if (dragVoice >= 0 && wasOn && ! valueDrag && e.getDistanceFromDragStart() < 3 && e.getNumberOfClicks() < 2)
    {
        processor.getPattern().on[(size_t) dragVoice][(size_t) dragStep] = false;
        markDirty (processor);
        repaint();
    }
    dragVoice = -1;
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    // Dubbelklick: tillbaka till standard för valt läge (och slaget finns kvar).
    const int v = juce::jlimit (0, dsp::numVoices - 1, (int) (e.position.y / (float) rowHeight()));
    const auto vi = (size_t) v, si = (size_t) stepAt (e.position.x);
    auto& pat = processor.getPattern();
    pat.on[vi][si] = true;
    switch (mode)
    {
        case EditMode::probability: pat.probability[vi][si] = 1.0f; break;
        case EditMode::ratchet:     pat.ratchet[vi][si] = 1; break;
        case EditMode::nudge:       pat.nudge[vi][si] = 0.0f; break;
        case EditMode::velocity:
        default:                    pat.velocity[vi][si] = 0.8f; break;
    }
    markDirty (processor);
    repaint();
}

void StepGrid::refresh()
{
    const double ppq = processor.getCurrentPpq();
    const auto s = processor.currentSettings();
    int hash = 0;
    for (int v = 0; v < dsp::numVoices; ++v)
        hash = hash * 31 + s.length[(size_t) v] * 2 + (s.enabled[(size_t) v] ? 1 : 0);
    const int prog = processor.programChangeCounter.load();
    if (std::abs (ppq - lastPpq) > 1.0e-9 || hash != lastHash || prog != lastProgram)
    {
        lastPpq = ppq;
        lastHash = hash;
        lastProgram = prog;
        repaint();
    }
}

//==============================================================================
SeqPanel::SeqPanel (SlaggProcessor& p)
    : processor (p), grid (p), dragAll (p, 0xFFFFFFFFu, true)
{
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        auto& r = rows[(size_t) v];
        r.seq.setButtonText (voiceInfo[(size_t) v].shortName);
        r.seq.setClickingTogglesState (true);
        r.seq.setTooltip (juce::String::fromUTF8 ("Sekvensern spelar ljudet (lampa). Släcks efter MIDI-dragning – klicka för att slå på igen. "
                                                  "(Sequencer plays this sound.)"));
        addAndMakeVisible (r.seq);
        r.seqA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, pid::id (v, pid::sSeq), r.seq);

        r.mute.setClickingTogglesState (true);
        r.mute.setTooltip ("Mute");
        addAndMakeVisible (r.mute);
        r.muteA = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, pid::id (v, pid::sMute), r.mute);

        r.len.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 26, 18);
        r.len.setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        r.len.setTooltip (juce::String::fromUTF8 ("Antal steg för ljudet, 1–32 (steps – polymeter)"));
        addAndMakeVisible (r.len);
        r.lenA = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, pid::id (v, pid::sLen), r.len);

        r.drag = std::make_unique<MidiDragSource> (processor, 1u << v, false);
        r.drag->getRange = [this] { return computeRange(); };
        addAndMakeVisible (*r.drag);
    }

    addAndMakeVisible (grid);
    dragAll.getRange = [this] { return computeRange(); };
    addAndMakeVisible (dragAll);

    rangeMode.addItem (juce::String::fromUTF8 ("Längd: DAW:ns markörer (locators)"), 1);
    rangeMode.addItem (juce::String::fromUTF8 ("Längd: från/till takt (from/to bar)"), 2);
    addAndMakeVisible (rangeMode);
    for (auto* s : { &fromBar, &toBar })
    {
        s->setRange (1.0, 999.0, 1.0);
        s->setTextBoxStyle (juce::Slider::TextBoxLeft, false, 40, 20);
        s->setIncDecButtonsMode (juce::Slider::incDecButtonsDraggable_Vertical);
        s->onValueChange = [this] { dragAll.repaint(); };
        addAndMakeVisible (*s);
    }
    rangeMode.onChange = [this] { dragAll.repaint(); };

    auto& st = processor.apvts.state;
    if (! st.hasProperty ("exportRange")) st.setProperty ("exportRange", 1, nullptr);
    if (! st.hasProperty ("exportFrom"))  st.setProperty ("exportFrom", 1, nullptr);
    if (! st.hasProperty ("exportTo"))    st.setProperty ("exportTo", 5, nullptr);
    rangeMode.getSelectedIdAsValue().referTo (st.getPropertyAsValue ("exportRange", nullptr));
    fromBar.getValueObject().referTo (st.getPropertyAsValue ("exportFrom", nullptr));
    toBar.getValueObject().referTo (st.getPropertyAsValue ("exportTo", nullptr));

    for (auto* l : { &fromLabel, &toLabel })
    {
        l->setFont (panelFont (12.5f, false));
        l->setColour (juce::Label::textColourId, colours::text.withAlpha (0.9f));
        l->setJustificationType (juce::Justification::centredRight);
        addAndMakeVisible (*l);
    }
    fromLabel.setText (juce::String::fromUTF8 ("Från (From)"), juce::dontSendNotification);
    toLabel.setText ("Till (To)", juce::dontSendNotification);

    // Kit (färdiga ljud + mönster), grupperade per kategori.
    {
        const auto& kits = getKits();
        juce::StringArray cats;
        for (const auto& k : kits)
            cats.addIfNotAlreadyThere (k.category);
        for (const auto& c : cats)
        {
            kitBox.addSectionHeading (c);
            for (int i = 0; i < (int) kits.size(); ++i)
                if (kits[(size_t) i].category == c)
                    kitBox.addItem (kits[(size_t) i].name, i + 1);
        }
        kitBox.setTextWhenNothingSelected ("Kit");
        kitBox.setTooltip (juce::String::fromUTF8 ("Färdiga kit: ljud + mönster (factory kits). Utgångsvalen (Out) behålls."));
        kitBox.onChange = [this] { if (kitBox.getSelectedId() > 0) selectKit (kitBox.getSelectedId() - 1); };
        addAndMakeVisible (kitBox);
        prevKit.onClick = [this] { selectKit (juce::jmax (0, processor.getCurrentProgram() - 1)); };
        nextKit.onClick = [this] { selectKit (juce::jmin (processor.getNumPrograms() - 1, processor.getCurrentProgram() + 1)); };
        addAndMakeVisible (prevKit);
        addAndMakeVisible (nextKit);
    }

    // Redigeringsläge för rutnätet.
    {
        const char* names[] { "Velocity", "Prob", "Ratchet", "Nudge" };
        const char* tips[] {
            "Dra upp/ner på ett slag: styrka (velocity). Dubbelklick = 80 %.",
            "Dra upp/ner: sannolikhet att slaget spelas (probability). Dubbelklick = 100 %.",
            "Dra upp/ner: 1–4 snabba slag i steget (ratchet). Dubbelklick = 1.",
            "Dra åt sidan: lite före/efter takten (nudge). Dubbelklick = i takt." };
        for (int i = 0; i < 4; ++i)
        {
            auto& b = modeButtons[(size_t) i];
            b.setButtonText (names[i]);
            b.setTooltip (juce::String::fromUTF8 (tips[i]));
            b.setClickingTogglesState (true);
            b.setRadioGroupId (4711);
            b.onClick = [this, i] { grid.mode = (EditMode) i; grid.repaint(); };
            addAndMakeVisible (b);
        }
        modeButtons[0].setToggleState (true, juce::dontSendNotification);
    }

    // Groove och Låt (song): rattarna ovanför rutnätet.
    grooveControls.push_back (std::make_unique<Control> (processor.apvts, pid::rate, "Rate", colours::accent2, true));
    grooveControls.push_back (std::make_unique<Control> (processor.apvts, pid::swing, "Swing", colours::accent2, false));
    grooveControls.push_back (std::make_unique<Control> (processor.apvts, pid::humanize, "Humanize", colours::accent2, false));
    songControls.push_back (std::make_unique<Control> (processor.apvts, pid::evolve, "Evolve", colours::accent2, false));
    songControls.push_back (std::make_unique<Control> (processor.apvts, pid::fills, "Fills", colours::accent2, false));
    songControls.push_back (std::make_unique<Control> (processor.apvts, pid::phrase, "Phrase", colours::accent2, true));
    for (auto* list : { &grooveControls, &songControls })
        for (auto& c : *list)
            addAndMakeVisible (*c);

    rerollButton.setTooltip (juce::String::fromUTF8 ("Slå om tärningen: ny variant av probability och humanize (new random variation)"));
    rerollButton.onClick = [this] { processor.reroll(); markDirty (processor); grid.repaint(); };
    addAndMakeVisible (rerollButton);

    allOn.setTooltip (juce::String::fromUTF8 ("Låt sekvensern spela alla ljud igen (re-enable all)"));
    allOn.onClick = [this]
    {
        for (int v = 0; v < dsp::numVoices; ++v)
            if (auto* prm = processor.apvts.getParameter (pid::id (v, pid::sSeq)))
                prm->setValueNotifyingHost (1.0f);
    };
    clearButton.setTooltip (juce::String::fromUTF8 ("Töm mönstret (clear pattern)"));
    clearButton.onClick = [this] { processor.getPattern().clear(); markDirty (processor); grid.repaint(); };
    addAndMakeVisible (allOn);
    addAndMakeVisible (clearButton);
}

void SeqPanel::selectKit (int index)
{
    processor.setCurrentProgram (index);
    kitBox.setSelectedId (index + 1, juce::dontSendNotification);
    grid.repaint();
}

ExportRange SeqPanel::computeRange() const
{
    const auto host = processor.getHostInfo();
    const double barPpq = host.timeSigNum * 4.0 / host.timeSigDen;
    ExportRange r;
    juce::String prefix;
    if (rangeMode.getSelectedId() != 2 && host.hasLoop)
    {
        r.from = host.loopStart;
        r.to = host.loopEnd;
        prefix = juce::String::fromUTF8 ("Markörer (locators): ");
    }
    else
    {
        const double a = fromBar.getValue();
        const double b = std::max (a + 1.0, toBar.getValue());
        r.from = (a - 1.0) * barPpq;
        r.to = (b - 1.0) * barPpq;
        if (rangeMode.getSelectedId() != 2)
            prefix = juce::String::fromUTF8 ("Inga markörer (no locators) – Från/Till används. ");
    }
    const auto a = formatBar (r.from, barPpq), b = formatBar (r.to, barPpq);
    r.barsText = juce::String::fromUTF8 ("takt ") + a + "-" + b;
    r.label = prefix + juce::String::fromUTF8 ("Takt ") + a + juce::String::fromUTF8 ("–") + b;
    return r;
}

void SeqPanel::paint (juce::Graphics& g)
{
    drawSectionFrame (g, getLocalBounds().toFloat().reduced (3.0f), juce::String::fromUTF8 ("SEKVENSERARE|Sequencer"));

    // LÅT (Song): ram och rubrik.
    if (! songArea.isEmpty())
    {
        const auto a = songArea.toFloat();
        g.setColour (colours::hairline.withAlpha (0.35f));
        g.drawRoundedRectangle (a.reduced (1.0f), 3.0f, 1.0f);
        auto t = a.withWidth (100.0f).reduced (10.0f, 8.0f);
        g.setFont (panelFont (15.0f, false).withExtraKerningFactor (0.2f));
        g.setColour (colours::text);
        g.drawText (juce::String::fromUTF8 ("LÅT"), t.removeFromTop (t.getHeight() * 0.5f), juce::Justification::bottomLeft);
        g.setFont (panelFont (10.0f, false).withExtraKerningFactor (0.15f));
        g.setColour (colours::dimText);
        g.drawText ("(SONG)", t, juce::Justification::topLeft);
    }

    // Stegnummer ovanför rutnätet.
    const auto gb = grid.getBounds().toFloat();
    g.setFont (panelFont (10.0f, false));
    for (int i = 0; i < seq::maxSteps; i += 4)
    {
        const auto c = grid.cell (0, i).translated (gb.getX(), gb.getY() - 14.0f).withHeight (13.0f);
        g.setColour (colours::dimText);
        g.drawText (juce::String (i + 1), c, juce::Justification::centredLeft);
    }
}

void SeqPanel::resized()
{
    auto r = getLocalBounds().reduced (10, 6);
    r.removeFromTop (22);

    auto right = r.removeFromRight (250);
    r.removeFromRight (10);

    // Rattrad: Rate, Swing, Humanize | LÅT: Evolve, Fills, Phrase.
    {
        auto row = r.removeFromTop (58);
        grooveControls[0]->setBounds (row.removeFromLeft (96));
        for (size_t i = 1; i < grooveControls.size(); ++i)
            grooveControls[i]->setBounds (row.removeFromLeft (70));
        row.removeFromLeft (20);
        songArea = row.removeFromLeft (100 + 70 * 2 + 96).reduced (0, 1);
        auto sa = songArea;
        sa.removeFromLeft (100);
        songControls[0]->setBounds (sa.removeFromLeft (70));
        songControls[1]->setBounds (sa.removeFromLeft (70));
        songControls[2]->setBounds (sa);
    }
    r.removeFromTop (4);
    r.removeFromTop (14);   // stegnummer

    auto left = r.removeFromLeft (210);
    grid.setBounds (r);
    const int rowH = grid.rowHeight();
    for (int v = 0; v < dsp::numVoices; ++v)
    {
        auto line = juce::Rectangle<int> (left.getX(), r.getY() + v * rowH, left.getWidth(), rowH).reduced (0, 1);
        auto& row = rows[(size_t) v];
        row.drag->setBounds (line.removeFromRight (26));
        line.removeFromRight (4);
        row.len.setBounds (line.removeFromRight (60));
        line.removeFromRight (3);
        row.mute.setBounds (line.removeFromRight (40));
        line.removeFromRight (3);
        row.seq.setBounds (line);
    }

    {
        auto row = right.removeFromTop (24);
        nextKit.setBounds (row.removeFromRight (26));
        row.removeFromRight (2);
        prevKit.setBounds (row.removeFromRight (26));
        row.removeFromRight (4);
        kitBox.setBounds (row);
    }
    right.removeFromTop (5);
    {
        auto row = right.removeFromTop (24);
        const int w = row.getWidth() / 4;
        for (auto& b : modeButtons)
            b.setBounds (row.removeFromLeft (w).reduced (1, 0));
    }
    right.removeFromTop (5);
    rangeMode.setBounds (right.removeFromTop (24));
    right.removeFromTop (4);
    auto bars = right.removeFromTop (24);
    fromLabel.setBounds (bars.removeFromLeft (64));
    fromBar.setBounds (bars.removeFromLeft (84).reduced (2, 0));
    toLabel.setBounds (bars.removeFromLeft (46));
    toBar.setBounds (bars.reduced (2, 0));
    auto buttons = right.removeFromBottom (26);
    const int bw = buttons.getWidth() / 3;
    allOn.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
    rerollButton.setBounds (buttons.removeFromLeft (bw).reduced (1, 0));
    clearButton.setBounds (buttons.reduced (1, 0));
    right.removeFromBottom (5);
    right.removeFromTop (5);
    dragAll.setBounds (right);
}

void SeqPanel::refresh()
{
    grid.refresh();
    if (const int prog = processor.programChangeCounter.load(); prog != lastProgram)
    {
        lastProgram = prog;
        kitBox.setSelectedId (processor.getCurrentProgram() + 1, juce::dontSendNotification);
    }
    const auto range = computeRange();
    if (range.label != lastLabel)
    {
        lastLabel = range.label;
        dragAll.repaint();
    }
}
} // namespace slagg::ui

//==============================================================================
SlaggEditor::SlaggEditor (SlaggProcessor& p) : AudioProcessorEditor (&p), processor (p), seqPanel (p)
{
    using namespace slagg::ui;
    setLookAndFeel (&lnf);

    for (int v = 0; v < dsp::numVoices; ++v)
    {
        strips.push_back (std::make_unique<VoiceStrip> (p, v));
        content.addAndMakeVisible (*strips.back());
    }
    globals.push_back (std::make_unique<Control> (p.apvts, pid::master, "Volume", colours::accent2, false));
    for (auto& c : globals)
        content.addAndMakeVisible (*c);
    // Muller (rumble): kicken in i en mörk efterklang, lågpass, mättning och ducking.
    rumbleControls.push_back (std::make_unique<Control> (p.apvts, pid::rumbleAmt, "Amount", colours::accent2, false));
    rumbleControls.push_back (std::make_unique<Control> (p.apvts, pid::rumbleDecay, "Decay", colours::accent2, false));
    rumbleControls.push_back (std::make_unique<Control> (p.apvts, pid::rumbleTone, "Tone", colours::accent2, false));
    rumbleControls.push_back (std::make_unique<Control> (p.apvts, pid::rumbleDrive, "Drive", colours::accent2, false));
    rumbleControls.push_back (std::make_unique<Control> (p.apvts, pid::rumbleDuck, "Duck", colours::accent2, false));
    for (auto& c : rumbleControls)
        content.addAndMakeVisible (*c);
    content.addAndMakeVisible (seqPanel);
    brandHotspot.setTooltip ("falk elektro  ·  Slagg v" SLAGG_VERSION);
    content.addAndMakeVisible (brandHotspot);
    addAndMakeVisible (content);
    content.setInterceptsMouseClicks (false, true);

    // Panelerna skapades före setLookAndFeel: bygg om alla barn med vårt skin.
    sendLookAndFeelChange();

    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio ((double) baseWidth / (double) baseHeight);
    setResizeLimits (baseWidth / 2, baseHeight / 2, baseWidth * 2, baseHeight * 2);
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

SlaggEditor::~SlaggEditor()
{
    setLookAndFeel (nullptr);
}

void SlaggEditor::paint (juce::Graphics& g)
{
    using namespace slagg::ui;
    g.fillAll (juce::Colour (0xff0b0c0d));
    g.addTransform (content.getTransform());
    if (panelTexture.isNull())
        panelTexture = makePanelTexture (innerW, baseHeight);

    auto unit = [&] (int y, int h)
    {
        g.drawImage (panelTexture, earW, y, innerW, h, 0, y, innerW, h);
        g.setColour (colours::engraveDark);
        g.drawRect (juce::Rectangle<int> (earW, y, innerW, h), 1);
        drawRackEar (g, juce::Rectangle<float> (0.0f, (float) y, (float) earW, (float) h));
        drawRackEar (g, juce::Rectangle<float> ((float) (earW + innerW), (float) y, (float) earW, (float) h));
        juce::Graphics::ScopedSaveState save (g);
        const auto area = juce::Rectangle<float> (0.0f, (float) y, (float) baseWidth, (float) h);
        g.reduceClipRegion (area.toNearestInt());
        drawSpotLight (g, area, { (float) baseWidth * 0.46f, (float) y + (float) h * 0.32f });
    };
    unit (0, voicesH);
    unit (voicesH + unitGap, seqH);

    // falk elektro längst upp till vänster, sedan produktnamnet. Versionen står undangömt nere i hörnet
    // (och i loggans tooltip).
    drawBrandHeader (g, juce::Rectangle<float> ((float) earW + 20.0f, 8.0f, 500.0f, 44.0f), "SLAGG");
    g.setFont (panelFont (9.0f, false));
    g.setColour (colours::dimText.withAlpha (0.35f));
    g.drawText ("v" SLAGG_VERSION, juce::Rectangle<float> ((float) (earW + innerW) - 70.0f, (float) (voicesH + unitGap + seqH) - 14.0f, 60.0f, 12.0f),
                juce::Justification::centredRight);

    // Muller-rubriken med en tunn ram runt rattarna.
    if (! rumbleArea.isEmpty())
    {
        const auto ra = rumbleArea.toFloat().translated (0.0f, 0.0f);
        g.setColour (colours::hairline.withAlpha (0.35f));
        g.drawRoundedRectangle (ra.reduced (1.0f), 3.0f, 1.0f);
        auto t = ra.withWidth (96.0f).reduced (10.0f, 6.0f);
        g.setFont (panelFont (15.0f, false).withExtraKerningFactor (0.2f));
        g.setColour (colours::text);
        g.drawText ("MULLER", t.removeFromTop (t.getHeight() * 0.55f), juce::Justification::bottomLeft);
        g.setFont (panelFont (10.0f, false).withExtraKerningFactor (0.15f));
        g.setColour (colours::dimText);
        g.drawText ("(RUMBLE)", t, juce::Justification::topLeft);
    }
}

void SlaggEditor::paintOverChildren (juce::Graphics& g)
{
    using namespace slagg::ui;
    g.addTransform (content.getTransform());
    if (grimeOverlay.isNull())
        grimeOverlay = makeGrimeOverlay (baseWidth, baseHeight);
    g.setOpacity (0.4f);
    g.drawImageAt (grimeOverlay, 0, 0);
    g.setOpacity (1.0f);
    auto light = [&] (int y, int h)
    {
        juce::Graphics::ScopedSaveState save (g);
        const auto area = juce::Rectangle<float> (0.0f, (float) y, (float) baseWidth, (float) h);
        g.reduceClipRegion (area.toNearestInt());
        g.setOpacity (0.1f);
        drawSpotLight (g, area, { (float) baseWidth * 0.46f, (float) y + (float) h * 0.32f });
    };
    light (0, voicesH);
    light (voicesH + unitGap, seqH);
}

void SlaggEditor::resized()
{
    const float scale = (float) getWidth() / (float) baseWidth;
    content.setBounds (0, 0, baseWidth, baseHeight);
    content.setTransform (juce::AffineTransform::scale (scale));

    brandHotspot.setBounds (earW + 20, 8, 300, 44);
    auto top = juce::Rectangle<int> (earW, 0, innerW, voicesH).reduced (12, 6);
    auto header = top.removeFromTop (56);
    header.removeFromLeft (400);
    for (auto it = globals.rbegin(); it != globals.rend(); ++it)
        (*it)->setBounds (header.removeFromRight (70));
    header.removeFromRight (24);
    rumbleArea = header.removeFromRight (64 * (int) rumbleControls.size() + 96);
    {
        auto r = rumbleArea;
        r.removeFromLeft (96);   // rubrik
        for (auto& c : rumbleControls)
            c->setBounds (r.removeFromLeft (64));
    }

    const int w = top.getWidth() / dsp::numVoices;
    for (auto& s : strips)
        s->setBounds (top.removeFromLeft (w));

    seqPanel.setBounds (juce::Rectangle<int> (earW, voicesH + unitGap, innerW, seqH).reduced (12, 6));
}

void SlaggEditor::timerCallback()
{
    for (auto& s : strips)
        s->refresh();
    seqPanel.refresh();
}
