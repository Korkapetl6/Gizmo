// Renders single notes of the guitar model to raw 32-bit float mono files, for looking at its tone:
//   SolistaTone <out.f32> <pitch> <drive 0..1> <tone 0..1> <seconds> [bend semitones]

#include "GuitarSynth.h"

#include <fstream>

int main (int argc, char** argv)
{
    if (argc < 6)
        return 1;

    const auto pitch = juce::String (argv[2]).getIntValue();
    const auto drive = juce::String (argv[3]).getFloatValue(), tone = juce::String (argv[4]).getFloatValue();
    const auto seconds = juce::String (argv[5]).getDoubleValue();
    const auto bend = argc > 6 ? juce::String (argv[6]).getFloatValue() : 0.0f;
    const auto sr = 48000.0;

    solista::GuitarSynth synth;
    synth.prepare (sr, 256);
    synth.setSound (drive, tone, 0.0f, 0.0f);
    synth.noteOn (pitch, 100, false);

    std::ofstream out (argv[1], std::ios::binary);
    std::vector<float> left (256), right (256);
    const auto blocks = (int) (seconds * sr / 256);

    for (int i = 0; i < blocks; ++i)
    {
        if (i == blocks / 3)
            synth.setBend (bend);

        if (i == blocks * 9 / 10)
            synth.noteOff (pitch);

        synth.render (left.data(), right.data(), 256);
        out.write ((const char*) left.data(), 256 * (std::streamsize) sizeof (float));
    }

    return 0;
}
