#include "PluginEditor.h"
#include "Params.h"
#include "ui/Theme.h"

using namespace gizmo;

namespace
{
void placeRow (std::initializer_list<juce::Component*> components, juce::Rectangle<int> area)
{
    const auto width = area.getWidth() / (int) components.size();

    for (auto* c : components)
        c->setBounds (area.removeFromLeft (width));
}
} // namespace

GizmoEditor::Content::Content (GizmoProcessor& p)
    : processor (p),
      viewA ("A", theme::accentA, pl ("Przeciągnij tu sampel albo kliknij, żeby wybrać plik")),
      viewB ("B", theme::accentB, pl ("Upuść tu sampel, żeby B miał własny")),
      importMode (juce::StringArray (pl ("Cykle"), pl ("Kawałki")), theme::accentA),
      character (juce::StringArray (pl ("Czysto"), pl ("Surowo")), theme::accentA),
      voiceMode (juce::StringArray ("Poly", "Mono"), theme::accentVoice),
      frameMode (juce::StringArray (pl ("Płynnie"), pl ("Skoki")), theme::accentA),
      filterOn (juce::StringArray (pl ("Wył."), pl ("Wł.")), theme::accentFilter),
      lfoShape (juce::StringArray ("Sin", "Tri", pl ("Piła"), "Kw.", "Los"), theme::accentLfo),
      lfoSync (juce::StringArray ("Hz", "Tempo"), theme::accentLfo),
      lfoRetrig (juce::StringArray (pl ("Wspólne"), pl ("Od nuty")), theme::accentLfo),
      position (p.apvts, ParamID::position, pl ("Pozycja"), theme::accentA, "I"),
      velToPos (p.apvts, ParamID::velToPos, pl ("Velocity"), theme::accentA),
      wheelToPos (p.apvts, ParamID::wheelToPos, pl ("Kółko"), theme::accentA),
      levelA (p.apvts, ParamID::levelA, pl ("Poziom A"), theme::accentA),
      panA (p.apvts, ParamID::panA, pl ("Pan A"), theme::accentA),
      levelB (p.apvts, ParamID::levelB, pl ("Poziom B"), theme::accentB),
      panB (p.apvts, ParamID::panB, pl ("Pan B"), theme::accentB),
      semiB (p.apvts, ParamID::semiB, pl ("Transp. B"), theme::accentB),
      offsetB (p.apvts, ParamID::offsetB, pl ("Przesun. B"), theme::accentB),
      detune (p.apvts, ParamID::detune, pl ("Rozstrojenie"), theme::accentA, "M"),
      glide (p.apvts, ParamID::glide, pl ("Glide"), theme::accentVoice),
      bendRange (p.apvts, ParamID::bendRange, pl ("Bend"), theme::accentVoice),
      gain (p.apvts, ParamID::gain, pl ("Głośność"), theme::accentVoice),
      cutoff (p.apvts, ParamID::cutoff, pl ("Cutoff"), theme::accentFilter, "Z"),
      resonance (p.apvts, ParamID::resonance, pl ("Rezonans"), theme::accentFilter),
      keyTrack (p.apvts, ParamID::keyTrack, pl ("Klawiatura"), theme::accentFilter),
      filterEnv (p.apvts, ParamID::filterEnv, pl ("Obwiednia"), theme::accentFilter),
      fAttack (p.apvts, ParamID::fAttack, pl ("Attack"), theme::accentFilter),
      fDecay (p.apvts, ParamID::fDecay, pl ("Decay"), theme::accentFilter),
      fSustain (p.apvts, ParamID::fSustain, pl ("Sustain"), theme::accentFilter),
      fRelease (p.apvts, ParamID::fRelease, pl ("Release"), theme::accentFilter),
      attack (p.apvts, ParamID::attack, pl ("Attack"), theme::accentA),
      decay (p.apvts, ParamID::decay, pl ("Decay"), theme::accentA),
      sustain (p.apvts, ParamID::sustain, pl ("Sustain"), theme::accentA),
      release (p.apvts, ParamID::release, pl ("Release"), theme::accentA),
      velToAmp (p.apvts, ParamID::velToAmp, pl ("Velocity"), theme::accentA),
      lfoRate (p.apvts, ParamID::lfoRate, pl ("Szybkość"), theme::accentLfo),
      lfoDivision (p.apvts, ParamID::lfoDivision, pl ("Podział"), theme::accentLfo),
      lfoToPos (p.apvts, ParamID::lfoToPos, pl ("Pozycja"), theme::accentLfo),
      lfoToCutoff (p.apvts, ParamID::lfoToCutoff, pl ("Cutoff"), theme::accentLfo),
      lfoToPitch (p.apvts, ParamID::lfoToPitch, pl ("Wysokość"), theme::accentLfo)
{
    auto attach = [&p] (Segmented& s, const char* id)
    {
        if (auto* parameter = p.apvts.getParameter (id))
            s.attach (*parameter);
    };

    attach (character, ParamID::character);
    attach (voiceMode, ParamID::voiceMode);
    attach (frameMode, ParamID::frameMode);
    attach (filterOn, ParamID::filterOn);
    attach (lfoShape, ParamID::lfoShape);
    attach (lfoSync, ParamID::lfoSync);
    attach (lfoRetrig, ParamID::lfoRetrig);

    for (auto* c : std::initializer_list<juce::Component*> {
             &viewA, &viewB, &importMode, &character, &voiceMode, &frameMode, &filterOn, &lfoShape, &lfoSync,
             &lfoRetrig, &position, &velToPos, &wheelToPos, &levelA, &panA, &levelB, &panB, &semiB, &offsetB,
             &detune, &glide, &bendRange, &gain, &cutoff, &resonance, &keyTrack, &filterEnv, &fAttack, &fDecay,
             &fSustain, &fRelease, &attack, &decay, &sustain, &release, &velToAmp, &lfoRate, &lfoDivision,
             &lfoToPos, &lfoToCutoff, &lfoToPitch })
        addAndMakeVisible (c);

    lfoDivision.setVisible (false);

    sections = {
        { pl ("Skanowanie"), { 540, 60, 200, 256 }, theme::accentA },
        { pl ("Oscylatory"), { 16, 328, 656, 120 }, theme::accentA },
        { pl ("Głos"), { 684, 328, 300, 120 }, theme::accentVoice },
        { pl ("Filtr"), { 16, 460, 968, 120 }, theme::accentFilter },
        { pl ("Obwiednia głośności"), { 16, 592, 480, 120 }, theme::accentA },
        { pl ("LFO"), { 508, 592, 476, 120 }, theme::accentLfo },
    };
}

void GizmoEditor::Content::resized()
{
    // Header
    importMode.setBounds (418, 20, 150, 26);
    character.setBounds (656, 20, 150, 26);
    voiceMode.setBounds (874, 20, 110, 26);

    // Row 1: tables and position
    viewA.setBounds (16, 60, 512, 256);
    viewB.setBounds (752, 60, 232, 256);
    position.setBounds (565, 82, 150, 124);
    frameMode.setBounds (556, 210, 168, 22);
    placeRow ({ &velToPos, &wheelToPos }, { 548, 238, 184, 74 });

    // Row 2: oscillators and voice
    placeRow ({ &levelA, &panA, &levelB, &panB, &semiB, &offsetB, &detune }, { 24, 352, 640, 92 });
    placeRow ({ &glide, &bendRange, &gain }, { 692, 352, 284, 92 });

    // Row 3: filter
    filterOn.setBounds (900, 466, 76, 20);
    placeRow ({ &cutoff, &resonance, &keyTrack, &filterEnv }, { 24, 484, 476, 92 });
    placeRow ({ &fAttack, &fDecay, &fSustain, &fRelease }, { 500, 484, 476, 92 });

    // Row 4: amplitude envelope and LFO
    placeRow ({ &attack, &decay, &sustain, &release, &velToAmp }, { 24, 616, 464, 92 });
    lfoShape.setBounds (640, 597, 336, 20);
    lfoSync.setBounds (516, 630, 120, 22);
    lfoRetrig.setBounds (516, 662, 120, 22);
    lfoRate.setBounds (644, 616, 83, 92);
    lfoDivision.setBounds (644, 616, 83, 92);
    placeRow ({ &lfoToPos, &lfoToCutoff, &lfoToPitch }, { 727, 616, 249, 92 });
}

void GizmoEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);

    // Logo
    g.setColour (theme::accentA);
    g.setFont (theme::font (30.0f, true));
    g.drawText ("gizmo", juce::Rectangle<int> (16, 12, 110, 40), juce::Justification::centredLeft, false);
    g.setColour (theme::textDim);
    g.setFont (theme::font (13.0f));
    g.drawText (pl ("transwave z twoich sampli"), juce::Rectangle<int> (118, 22, 220, 24),
                juce::Justification::centredLeft, false);

    // Header labels
    g.setFont (theme::font (12.0f));
    g.drawText (pl ("Cięcie"), juce::Rectangle<int> (352, 20, 60, 26), juce::Justification::centredRight, false);
    g.drawText (pl ("Brzmienie"), juce::Rectangle<int> (576, 20, 74, 26), juce::Justification::centredRight, false);
    g.drawText (pl ("Głosy"), juce::Rectangle<int> (812, 20, 56, 26), juce::Justification::centredRight, false);

    for (const auto& s : sections)
    {
        const auto r = s.bounds.toFloat();
        g.setColour (theme::panel);
        g.fillRoundedRectangle (r, 10.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), 10.0f, 1.0f);

        g.setColour (s.accent);
        g.setFont (theme::font (12.5f, true));
        g.drawText (s.title, s.bounds.reduced (14, 6).removeFromTop (16), juce::Justification::centredLeft, false);
    }

    // Filter envelope group
    g.setColour (theme::panelEdge);
    g.drawVerticalLine (500, 484.0f, 572.0f);
    g.setColour (theme::textDim);
    g.setFont (theme::font (11.5f));
    g.drawText (pl ("obwiednia filtra"), juce::Rectangle<int> (512, 466, 200, 16), juce::Justification::centredLeft, false);
}

//==============================================================================
GizmoEditor::GizmoEditor (GizmoProcessor& p)
    : AudioProcessorEditor (p), processor (p), content (p)
{
    setLookAndFeel (&lookAndFeel);
    addAndMakeVisible (content);

    content.viewA.onBrowse = [this] { browseFor (GizmoProcessor::slotA); };
    content.viewB.onBrowse = [this] { browseFor (GizmoProcessor::slotB); };
    content.viewB.onClear = [this] { processor.clearSlotB(); };
    content.importMode.onChange = [this] (int index)
    {
        processor.setImportMode (index == 1 ? ImportMode::chunks : ImportMode::cycles);
    };

    processor.addChangeListener (this);
    refreshTables();

    setResizable (true, true);
    setResizeLimits (700, 510, 1500, 1092);
    getConstrainer()->setFixedAspectRatio ((double) kDesignWidth / (double) kDesignHeight);
    setSize (900, 655);
    startTimerHz (30);
}

GizmoEditor::~GizmoEditor()
{
    stopTimer();
    processor.removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void GizmoEditor::paint (juce::Graphics& g)
{
    g.fillAll (theme::background);
}

void GizmoEditor::resized()
{
    const auto scale = (float) getWidth() / (float) kDesignWidth;
    content.setTransform (juce::AffineTransform::scale (scale));
    content.setBounds (0, 0, kDesignWidth, kDesignHeight);
}

void GizmoEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refreshTables();
}

void GizmoEditor::refreshTables()
{
    const auto* a = processor.getTable (GizmoProcessor::slotA);
    const auto* b = processor.getTable (GizmoProcessor::slotB);

    content.viewA.setTable (a, false);
    content.viewB.setTable (b != nullptr ? b : a, b == nullptr);
    content.viewA.setStatus (processor.isLoading (GizmoProcessor::slotA), processor.getError (GizmoProcessor::slotA));
    content.viewB.setStatus (processor.isLoading (GizmoProcessor::slotB), processor.getError (GizmoProcessor::slotB));
    content.importMode.setSelected (processor.getImportMode() == ImportMode::chunks ? 1 : 0);
}

void GizmoEditor::timerCallback()
{
    updateDisplay();
}

void GizmoEditor::updateDisplay()
{
    const auto& engine = processor.getEngine();
    auto value = [this] (const char* id) { return processor.apvts.getRawParameterValue (id)->load(); };

    const auto pos = engine.getDisplayPosition();
    const auto offsetB = value (ParamID::offsetB);
    const auto stepped = value (ParamID::frameMode) > 0.5f;

    content.viewA.setPosition (pos, stepped);
    content.viewB.setPosition (juce::jlimit (0.0f, 1.0f, pos + offsetB), stepped);

    std::array<float, WavetableView::kMaxMarkers> markersA {}, markersB {};

    for (int i = 0; i < WavetableView::kMaxMarkers; ++i)
    {
        const auto v = i < SynthEngine::kMaxVoices ? engine.getVoicePosition (i) : -1.0f;
        markersA[(size_t) i] = v;
        markersB[(size_t) i] = v < 0.0f ? -1.0f : juce::jlimit (0.0f, 1.0f, v + offsetB);
    }

    content.viewA.setVoicePositions (markersA);
    content.viewB.setVoicePositions (markersB);

    const auto synced = value (ParamID::lfoSync) > 0.5f;
    content.lfoRate.setVisible (! synced);
    content.lfoDivision.setVisible (synced);
}

void GizmoEditor::browseFor (GizmoProcessor::Slot slot)
{
    chooser = std::make_unique<juce::FileChooser> (pl ("Wybierz sampel"), juce::File(), processor.getSupportedWildcard());
    juce::Component::SafePointer<GizmoEditor> safe (this);

    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [safe, slot] (const juce::FileChooser& fc)
                          {
                              if (auto* self = safe.getComponent())
                              {
                                  const auto file = fc.getResult();

                                  if (file.existsAsFile())
                                      self->processor.loadFile (slot, file);
                              }
                          });
}

//==============================================================================
bool GizmoEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (const auto& f : files)
        if (processor.isSupportedFile (juce::File (f)))
            return true;

    return false;
}

GizmoProcessor::Slot GizmoEditor::slotAt (int x, int y)
{
    const auto p = content.getLocalPoint (this, juce::Point<int> (x, y));
    return content.viewB.getBounds().contains (p) ? GizmoProcessor::slotB : GizmoProcessor::slotA;
}

void GizmoEditor::updateDragHover (int x, int y)
{
    const auto slot = slotAt (x, y);
    content.viewA.setDragHover (slot == GizmoProcessor::slotA);
    content.viewB.setDragHover (slot == GizmoProcessor::slotB);
}

void GizmoEditor::fileDragEnter (const juce::StringArray&, int x, int y) { updateDragHover (x, y); }
void GizmoEditor::fileDragMove (const juce::StringArray&, int x, int y) { updateDragHover (x, y); }

void GizmoEditor::fileDragExit (const juce::StringArray&)
{
    content.viewA.setDragHover (false);
    content.viewB.setDragHover (false);
}

void GizmoEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    fileDragExit (files);

    for (const auto& f : files)
    {
        const juce::File file (f);

        if (processor.isSupportedFile (file))
        {
            processor.loadFile (slotAt (x, y), file);
            return;
        }
    }
}
