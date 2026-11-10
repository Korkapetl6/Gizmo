// Prints generated solos as text, for reading them like a tab. Usage: SolistaDump [style] [seed] [bars] [minor 0/1] [progression] [shuffle 0/1] [bpm]
#include "SoloGenerator.h"

#include <iostream>

using namespace solista;

int main (int argc, char** argv)
{
    GeneratorParams p;
    p.style = argc > 1 ? atoi (argv[1]) : 0;
    p.seed = (juce::uint32) (argc > 2 ? atoi (argv[2]) : 1);
    p.bars = argc > 3 ? atoi (argv[3]) : 8;
    p.minorKey = argc > 4 ? atoi (argv[4]) != 0 : true;
    p.progression = argc > 5 ? atoi (argv[5]) : 0;
    p.shuffle = argc > 6 ? atoi (argv[6]) != 0 : false;
    p.bpm = argc > 7 ? atof (argv[7]) : 100.0;
    p.keyPc = argc > 8 ? atoi (argv[8]) : 9;

    const auto solo = generateSolo (p);
    static const char* names[] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "Bb", "B" };
    static const char* degrees[] = { "1", "b2", "2", "b3", "3", "4", "b5", "5", "b6", "6", "b7", "7" };
    static const char* kinds[] = { "", "bend", "bend+release", "prebend", "bend2stage", "curl" };
    int bar = -1, bends = 0, vibratos = 0, offbeat = 0;

    for (const auto& n : solo.notes)
    {
        const auto b = (int) std::floor (n.start / 4.0);

        if (b != bar)
        {
            bar = b;
            std::cout << "--- takt " << bar + 1 << "  [" << solo.chords[(size_t) std::min (bar, (int) solo.chords.size() - 1)].getName() << "]" << std::endl;
        }

        const auto beat = n.start - 4.0 * bar;
        const auto degree = ((n.pitch - solo.keyPc) % 12 + 12) % 12;
        const auto& a = n.art;
        juce::String line;
        line << juce::String (beat + 1.0, 2).paddedLeft (' ', 5) << "  " << juce::String (names[n.pitch % 12]).paddedRight (' ', 2) << n.pitch / 12 - 1
             << " (" << juce::String (degrees[degree]).paddedRight (' ', 2) << ") len " << juce::String (n.length, 2).paddedLeft (' ', 5)
             << " vel " << juce::String (n.velocity).paddedLeft (' ', 3) << (n.legato ? " leg" : "    ") << (n.chordNote ? " +chord" : "");

        if (a.bend != BendKind::none)
        {
            line << "  " << kinds[(int) a.bend] << " +" << juce::String (a.bendSemis, 1) << " in " << juce::String (a.bendTime, 2) << "s";
            ++bends;
        }

        if (a.slideIn != 0.0f) line << "  slide " << juce::String (a.slideIn, 0);
        if (a.fallOff != 0.0f) line << "  fall";

        if (a.vibrato.depth > 0.0f)
        {
            line << "  vib " << juce::roundToInt (a.vibrato.depth * 100.0f) << "c " << juce::String (a.vibrato.rateHz, 1) << "Hz after "
                 << juce::String (a.vibrato.delay, 2) << "s" << (a.vibrato.swell > 0.3f ? " swell" : "");
            ++vibratos;
        }

        const auto frac = beat - std::floor (beat);
        if (frac > 0.1 && frac < 0.9) ++offbeat;
        std::cout << line << std::endl;
    }

    std::cout << "== " << solo.notes.size() << " notes, " << bends << " bends, " << vibratos << " vibratos, " << offbeat << " off the beat, "
              << renderEvents (solo).size() << " events" << std::endl;
    return 0;
}
