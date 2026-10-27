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
    /** Moves the playhead; when following, pages the view to keep it visible.
        After the user scrolls by hand the view stays put until goToPlayhead(). */
    void setPlayhead (double seconds, bool follow);
    /** Zooms by factor, keeping the playhead (or the middle of the view when
        the playhead is off screen) at the same place. */
    void zoomBy (double factor);
    /** Fits the whole song into the view. */
    void showWholeSong();
    /** Brings the playhead into view and resumes following it. */
    void goToPlayhead();
    bool userScrolled() const noexcept { return userScrolled_; }
    /** The user scrolled away from the playhead (true) or came back (false). */
    std::function<void (bool scrolledAway)> onUserScrolledChanged;

    std::function<void (double pixelsPerSecond)> onZoomChanged;
    std::function<void (double seconds)> onSeekRequested;
    /** Dragging along the bar numbers selects a loop (snapped to bars). */
    std::function<void (double startSeconds, double endSeconds)> onLoopSelected;
    void setLoop (bool enabled, double startSeconds, double endSeconds);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;

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
    bool userScrolled_ = false;
    void setUserScrolled (bool scrolled);
    void zoomAround (double newPixelsPerSecond, double anchorSeconds);
    double playheadSeconds_ = 0.0;
    bool loopEnabled_ = false;
    double loopStart_ = 0.0, loopEnd_ = 0.0;
    bool draggingLoop_ = false;
    int edgeDrag_ = 0; ///< 1 = moving the loop start (A), 2 = the end (B)
    /** 1 or 2 when x is within grabbing distance of the A or B edge. */
    int loopEdgeAt (float x) const;
    double dragAnchor_ = 0.0;
    double secondsAt (float x) const;
    double snapToBar (double seconds) const;
    double barAtOrBefore (double seconds) const;
    int lowPitch_ = 48;
    int highPitch_ = 84;
    juce::ScrollBar scrollBar_ { false };
};

} // namespace pianoassist::ui
