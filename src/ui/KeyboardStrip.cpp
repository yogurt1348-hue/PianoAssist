#include "ui/KeyboardStrip.h"

#include "ui/RollColours.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int lowest = 21;  // A0
constexpr int highest = 108; // C8
const juce::Colour heldColour (0xff6fcf97);
} // namespace

void KeyboardStrip::setHeldPitches (std::vector<std::uint8_t> pitches)
{
    if (pitches == held_)
        return;
    held_ = std::move (pitches);
    repaint();
}

void KeyboardStrip::paint (juce::Graphics& g)
{
    int whiteCount = 0;
    for (int p = lowest; p <= highest; ++p)
        if (! isBlackKey (p))
            ++whiteCount;

    const auto bounds = getLocalBounds().toFloat();
    const float whiteWidth = bounds.getWidth() / static_cast<float> (whiteCount);
    const auto isHeld = [this] (int p) { return std::find (held_.begin(), held_.end(), p) != held_.end(); };

    float x = bounds.getX();
    for (int p = lowest; p <= highest; ++p)
    {
        if (isBlackKey (p))
            continue;
        const auto key = juce::Rectangle<float> (x, bounds.getY(), whiteWidth, bounds.getHeight());
        g.setColour (isHeld (p) ? heldColour : juce::Colours::white);
        g.fillRect (key.reduced (0.5f, 0.0f));
        if (p == 60)
        {
            g.setColour (juce::Colours::grey);
            g.setFont (juce::FontOptions (10.0f));
            g.drawText ("C4", key.withTrimmedTop (key.getHeight() - 14.0f), juce::Justification::centred);
        }
        x += whiteWidth;
    }

    x = bounds.getX();
    for (int p = lowest; p <= highest; ++p)
    {
        if (! isBlackKey (p))
        {
            x += whiteWidth;
            continue;
        }
        const float blackWidth = whiteWidth * 0.6f;
        const auto key = juce::Rectangle<float> (x - blackWidth / 2.0f, bounds.getY(), blackWidth, bounds.getHeight() * 0.62f);
        g.setColour (isHeld (p) ? heldColour.darker (0.2f) : juce::Colours::black);
        g.fillRect (key);
    }
}

} // namespace pianoassist::ui
