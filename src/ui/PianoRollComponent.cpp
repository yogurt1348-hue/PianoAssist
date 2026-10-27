#include "ui/PianoRollComponent.h"

#include "core/NoteName.h"
#include "ui/RollColours.h"

#include <algorithm>
#include <array>
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
    hover_ = { -1.0f, -1.0f };
    overlaps_.assign (view != nullptr ? view->notes.size() : 0, 0);
    if (view != nullptr)
    {
        // Notes sharing a pitch and time hide each other; count them once per song.
        std::array<std::vector<std::size_t>, 128> byPitch;
        for (std::size_t i = 0; i < view->notes.size(); ++i)
            byPitch[view->notes[i].pitch].push_back (i); // already sorted by start
        for (const auto& list : byPitch)
            for (std::size_t a = 0; a < list.size(); ++a)
            {
                const auto& na = view->notes[list[a]];
                for (std::size_t b = a + 1; b < list.size(); ++b)
                {
                    const auto& nb = view->notes[list[b]];
                    if (nb.startSeconds >= na.endSeconds - 1.0e-4)
                        break;
                    ++overlaps_[list[a]];
                    ++overlaps_[list[b]];
                }
            }
    }
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

void PianoRollComponent::setPartStyles (std::vector<PartStyle> styles)
{
    styles_ = std::move (styles);
    repaint();
}

void PianoRollComponent::setColourByTrack (bool byTrack)
{
    if (colourByTrack_ == byTrack)
        return;
    colourByTrack_ = byTrack;
    repaint();
}

void PianoRollComponent::setHideDimmed (bool hide)
{
    if (hideDimmed_ == hide)
        return;
    hideDimmed_ = hide;
    repaint();
}

void PianoRollComponent::setGateHighlight (double onsetSeconds, std::vector<std::uint8_t> required, std::vector<std::uint8_t> matched)
{
    if (onsetSeconds == gateOnset_ && required == gateRequired_ && matched == gateMatched_)
        return;
    gateOnset_ = onsetSeconds;
    gateRequired_ = std::move (required);
    gateMatched_ = std::move (matched);
    repaint (rollArea());
}

void PianoRollComponent::setWrongPitches (std::vector<std::uint8_t> pitches)
{
    if (pitches == wrong_)
        return;
    wrong_ = std::move (pitches);
    repaint (0, 0, gutterWidth, getHeight());
}

const PianoRollComponent::PartStyle* PianoRollComponent::styleOf (int part) const
{
    return part >= 0 && static_cast<std::size_t> (part) < styles_.size() ? &styles_[static_cast<std::size_t> (part)] : nullptr;
}

bool PianoRollComponent::shown (const PianoRollView::Note& note) const
{
    if (note.part >= 0 && static_cast<std::size_t> (note.part) < partVisible_.size() && ! partVisible_[static_cast<std::size_t> (note.part)])
        return false;
    if (const auto* style = styleOf (note.part))
    {
        if (! style->visible)
            return false;
        if (hideDimmed_ && ! style->bright)
            return false;
    }
    return true;
}

juce::Rectangle<float> PianoRollComponent::noteRect (const PianoRollView::Note& note) const
{
    const float x1 = xForSeconds (note.startSeconds);
    const float x2 = std::max (x1 + 2.0f, xForSeconds (note.endSeconds));
    return { x1, yForPitch (note.pitch) + 0.5f, x2 - x1, std::max (1.0f, rowHeight() - 1.0f) };
}

std::vector<std::size_t> PianoRollComponent::notesAt (juce::Point<float> p) const
{
    std::vector<std::size_t> found;
    if (view_ == nullptr || ! rollArea().toFloat().contains (p))
        return found;
    const double t = viewStartSeconds_ + (static_cast<double> (p.x) - rollArea().getX()) / pixelsPerSecond_;
    const double slack = 3.0 / pixelsPerSecond_; // very short notes stay reachable
    for (std::size_t i = 0; i < view_->notes.size(); ++i)
    {
        const auto& note = view_->notes[i];
        if (note.startSeconds > t + slack)
            break;
        if (note.endSeconds + slack < t || ! shown (note))
            continue;
        if (noteRect (note).expanded (2.0f, 0.0f).contains (p))
            found.push_back (i);
    }
    std::stable_sort (found.begin(), found.end(), [this] (std::size_t a, std::size_t b) {
        const auto* sa = styleOf (view_->notes[a].part);
        const auto* sb = styleOf (view_->notes[b].part);
        return (sa != nullptr && sa->selected) && ! (sb != nullptr && sb->selected);
    });
    return found;
}

juce::String PianoRollComponent::positionText (double seconds) const
{
    if (view_ == nullptr || view_->barStarts.empty())
        return {};
    std::size_t bar = 0;
    for (std::size_t i = 0; i < view_->barStarts.size() && view_->barStarts[i] <= seconds + 1.0e-6; ++i)
        bar = i;
    const double start = view_->barStarts[bar];
    const double end = bar + 1 < view_->barStarts.size() ? view_->barStarts[bar + 1] : view_->durationSeconds;
    const double beatLength = std::max (1.0e-3, (end - start) / std::max (1, view_->beatsPerBar));
    const int beat = std::clamp (static_cast<int> ((seconds - start) / beatLength + 1.0e-6) + 1, 1, std::max (1, view_->beatsPerBar));
    return TRANS ("bar") + " " + juce::String (static_cast<int> (bar + 1)) + ", " + TRANS ("beat") + " " + juce::String (beat);
}

void PianoRollComponent::paintNotes (juce::Graphics& g, double visibleEnd)
{
    const float row = rowHeight();
    const float corner = std::min (3.0f, row / 3.0f);
    const auto inGate = [this] (const PianoRollView::Note& note, const std::vector<std::uint8_t>& pitches) {
        return gateOnset_ >= 0.0 && std::abs (note.startSeconds - gateOnset_) < 0.03
               && std::find (pitches.begin(), pitches.end(), note.pitch) != pitches.end();
    };

    // Two passes: the selected track last, so other tracks never hide it.
    for (int pass = 0; pass < 2; ++pass)
        for (std::size_t i = 0; i < view_->notes.size(); ++i)
        {
            const auto& note = view_->notes[i];
            if (note.startSeconds > visibleEnd)
                break;
            if (note.endSeconds < viewStartSeconds_ || note.pitch < lowPitch_ || note.pitch > highPitch_ || ! shown (note))
                continue;
            const auto* style = styleOf (note.part);
            const bool selected = style != nullptr && style->selected;
            if (selected != (pass == 1))
                continue;

            const auto rect = noteRect (note);
            juce::Colour colour = colourByTrack_ && style != nullptr ? style->colour : roleColour (note.role);
            if (! colourByTrack_ && note.role == RollRole::backing)
                colour = colour.withAlpha (0.55f);
            const bool bright = style == nullptr || style->bright;
            if (! bright)
                colour = colour.withSaturation (colour.getSaturation() * 0.45f).withAlpha (0.3f); // faint but still there for context
            g.setColour (colour);
            g.fillRoundedRectangle (rect, corner);

            // Hand marker while the notes wear their track colours.
            if (colourByTrack_ && style != nullptr && style->hand != 0 && rect.getWidth() >= 6.0f)
            {
                const int hand = style->hand == 3 ? (note.role == RollRole::leftHand ? 2 : 1) : style->hand;
                g.setColour ((hand == 2 ? theme::leftHand : theme::rightHand).withAlpha (bright ? 1.0f : 0.4f));
                g.fillRect (rect.withWidth (3.0f));
            }

            if (selected)
            {
                g.setColour (theme::text.withAlpha (0.85f));
                g.drawRoundedRectangle (rect.reduced (0.5f), corner, 1.2f);
                if (overlaps_[i] > 0 && rect.getWidth() >= 18.0f && row >= 10.0f)
                {
                    g.setColour (theme::onHand);
                    g.setFont (juce::FontOptions (std::min (11.0f, row - 1.0f), juce::Font::bold));
                    g.drawText ("+" + juce::String (overlaps_[i]), rect.withTrimmedLeft (4.0f), juce::Justification::centredLeft, false);
                }
            }

            if (inGate (note, gateRequired_))
            {
                // Waiting for this note: an outline in the waiting colour and a dot before it.
                g.setColour (theme::waiting);
                g.drawRoundedRectangle (rect.expanded (1.5f), corner + 1.0f, 2.0f);
                const float d = std::clamp (row * 0.6f, 4.0f, 8.0f);
                g.fillEllipse (rect.getX() - d - 3.0f, rect.getCentreY() - d / 2.0f, d, d);
            }
            else if (inGate (note, gateMatched_))
            {
                g.setColour (theme::good);
                g.drawRoundedRectangle (rect.expanded (1.5f), corner + 1.0f, 2.0f);
                if (row >= 8.0f)
                {
                    g.setFont (juce::FontOptions (std::min (13.0f, row + 1.0f), juce::Font::bold));
                    g.drawText (juce::String::fromUTF8 ("\xe2\x9c\x93"), juce::Rectangle<float> (rect.getX() - row - 3.0f, rect.getY(), row + 2.0f, rect.getHeight()),
                                juce::Justification::centred, false);
                }
            }
        }
}

void PianoRollComponent::paintHoverCard (juce::Graphics& g)
{
    const auto under = notesAt (hover_);
    if (under.empty())
        return;
    const auto& note = view_->notes[under.front()];
    const auto* style = styleOf (note.part);
    const auto partLabel = [this] (int part) {
        const auto* st = styleOf (part);
        if (st != nullptr && st->name.isNotEmpty())
            return st->name;
        return part >= 0 && static_cast<std::size_t> (part) < view_->parts.size()
                   ? juce::String::fromUTF8 (view_->parts[static_cast<std::size_t> (part)].label.c_str())
                   : juce::String ("?");
    };
    juce::StringArray lines;
    lines.add (partLabel (note.part));
    lines.add ((style != nullptr && style->instrument.isNotEmpty() ? style->instrument + "  ·  " : juce::String()) + noteName (note.pitch));
    lines.add (positionText (note.startSeconds) + "  ·  " + juce::String (note.endSeconds - note.startSeconds, 2) + " " + TRANS ("s"));
    if (under.size() > 1)
    {
        juce::StringArray others;
        for (std::size_t k = 1; k < under.size() && others.size() < 4; ++k)
            others.addIfNotAlreadyThere (partLabel (view_->notes[under[k]].part));
        lines.add (TRANS ("Also here") + " (" + juce::String (static_cast<int> (under.size() - 1)) + "): " + others.joinIntoString (", "));
    }

    const juce::Font font (juce::FontOptions (13.0f));
    float width = 0.0f;
    for (int k = 0; k < lines.size(); ++k)
        width = std::max (width, juce::GlyphArrangement::getStringWidth (k == 0 ? font.boldened() : font, lines[k]));
    const float lineHeight = 18.0f;
    auto card = juce::Rectangle<float> (hover_.x + 14.0f, hover_.y + 14.0f, width + 46.0f, lineHeight * static_cast<float> (lines.size()) + 12.0f);
    const auto bounds = getLocalBounds().toFloat().reduced (4.0f);
    if (card.getRight() > bounds.getRight())
        card.setX (hover_.x - card.getWidth() - 10.0f);
    if (card.getBottom() > bounds.getBottom())
        card.setY (hover_.y - card.getHeight() - 10.0f);
    card = card.constrainedWithin (bounds);

    g.setColour (theme::panel.withAlpha (0.97f));
    g.fillRoundedRectangle (card, 6.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (card, 6.0f, 1.0f);
    // Track marker: the colour square, the text stays neutral.
    g.setColour (style != nullptr ? style->colour : roleColour (note.role));
    g.fillRoundedRectangle (juce::Rectangle<float> (card.getX() + 10.0f, card.getY() + 10.0f, 12.0f, 12.0f), 3.0f);
    auto text = card.reduced (10.0f, 6.0f).withTrimmedLeft (20.0f);
    for (int k = 0; k < lines.size(); ++k)
    {
        g.setColour (k == 0 ? theme::text : theme::mutedText);
        g.setFont (juce::FontOptions (13.0f, k == 0 ? juce::Font::bold : juce::Font::plain));
        g.drawText (lines[k], text.removeFromTop (lineHeight), juce::Justification::centredLeft, false);
    }
}

void PianoRollComponent::mouseExit (const juce::MouseEvent&)
{
    if (hover_.x < 0.0f)
        return;
    hover_ = { -1.0f, -1.0f };
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
        const bool wrong = std::find (wrong_.begin(), wrong_.end(), pitch) != wrong_.end();
        g.setColour (wrong ? theme::danger : held ? theme::good : (isBlackKey (pitch) ? juce::Colour (0xff222222) : juce::Colour (0xffdcdcdc)));
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
            // Grab handles: A and B flags, wide enough to catch without aiming.
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            for (const auto& [x, text] : { std::pair<float, const char*> { x1, "A" }, std::pair<float, const char*> { x2, "B" } })
            {
                const auto flag = juce::Rectangle<float> (x - 8.0f, 1.0f, 16.0f, static_cast<float> (rulerHeight - 2));
                g.setColour (theme::accent);
                g.fillRoundedRectangle (flag, 3.0f);
                g.setColour (theme::onAccent);
                g.drawText (text, flag, juce::Justification::centred);
            }
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

    paintNotes (g, visibleEnd);
    if (loopEnabled_ && loopEnd_ > loopStart_)
    {
        const float x1 = xForSeconds (loopStart_), x2 = xForSeconds (loopEnd_);
        g.setColour (theme::accent.withAlpha (0.08f));
        g.fillRect (juce::Rectangle<float> (x1, static_cast<float> (area.getY()), x2 - x1, static_cast<float> (area.getHeight())));
        g.setColour (theme::accent.withAlpha (0.8f));
        g.fillRect (juce::Rectangle<float> (x1 - 1.0f, static_cast<float> (area.getY()), 2.0f, static_cast<float> (area.getHeight())));
        g.fillRect (juce::Rectangle<float> (x2 - 1.0f, static_cast<float> (area.getY()), 2.0f, static_cast<float> (area.getHeight())));
    }
    const float playheadX = xForSeconds (playheadSeconds_);
    // A dark edge keeps the playhead visible over any track colour.
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (playheadX - 2.5f, static_cast<float> (area.getY()), 5.0f, static_cast<float> (area.getHeight())));
    g.setColour (theme::playhead);
    g.fillRect (juce::Rectangle<float> (playheadX - 1.0f, static_cast<float> (area.getY()), 2.0f, static_cast<float> (area.getHeight())));
    g.restoreState();
    if (hover_.x >= 0.0f)
        paintHoverCard (g);
}

void PianoRollComponent::setPlayhead (double seconds, bool follow)
{
    if (std::abs (seconds - playheadSeconds_) < 1.0e-4 && ! follow)
        return;
    playheadSeconds_ = seconds;
    if (follow && ! userScrolled_)
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

double PianoRollComponent::barAtOrBefore (double seconds) const
{
    if (view_ == nullptr || view_->barStarts.empty())
        return seconds;
    double best = 0.0;
    for (const double bar : view_->barStarts)
    {
        if (bar > seconds + 1.0e-6)
            break;
        best = bar;
    }
    return best;
}

int PianoRollComponent::loopEdgeAt (float x) const
{
    if (! loopEnabled_ || loopEnd_ <= loopStart_)
        return 0;
    constexpr float grab = 9.0f;
    const float da = std::abs (x - xForSeconds (loopStart_)), db = std::abs (x - xForSeconds (loopEnd_));
    if (da > grab && db > grab)
        return 0;
    return da <= db ? 1 : 2;
}

void PianoRollComponent::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (loopEdgeAt (e.position.x) != 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    const bool wasOver = ! notesAt (hover_).empty();
    hover_ = e.position;
    if (wasOver || ! notesAt (hover_).empty())
        repaint();
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
    if (e.mods.isPopupMenu())
    {
        // Right-click: the track under the mouse. Only the view changes.
        edgeDrag_ = 0;
        const auto under = notesAt (e.position);
        juce::PopupMenu menu;
        if (! under.empty())
        {
            const int part = view_->notes[under.front()].part;
            const auto* style = styleOf (part);
            const auto name = style != nullptr ? style->name : juce::String();
            menu.addItem (TRANS ("Select track") + " \"" + name + "\"", onSelectPart != nullptr, false, [this, part] { if (onSelectPart) onSelectPart (part); });
            menu.addItem (TRANS ("Show only this track"), onShowOnlyPart != nullptr, false, [this, part] { if (onShowOnlyPart) onShowOnlyPart (part); });
        }
        menu.addItem (TRANS ("Show all tracks"), onShowAllParts != nullptr, false, [this] { if (onShowAllParts) onShowAllParts(); });
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition());
        dragAnchor_ = -1.0;
        return;
    }
    dragAnchor_ = secondsAt (e.position.x);
    edgeDrag_ = loopEdgeAt (e.position.x);
    if (edgeDrag_ != 0)
        return; // moving A or B, anywhere along the edge
    if (e.position.y < static_cast<float> (rulerHeight))
        return; // the ruler: a click seeks on mouse up, a drag selects a loop
    setUserScrolled (false); // the playhead goes where the user looks
    if (onSeekRequested)
        onSeekRequested (dragAnchor_);
}

void PianoRollComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (view_ != nullptr && edgeDrag_ != 0)
    {
        // Edges snap to bars; hold Alt to place them freely.
        auto t = secondsAt (e.position.x);
        if (! e.mods.isAltDown())
            t = snapToBar (t);
        if (edgeDrag_ == 1)
            setLoop (true, std::min (t, loopEnd_ - 0.05), loopEnd_);
        else
            setLoop (true, loopStart_, std::max (t, loopStart_ + 0.05));
        return;
    }
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
    if (view_ != nullptr && edgeDrag_ != 0)
    {
        edgeDrag_ = 0;
        if (onLoopSelected)
            onLoopSelected (loopStart_, loopEnd_);
        return;
    }
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
    {
        setUserScrolled (false);
        onSeekRequested (barAtOrBefore (dragAnchor_));
    } // a click on the bar numbers starts at that bar
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
    setUserScrolled (true);
    repaint();
}

void PianoRollComponent::setUserScrolled (bool scrolled)
{
    if (userScrolled_ == scrolled)
        return;
    userScrolled_ = scrolled;
    if (onUserScrolledChanged)
        onUserScrolledChanged (scrolled);
}

void PianoRollComponent::zoomAround (double newPixelsPerSecond, double anchorSeconds)
{
    newPixelsPerSecond = std::clamp (newPixelsPerSecond, minPixelsPerSecond, maxPixelsPerSecond);
    const double anchorX = (anchorSeconds - viewStartSeconds_) * pixelsPerSecond_;
    pixelsPerSecond_ = newPixelsPerSecond;
    viewStartSeconds_ = std::max (0.0, anchorSeconds - anchorX / pixelsPerSecond_);
    updateScrollBar();
    repaint();
    if (onZoomChanged)
        onZoomChanged (pixelsPerSecond_);
}

void PianoRollComponent::zoomBy (double factor)
{
    const double visible = rollArea().getWidth() / pixelsPerSecond_;
    const bool playheadShown = playheadSeconds_ >= viewStartSeconds_ && playheadSeconds_ <= viewStartSeconds_ + visible;
    zoomAround (pixelsPerSecond_ * factor, playheadShown ? playheadSeconds_ : viewStartSeconds_ + visible * 0.5);
}

void PianoRollComponent::showWholeSong()
{
    if (view_ == nullptr || rollArea().getWidth() <= 0)
        return;
    const double duration = std::max (1.0, view_->durationSeconds);
    pixelsPerSecond_ = std::clamp (rollArea().getWidth() / duration, minPixelsPerSecond, maxPixelsPerSecond);
    viewStartSeconds_ = 0.0;
    setUserScrolled (playheadSeconds_ > rollArea().getWidth() / pixelsPerSecond_); // whole song may still not fit
    updateScrollBar();
    repaint();
    if (onZoomChanged)
        onZoomChanged (pixelsPerSecond_);
}

void PianoRollComponent::goToPlayhead()
{
    const double visible = rollArea().getWidth() / pixelsPerSecond_;
    viewStartSeconds_ = std::max (0.0, playheadSeconds_ - visible * 0.2);
    setUserScrolled (false);
    updateScrollBar();
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
        setUserScrolled (true);
    }
    updateScrollBar();
    repaint();
}

} // namespace pianoassist::ui
