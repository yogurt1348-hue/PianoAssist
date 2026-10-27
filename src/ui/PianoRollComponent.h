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

    /** How one track is drawn. Colour is the track's own; bright is false
        when the track is not heard now (Mute, Solo, accompaniment off) and is
        not played by the learner either. hand: 0 none, 1 right, 2 left, 3 both. */
    struct PartStyle
    {
        juce::Colour colour { 0xff7fcf95 };
        juce::String name;      ///< for the hover card
        juce::String instrument;
        bool visible = true;    ///< the "eye"
        bool bright = true;
        bool selected = false;
        int hand = 0;
    };

    void setSong (const PianoRollView* view); ///< nullptr clears; the view must outlive its use
    /** Which tracks this roll shows at all (the tab: my parts or the others). */
    void setPartVisible (std::size_t part, bool visible);
    void setPartStyles (std::vector<PartStyle> styles);
    /** Notes in their track colours (true) or in hand colours (false). */
    void setColourByTrack (bool byTrack);
    /** Leave out tracks that are not heard instead of drawing them faint. */
    void setHideDimmed (bool hide);
    /** The step waiting for the learner: its notes get an outline, played
        ones a tick. onsetSeconds < 0 clears. */
    void setGateHighlight (double onsetSeconds, std::vector<std::uint8_t> required, std::vector<std::uint8_t> matched);
    /** Wrong presses flash red on the keys at the left. */
    void setWrongPitches (std::vector<std::uint8_t> pitches);
    void setHeldInputPitches (std::vector<std::uint8_t> pitches);
    /** Right-click on a note: the track to select, show alone, or show all again. */
    std::function<void (int part)> onSelectPart;
    std::function<void (int part)> onShowOnlyPart;
    std::function<void()> onShowAllParts;
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
    void mouseExit (const juce::MouseEvent& e) override;

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
    std::vector<PartStyle> styles_;
    std::vector<int> overlaps_; ///< per note: other notes at the same pitch and time
    bool colourByTrack_ = true;
    bool hideDimmed_ = false;
    double gateOnset_ = -1.0;
    std::vector<std::uint8_t> gateRequired_, gateMatched_, wrong_;
    juce::Point<float> hover_ { -1.0f, -1.0f };
    std::vector<std::uint8_t> heldInput_;
    /** Shown notes under a point, the selected track's first. */
    std::vector<std::size_t> notesAt (juce::Point<float> p) const;
    bool shown (const PianoRollView::Note& note) const;
    const PartStyle* styleOf (int part) const;
    juce::Rectangle<float> noteRect (const PianoRollView::Note& note) const;
    void paintNotes (juce::Graphics& g, double visibleEnd);
    void paintHoverCard (juce::Graphics& g);
    juce::String positionText (double seconds) const;
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
