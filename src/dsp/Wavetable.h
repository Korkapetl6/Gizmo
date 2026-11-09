#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <memory>
#include <vector>

namespace gizmo
{
constexpr int kNumFrames    = 64;            // positions in every wavetable
constexpr int kFrameSize    = 2048;          // samples per frame (one cycle)
constexpr int kNumMipLevels = 11;            // 1023, 512, 256 ... 1 harmonics
constexpr int kMipStride    = kFrameSize + 1; // one guard sample for interpolation

/** How a sample is cut into frames. */
enum class ImportMode
{
    cycles = 0, // one pitch-synchronous cycle per frame (falls back to chunks for unpitched parts)
    chunks = 1  // a fixed 2048-sample piece of the recording per frame
};

/** A finished wavetable. Immutable once built, so it can be shared with the audio thread. */
struct Wavetable
{
    std::vector<float> frames; // kNumFrames * kFrameSize, the base frames (peak normalised)
    std::vector<float> mips;   // kNumMipLevels * kNumFrames * kMipStride, band-limited copies
    juce::String name;         // shown in the UI
    juce::String sourcePath;   // original file, used to re-import with another mode
    int pitchedFrames = 0;     // how many frames were cut as real cycles
    bool isDefault = false;    // the built-in table, not saved with the project

    std::vector<float> source; // the audible part of the original sample (mono), played by the B steps
    double sourceRate = 44100.0;
    float rootNote = 60.0f;    // detected pitch of the sample, as a MIDI note (60 if it has none)

    const float* getFrame (int frame) const noexcept
    {
        return frames.data() + (size_t) frame * kFrameSize;
    }

    const float* getMip (int level, int frame) const noexcept
    {
        return mips.data() + ((size_t) level * kNumFrames + (size_t) frame) * kMipStride;
    }
};

struct ImportResult
{
    std::unique_ptr<Wavetable> table;
    juce::String error;
};

/** Cuts mono audio into a 64-frame wavetable. */
ImportResult buildWavetableFromAudio (std::vector<float> mono, double sampleRate, ImportMode mode);

/** Builds a wavetable (including the band-limited copies) from kNumFrames * kFrameSize samples. */
std::unique_ptr<Wavetable> buildWavetableFromFrames (const float* frames);

/** The table the plugin starts with: sine, saw, square and a narrowing pulse. */
std::unique_ptr<Wavetable> makeDefaultWavetable();

/** Longer files are cut off after this many seconds (the sample itself is kept for the B steps). */
constexpr double kMaxImportSeconds = 60.0;

/** Reads an audio file (up to kMaxImportSeconds) and builds a wavetable from it. */
ImportResult loadWavetableFromFile (const juce::File& file, ImportMode mode, juce::AudioFormatManager& formats);
} // namespace gizmo
