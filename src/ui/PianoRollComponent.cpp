#include "ui/PianoRollComponent.h"

#include "core/NoteName.h"
#include "ui/RollColours.h"

#include <algorithm>
#include <cmath>

namespace pianoassist::ui
{
namespace
{
constexpr double minPixelsPerSecond = 10.0;
constexpr double maxPixelsPerSecond = 2000.0;
constexpr int pitchMargin = 2;
constexpr int minVisibleRange = 24;
} // namespace

PianoRollComponent::PianoRollComponent()
{
    scrollBar_.addListener (this);
    scrollBar_.setAutoHide (false);
    addAndMakeVisible (scrollBar_);
    setTitle ("Piano roll");
    setWantsKeyboardFocus (false);
}

void PianoRollComponent::setSong (const PianoRollView* view)
{
    view_ = view;
    viewStartSeconds_ = 0.0;
    playheadSeconds_ = 0.0;
    partVisible_.assign (view != nullptr ? view->parts.size() : 0, true);
    if (view != nullptr && ! view->notes.empty())
    {
        lowPitch_ = std::max (0, view->lowestPitch - pitchMargin);
        highPitch_ = std::min (127, view->highestPitch + pitchMargin);
        if (highPitch_ - lowPitch_ < minVisibleRange)
        {
            const int centre = (lowPitch_ + highPitch_) / 2;
            lowPitch_ = std::max (0, centre - minVisibleRange / 2);
            highPitch_ = std::min (127, lowPitch_ + minVisibleRange);
        }
    }
    updateScrollBar();
    repaint();
}

void PianoRollComponent::setPartVisible (std::size_t part, bool visible)
{
    if (part < partVisible_.size())
        partVisible_[part] = visible;
    repaint();
}

void PianoRollComponent::setHeldInputPitches (std::vector<std::uint8_t> pitches)
{
    if (pitches == heldInput_)
        return;
    heldInput_ = std::move (pitches);
    repaint (0, 0, gutterWidth, getHeight());
}

void PianoRollComponent::setPixelsPerSecond (double pixelsPerSecond)
{
    pixelsPerSecond_ = std::clamp (pixelsPerSecond, minPixelsPerSecond, maxPixelsPerSecond);
    updateScrollBar();
    repaint();
}

juce::Rectangle<int> PianoRollComponent::rollArea() const
{
    return getLocalBounds().withTrimmedLeft (gutterWidth).withTrimmedBottom (scrollBarHeight);
}

float PianoRollComponent::rowHeight() const
{
    return static_cast<float> (rollArea().getHeight()) / static_cast<float> (highPitch_ - lowPitch_ + 1);
}

float PianoRollComponent::yForPitch (int pitch) const
{
    // Top edge of the pitch row.
    return static_cast<float> (rollArea().getY()) + static_cast<float> (highPitch_ - pitch) * rowHeight();
}

float PianoRollComponent::xForSeconds (double seconds) const
{
    return static_cast<float> (rollArea().getX() + (seconds - viewStartSeconds_) * pixelsPerSecond_);
}

void PianoRollComponent::paint (juce::Graphics& g)
{
    const auto area = rollArea();
    g.setColour (juce::Colour (0xff1e2428));
    g.fillRect (area);

    const float row = rowHeight();
    for (int pitch = lowPitch_; pitch <= highPitch_; ++pitch)
    {
        const float y = yForPitch (pitch);
        if (isBlackKey (pitch))
        {
            g.setColour (juce::Colour (0xff181d20));
            g.fillRect (juce::Rectangle<float> (static_cast<float> (area.getX()), y, static_cast<float> (area.getWidth()), row));
        }
        if (pitch % 12 == 0)
        {
            g.setColour (juce::Colour (0xff3a444a));
            g.drawHorizontalLine (static_cast<int> (y + row), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        }
    }

    // Gutter: keys and C labels; live input highlighted.
    for (int pitch = lowPitch_; pitch <= highPitch_; ++pitch)
    {
        const auto key = juce::Rectangle<float> (0.0f, yForPitch (pitch), static_cast<float> (gutterWidth - 2), row);
        const bool held = std::find (heldInput_.begin(), heldInput_.end(), pitch) != heldInput_.end();
        g.setColour (held ? juce::Colour (0xff6fcf97) : (isBlackKey (pitch) ? juce::Colour (0xff222222) : juce::Colour (0xffdcdcdc)));
        g.fillRect (key.reduced (0.0f, 0.5f));
        if (pitch % 12 == 0 && row >= 7.0f)
        {
            g.setColour (juce::Colours::black);
            g.setFont (juce::FontOptions (std::min (12.0f, row + 2.0f)));
            g.drawText (noteName (pitch), key.withTrimmedLeft (2.0f), juce::Justification::centredLeft);
        }
    }

    if (view_ == nullptr)
        return;

    g.saveState();
    g.reduceClipRegion (area);

    const double visibleEnd = viewStartSeconds_ + area.getWidth() / pixelsPerSecond_;
    g.setColour (juce::Colour (0xff4d5a62));
    for (const double bar : view_->barStarts)
        if (bar >= viewStartSeconds_ && bar <= visibleEnd)
            g.drawVerticalLine (static_cast<int> (xForSeconds (bar)), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));

    for (const auto& note : view_->notes)
    {
        if (note.startSeconds > visibleEnd)
            break;
        if (note.endSeconds < viewStartSeconds_ || note.pitch < lowPitch_ || note.pitch > highPitch_)
            continue;
        if (note.part >= 0 && static_cast<std::size_t> (note.part) < partVisible_.size() && ! partVisible_[static_cast<std::size_t> (note.part)])
            continue;

        const float x1 = xForSeconds (note.startSeconds);
        const float x2 = std::max (x1 + 2.0f, xForSeconds (note.endSeconds));
        const auto rect = juce::Rectangle<float> (x1, yForPitch (note.pitch) + 0.5f, x2 - x1, std::max (1.0f, row - 1.0f));
        auto colour = roleColour (note.role);
        if (note.role == RollRole::backing)
            colour = colour.withAlpha (0.55f);
        g.setColour (colour);
        g.fillRoundedRectangle (rect, std::min (3.0f, row / 3.0f));
    }
    const float playheadX = xForSeconds (playheadSeconds_);
    g.setColour (juce::Colour (0xffff5c5c));
    g.fillRect (juce::Rectangle<float> (playheadX - 1.0f, static_cast<float> (area.getY()), 2.0f, static_cast<float> (area.getHeight())));
    g.restoreState();
}

void PianoRollComponent::setPlayhead (double seconds, bool follow)
{
    if (std::abs (seconds - playheadSeconds_) < 1.0e-4 && ! follow)
        return;
    playheadSeconds_ = seconds;
    if (follow)
    {
        const double visible = rollArea().getWidth() / pixelsPerSecond_;
        if (seconds < viewStartSeconds_ || seconds > viewStartSeconds_ + visible * 0.8)
        {
            viewStartSeconds_ = std::max (0.0, seconds - visible * 0.2);
            updateScrollBar();
        }
    }
    repaint (rollArea());
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    if (view_ == nullptr || ! rollArea().contains (e.getPosition()) || ! onSeekRequested)
        return;
    const double seconds = viewStartSeconds_ + (static_cast<double> (e.position.x) - rollArea().getX()) / pixelsPerSecond_;
    onSeekRequested (std::clamp (seconds, 0.0, view_->durationSeconds));
}

void PianoRollComponent::resized()
{
    scrollBar_.setBounds (getLocalBounds().removeFromBottom (scrollBarHeight).withTrimmedLeft (gutterWidth));
    updateScrollBar();
}

void PianoRollComponent::updateScrollBar()
{
    const double duration = view_ != nullptr ? view_->durationSeconds : 0.0;
    const double visible = rollArea().getWidth() / pixelsPerSecond_;
    scrollBar_.setRangeLimits (0.0, std::max (duration + 1.0, visible), juce::dontSendNotification);
    scrollBar_.setCurrentRange (viewStartSeconds_, visible, juce::dontSendNotification);
}

void PianoRollComponent::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    viewStartSeconds_ = std::max (0.0, newRangeStart);
    repaint();
}

void PianoRollComponent::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const double mouseX = static_cast<double> (e.position.x) - rollArea().getX();
        const double anchorSeconds = viewStartSeconds_ + mouseX / pixelsPerSecond_;
        const double newZoom = std::clamp (pixelsPerSecond_ * std::pow (1.25, delta * 4.0), minPixelsPerSecond, maxPixelsPerSecond);
        viewStartSeconds_ = std::max (0.0, anchorSeconds - mouseX / newZoom);
        pixelsPerSecond_ = newZoom;
        if (onZoomChanged)
            onZoomChanged (pixelsPerSecond_);
    }
    else
    {
        const double visible = rollArea().getWidth() / pixelsPerSecond_;
        viewStartSeconds_ = std::max (0.0, viewStartSeconds_ - delta * visible * 0.25);
    }
    updateScrollBar();
    repaint();
}

} // namespace pianoassist::ui
