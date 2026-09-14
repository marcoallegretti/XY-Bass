#pragma once

#include "DspUtility.h"

namespace xyb
{

class SpectralBalance
{
public:
    void prepare (double sampleRate, int numChannels);
    void reset();

    void setControls (float mudAmount, float fizzAmount) noexcept;

    void analyseDry (float mono) noexcept;
    void analyseWet (float mono) noexcept;
    void updateBlock (int numSamples) noexcept;

    float process (int channel, float input) noexcept;

    float getMudGainDb() const noexcept { return mudGainDb; }
    float getFizzGainDb() const noexcept { return fizzGainDb; }

private:
    struct BandProbe
    {
        TptSvf anchor, mud, fizz;
        EnvelopeFollower anchorLevel, mudLevel, fizzLevel;

        void prepare (double rate)
        {
            anchor.prepare (rate);
            anchor.setCutoff (85.0f);
            anchor.setQ (1.0f);

            mud.prepare (rate);
            mud.setCutoff (190.0f);
            mud.setQ (1.1f);

            fizz.prepare (rate);
            fizz.setCutoff (520.0f);
            fizz.setQ (1.0f);

            for (auto* envelope : { &anchorLevel, &mudLevel, &fizzLevel })
            {
                envelope->prepare (rate);
                envelope->setTimes (25.0f, 220.0f);
            }
        }

        void reset()
        {
            anchor.reset();
            mud.reset();
            fizz.reset();

            for (auto* envelope : { &anchorLevel, &mudLevel, &fizzLevel })
                envelope->reset();
        }

        void push (float mono) noexcept
        {
            anchorLevel.process (anchor.processBandPass (mono));
            mudLevel.process (mud.processBandPass (mono));
            fizzLevel.process (fizz.processBandPass (mono));
        }
    };

    BandProbe dryProbe, wetProbe;
    std::vector<Biquad> mudFilters, fizzFilters;

    SmoothedScalar mudControl, fizzControl, mudGain, fizzGain;

    float mudGainDb = 0.0f;
    float fizzGainDb = 0.0f;
    float sampleRate = 44100.0f;
};

} // namespace xyb
