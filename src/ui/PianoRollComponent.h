#pragma once

#include "ui/PianoRollView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace pianoassist::ui
{

/** Horizontal piano roll: time left to right, pitch bottom to top, note
    names in the left gutter. Wheel scrolls, Ctrl+wheel zooms around the
    mouse. Draws only the visible time range. */
class PianoRollComponent final : public juce::Component, private juce::ScrollBar::Listener
{
public:
    PianoRollComponent();

    void setSong (const PianoRollView* view); ///< nullptr clears; the view must outlive its use
    void setPartVisible (std::size_t part, bool visible);
    void setHeldInputPitches (std::vector<std::uint8_t> pitches);
    void setPixelsPerSecond (double pixelsPerSecond);
    double pixelsPerSecond() const noexcept { return pixelsPerSecond_; }
    /** Moves the playhead; when following, pages the view to keep it visible. */
    void setPlayhead (double seconds, bool follow);

    std::function<void (double pixelsPerSecond)> onZoomChanged;
    std::function<void (double seconds)> onSeekRequested;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent& e) override;

private:
    static constexpr int gutterWidth = 44;
    static constexpr int scrollBarHeight = 12;
    static constexpr int rulerHeight = 22;

    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void updateScrollBar();
    juce::Rectangle<int> rollArea() const;
    float yForPitch (int pitch) const;
    float rowHeight() const;
    float xForSeconds (double seconds) const;

    const PianoRollView* view_ = nullptr;
    std::vector<bool> partVisible_;
    std::vector<std::uint8_t> heldInput_;
    double pixelsPerSecond_ = 120.0;
    double viewStartSeconds_ = 0.0;
    double playheadSeconds_ = 0.0;
    int lowPitch_ = 48;
    int highPitch_ = 84;
    juce::ScrollBar scrollBar_ { false };
};

} // namespace pianoassist::ui
