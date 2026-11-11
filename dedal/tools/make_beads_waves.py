#!/usr/bin/env python3
# Builds dedal/src/BeadsWaves.h from the wavetable definitions of the Beads firmware
# (https://github.com/pichenettes/eurorack, beads/resources/wavetables.py, MIT license,
# Copyright 2016 Emilie Gillet). This is a Python 3 port of that script; it keeps the
# waves themselves and stores them as plain single cycles instead of integrated tables.
#
#   python3 make_beads_waves.py <path to eurorack checkout> <output header>

import sys
import numpy

root, output = sys.argv[1], sys.argv[2]
WAVETABLE_SIZE = 256
BRAIDS_WAVES = numpy.frombuffer(open(root + '/beads/resources/waves.bin', 'rb').read(), numpy.uint8)


def sine(frequency):
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    if frequency >= WAVETABLE_SIZE / 2:
        return t * 0
    return numpy.sin(2 * numpy.pi * t * frequency)


def tri(n, f=1):
    x = 0
    for i in range(n):
        x = x + sine((2 * i + 1) * f) / (2 * i + 1) ** 2.0
    return x


def tri_stack(n):
    x = 0
    for i in range(n):
        x = x + tri(15 + 5 * n, i + n // 3)  # integer division, as in the original Python 2
    return x


def saw(n, f=1):
    x = 0
    for i in range(n):
        x = x + sine((i + 1) * f) / (i + 1)
    return x


def drawbars(bars):
    pipes = [1.0, 3.0, 2.0, 4.0, 6.0, 8.0, 10.0, 12.0, 16.0]
    x = 0
    for intensity, frequency in zip(bars, pipes):
        x = x + int(intensity) / 8.0 * sine(frequency)
    return x


def burst(duty):
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    t[-1] = t[0]
    d = duty ** 0.5
    t[t < d] = -1.0
    t[t >= d] = 0.0
    return -t * sine(1.0 / duty)


def trisaw(harmonic):
    return tri(80) + saw(80, harmonic) * (1 if harmonic != 1 else 0.25) * 0.5


def sawtri(harmonic):
    return saw(80) * 0.5 + tri(80, harmonic) * (1 if harmonic != 1 else 0.25)


def bandpass_formant(ratio):
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    amplitude = 1.0 - t
    return sine(ratio * 1.5) * amplitude * 0.5


def formant_f(index):
    formant_1 = 3.9 * (index + 1) / 8.0
    formant_2 = formant_1 * (1.0 - numpy.cos(formant_1 * numpy.pi * 0.8))
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    amplitude_1 = (1.0 - t) ** 0.2 * numpy.exp(-4.0 * t)
    amplitude_2 = (1.0 - t) ** 0.2 * numpy.exp(-2.0 * t)
    formant_3 = sine(1 + 2.8 * (formant_2 + formant_1)) * amplitude_2 * 1.7
    formant_1 = sine(1 + 3 * formant_1) * amplitude_1
    formant_2 = sine(1 + 4 * formant_2) * amplitude_2 * 1.5
    f = formant_1 + formant_2 + formant_3
    return f - (f.max() + f.min()) / 2.0


def distort(x):
    return numpy.arctan(x * 8.0) / numpy.pi


def digi_formant_f(index):
    formant_1 = 3.9 * (index + 1) / 8.0
    formant_2 = formant_1 * (1.0 - numpy.cos(formant_1 * numpy.pi * 0.8))
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    amplitude_1 = (1.0 - t) ** 0.2 * numpy.exp(-4.0 * t)
    amplitude_2 = (1.0 - t) ** 0.2 * numpy.exp(-2.0 * t)
    formant_3 = distort(sine(1 + 2.9 * (formant_2 + formant_1))) * amplitude_2 * 0.7
    formant_1 = distort(sine(1 + 3.2 * formant_1)) * amplitude_1
    formant_2 = distort(sine(1 + 4.1 * formant_2)) * amplitude_2 * 0.7
    f = formant_1 + formant_2 + formant_3
    return f - (f.max() + f.min()) / 2.0


def fm(carrier, modulator, amount):
    modulator = sine(modulator)
    t = numpy.arange(0, WAVETABLE_SIZE) / float(WAVETABLE_SIZE)
    return numpy.sin(2 * numpy.pi * (t * carrier + modulator * amount))


def make_braids_family(indices):
    family = []
    for i in indices:
        s = BRAIDS_WAVES[i * 129:i * 129 + 128] - 128.0
        sf = numpy.fft.rfft(s)
        sf = numpy.abs(sf) * numpy.exp(-1j * numpy.pi / 2.0)
        family.append(numpy.fft.irfft(sf, WAVETABLE_SIZE))
    return family


waves = []
waves += [sine(f) for f in range(1, 17)]                                    # A
waves += [tri_stack(n) for n in range(2, 18)]                               # B
waves += [drawbars(b) for b in ['688600000', '686040000', '666806000', '655550600', '665560060',
                                '688500888', '660000888', '060000046', '867000006', '888876788',
                                '668744354', '448644054', '327645222', '204675300', '002478500',
                                '002050321']]                               # C
waves += [trisaw(h) for h in [1, 1.5, 2, 3, 4, 4.5, 5, 8]]                  # D
waves += [sawtri(h) for h in [8, 5, 4.5, 4, 3, 2, 1.5, 1]]
waves += [burst(d) for d in [0.5, 0.4, 1 / 3.0, 0.25, 0.2, 0.125, 1 / 16.0, 1 / 32.0]]  # E
waves += [bandpass_formant(r) for r in [16.0, 10.0, 9.0, 8.0, 6.0, 4.0, 3.0, 2.0]]
waves += [formant_f(i) for i in range(8)]                                   # F
waves += [digi_formant_f(i) for i in [7, 6, 5, 4, 3, 2, 1, 0]]
waves += make_braids_family([176, 189, 191, 193, 195, 197, 199, 201])       # G
waves += make_braids_family([220, 222, 224, 226, 228, 230, 232, 234])
waves += make_braids_family(range(172, 188))                                # H
sweep = numpy.linspace(0, 1.0, 16)
waves += [fm(2.0, 1.0, x) for x in 4 * sweep ** 2]                          # I
waves += [fm(1.0, 0.5, x) for x in 8 * sweep ** 2]                          # J
waves += [fm(1.0, 3.0, x) for x in 3 * sweep ** 2]                          # K
waves += [fm(1.0, 8.0, x) for x in 2 * sweep ** 2]                          # L
assert len(waves) == 192

with open(output, 'w') as f:
    f.write('// Generated by dedal/tools/make_beads_waves.py. Do not edit.\n')
    f.write('//\n// Wavetables of the Beads firmware, https://github.com/pichenettes/eurorack\n')
    f.write('// Copyright 2016 Emilie Gillet. MIT license:\n//\n')
    f.write('// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and\n'
            '// associated documentation files (the "Software"), to deal in the Software without restriction,\n'
            '// including without limitation the rights to use, copy, modify, merge, publish, distribute,\n'
            '// sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is\n'
            '// furnished to do so, subject to the following conditions:\n//\n'
            '// The above copyright notice and this permission notice shall be included in all copies or\n'
            '// substantial portions of the Software.\n//\n'
            '// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT\n'
            '// NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n'
            '// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,\n'
            '// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT\n'
            '// OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n\n')
    f.write('#pragma once\n\n#include <cstdint>\n\nnamespace dedal::beads\n{\n')
    f.write('constexpr int kNumBanks = 12, kWavesPerBank = 16, kWaveSize = 256;\n\n')
    f.write('// 12 banks x 16 single cycles x 256 samples, each wave normalised to full scale.\n')
    f.write('inline constexpr std::int16_t waves[kNumBanks * kWavesPerBank * kWaveSize] = {\n')
    for w in waves:
        x = numpy.array(w, dtype=float)
        x -= x.mean()
        x /= numpy.abs(x).max()
        values = numpy.round(x * 32767).astype(int)
        for i in range(0, WAVETABLE_SIZE, 32):
            f.write('    ' + ','.join(str(v) for v in values[i:i + 32]) + ',\n')
    f.write('};\n} // namespace dedal::beads\n')
