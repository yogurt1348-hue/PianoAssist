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
    return getLocalBounds().withTrimmedLeft (gutterWidth).withTrimmedBottom (scrollBarHeight).withTrimmedTop (rulerHeight);
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
    g.setColour (theme::rollBackground);
    g.fillRect (area);

    const float row = rowHeight();
    for (int pitch = lowPitch_; pitch <= highPitch_; ++pitch)
    {
        const float y = yForPitch (pitch);
        if (isBlackKey (pitch))
        {
            g.setColour (theme::rollBlackRow);
            g.fillRect (juce::Rectangle<float> (static_cast<float> (area.getX()), y, static_cast<float> (area.getWidth()), row));
        }
        if (pitch % 12 == 0)
        {
            g.setColour (theme::rollOctaveLine);
            g.drawHorizontalLine (static_cast<int> (y + row), static_cast<float> (area.getX()), static_cast<float> (area.getRight()));
        }
    }

    // Gutter: keys and C labels; live input highlighted.
    for (int pitch = lowPitch_; pitch <= highPitch_; ++pitch)
    {
        const auto key = juce::Rectangle<float> (0.0f, yForPitch (pitch), static_cast<float> (gutterWidth - 2), row);
        const bool held = std::find (heldInput_.begin(), heldInput_.end(), pitch) != heldInput_.end();
        g.setColour (held ? theme::good : (isBlackKey (pitch) ? juce::Colour (0xff222222) : juce::Colour (0xffdcdcdc)));
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
    g.setColour (theme::rollBarLine);
    for (const double bar : view_->barStarts)
        if (bar >= viewStartSeconds_ && bar <= visibleEnd)
            g.drawVerticalLine (static_cast<int> (xForSeconds (bar)), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
    g.restoreState();

    // Bar numbers above the roll; thinned out when bars are narrow.
    {
        const auto ruler = juce::Rectangle<int> (area.getX(), 0, area.getWidth(), rulerHeight);
        g.saveState();
        g.reduceClipRegion (ruler);
        if (loopEnabled_ && loopEnd_ > loopStart_)
        {
            const float x1 = xForSeconds (loopStart_), x2 = xForSeconds (loopEnd_);
            g.setColour (theme::accent.withAlpha (0.35f));
            g.fillRoundedRectangle (juce::Rectangle<float> (x1, 3.0f, x2 - x1, static_cast<float> (rulerHeight - 6)), 4.0f);
        }
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (theme::mutedText);
        int every = 1;
        if (view_->barStarts.size() > 1)
        {
            const double barPixels = (view_->barStarts[1] - view_->barStarts[0]) * pixelsPerSecond_;
            while (barPixels * every < 36.0 && every < 64)
                every *= 2;
        }
        for (std::size_t i = 0; i < view_->barStarts.size(); ++i)
        {
            const double bar = view_->barStarts[i];
            if (bar > visibleEnd)
                break;
            if (bar < viewStartSeconds_ - 2.0 || (i % static_cast<std::size_t> (every)) != 0)
                continue;
            const auto x = static_cast<int> (xForSeconds (bar));
            g.drawText (juce::String (static_cast<int> (i + 1)), x + 3, 2, 40, rulerHeight - 4, juce::Justification::centredLeft);
        }
        g.restoreState();
    }
    g.saveState();
    g.reduceClipRegion (area);

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
    if (loopEnabled_ && loopEnd_ > loopStart_)
    {
        const float x1 = xForSeconds (loopStart_), x2 = xForSeconds (loopEnd_);
        g.setColour (theme::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x1, static_cast<float> (area.getY()), x2 - x1, static_cast<float> (area.getHeight())));
        g.setColour (theme::accent.withAlpha (0.6f));
        g.drawVerticalLine (static_cast<int> (x1), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
        g.drawVerticalLine (static_cast<int> (x2), static_cast<float> (area.getY()), static_cast<float> (area.getBottom()));
    }
    const float playheadX = xForSeconds (playheadSeconds_);
    g.setColour (theme::playhead);
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

double PianoRollComponent::secondsAt (float x) const
{
    const double seconds = viewStartSeconds_ + (static_cast<double> (x) - rollArea().getX()) / pixelsPerSecond_;
    return std::clamp (seconds, 0.0, view_ != nullptr ? view_->durationSeconds : 0.0);
}

double PianoRollComponent::snapToBar (double seconds) const
{
    if (view_ == nullptr || view_->barStarts.empty())
        return seconds;
    double best = view_->durationSeconds;
    for (const double bar : view_->barStarts)
        if (std::abs (bar - seconds) < std::abs (best - seconds))
            best = bar;
    return best;
}

void PianoRollComponent::setLoop (bool enabled, double startSeconds, double endSeconds)
{
    loopEnabled_ = enabled;
    loopStart_ = startSeconds;
    loopEnd_ = endSeconds;
    repaint();
}

void PianoRollComponent::mouseDown (const juce::MouseEvent& e)
{
    const auto clickable = rollArea().withTop (0);
    draggingLoop_ = false;
    if (view_ == nullptr || ! clickable.contains (e.getPosition()))
        return;
    dragAnchor_ = secondsAt (e.position.x);
    if (e.position.y < static_cast<float> (rulerHeight))
        return; // the ruler: a click seeks on mouse up, a drag selects a loop
    if (onSeekRequested)
        onSeekRequested (dragAnchor_);
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (view_ == nullptr || e.mouseDownPosition.y >= static_cast<float> (rulerHeight) || e.getDistanceFromDragStartX() == 0)
        return;
    if (std::abs (e.getDistanceFromDragStartX()) < 6 && ! draggingLoop_)
        return;
    draggingLoop_ = true;
    const auto a = snapToBar (dragAnchor_);
    const auto b = snapToBar (secondsAt (e.position.x));
    setLoop (true, std::min (a, b), std::max (a, b));
}

void PianoRollComponent::mouseUp (const juce::MouseEvent& e)
{
    if (view_ == nullptr || e.mouseDownPosition.y >= static_cast<float> (rulerHeight))
        return;
    if (draggingLoop_)
    {
        draggingLoop_ = false;
        if (loopEnd_ - loopStart_ > 0.05 && onLoopSelected)
            onLoopSelected (loopStart_, loopEnd_);
        return;
    }
    if (onSeekRequested && rollArea().withTop (0).contains (e.getPosition()))
        onSeekRequested (dragAnchor_);
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
