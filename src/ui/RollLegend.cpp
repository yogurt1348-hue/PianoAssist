#include "ui/RollLegend.h"

#include "ui/AppLookAndFeel.h"

#include <algorithm>

namespace pianoassist::ui
{
namespace
{
constexpr int rowHeight = 26;
constexpr int gap = 6;
constexpr float fontSize = 13.0f;

juce::String handText (int hand)
{
    switch (hand)
    {
        case 1: return TRANS ("R");
        case 2: return TRANS ("L");
        case 3: return TRANS ("R+L");
        default: return {};
    }
}
} // namespace

RollLegend::RollLegend()
{
    byHand_.setButtonText (TRANS ("By hands"));
    byTrack_.setButtonText (TRANS ("By tracks"));
    byHand_.setTooltip (TRANS ("Notes in hand colours: right and left"));
    byTrack_.setTooltip (TRANS ("Notes in track colours; a stripe on each note still shows the hand"));
    byHand_.setRadioGroupId (41);
    byTrack_.setRadioGroupId (41);
    byHand_.setClickingTogglesState (true);
    byTrack_.setClickingTogglesState (true);
    byHand_.setConnectedEdges (juce::Button::ConnectedOnRight);
    byTrack_.setConnectedEdges (juce::Button::ConnectedOnLeft);
    byHand_.onClick = [this] { if (byHand_.getToggleState() && onColourByTrack) onColourByTrack (false); };
    byTrack_.onClick = [this] { if (byTrack_.getToggleState() && onColourByTrack) onColourByTrack (true); };
    pianoOnly_.setButtonText (TRANS ("Piano parts"));
    pianoOnly_.setTooltip (TRANS ("Show only the piano parts. Changes neither sound, Solo nor what you have to play."));
    pianoOnly_.onClick = [this] { if (onPianoOnly) onPianoOnly (pianoOnly_.getToggleState()); };
    hideDimmed_.setButtonText (TRANS ("Hide silent"));
    hideDimmed_.setTooltip (TRANS ("Leave out tracks that are not heard now instead of drawing them faint"));
    hideDimmed_.onClick = [this] { if (onHideDimmed) onHideDimmed (hideDimmed_.getToggleState()); };
    showAll_.setButtonText (TRANS ("Show all"));
    showAll_.onClick = [this] { if (onShowAll) onShowAll(); };
    clearSolos_.setButtonText (TRANS ("Clear all Solo"));
    clearSolos_.onClick = [this] { if (onClearSolos) onClearSolos(); };
    for (auto* c : std::initializer_list<juce::Component*> { &byHand_, &byTrack_, &pianoOnly_, &hideDimmed_, &showAll_, &clearSolos_ })
        addAndMakeVisible (c);
}

void RollLegend::setItems (std::vector<Item> items, int tab, bool colourByTrack, bool pianoOnly, bool hideDimmed, bool anySolo)
{
    items_ = std::move (items);
    tab_ = tab;
    anySolo_ = anySolo;
    byHand_.setToggleState (! colourByTrack, juce::dontSendNotification);
    byTrack_.setToggleState (colourByTrack, juce::dontSendNotification);
    pianoOnly_.setToggleState (pianoOnly, juce::dontSendNotification);
    hideDimmed_.setToggleState (hideDimmed, juce::dontSendNotification);
    byHand_.setVisible (tab == 0);
    byTrack_.setVisible (tab == 0);
    pianoOnly_.setVisible (tab == 1);
    clearSolos_.setVisible (anySolo);
    resized();
    repaint();
}

int RollLegend::chipWidth (const Item& item) const
{
    const juce::Font font (juce::FontOptions (fontSize, item.selected ? juce::Font::bold : juce::Font::plain));
    juce::String extra = handText (item.hand);
    if (item.muted)
        extra << " M";
    if (item.solo)
        extra << " S";
    const auto text = juce::GlyphArrangement::getStringWidthInt (font, item.name)
                      + (extra.isEmpty() ? 0 : juce::GlyphArrangement::getStringWidthInt (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)), extra) + 8);
    return std::min (260, 24 + 18 + text + 12);
}

int RollLegend::optionWidth (const juce::Component& c) const
{
    return &c == &byHand_ || &c == &byTrack_ ? 104 : &c == &hideDimmed_ ? 170 : &c == &pianoOnly_ ? 190 : &c == &clearSolos_ ? 150 : 110;
}

int RollLegend::optionsWidth() const
{
    // From the fixed widths, not getWidth(): an option just shown has none yet.
    int w = 0;
    for (const auto* c : std::initializer_list<const juce::Component*> { &byHand_, &byTrack_, &pianoOnly_, &hideDimmed_, &showAll_, &clearSolos_ })
        if (c->isVisible())
            w += optionWidth (*c) + gap;
    return w;
}

int RollLegend::layoutChips (int width, std::vector<Chip>* out) const
{
    // Options first on the right of the first line; chips fill the lines.
    int x = 0, y = 0;
    const int firstLineWidth = width - optionsWidth() - 8;
    for (std::size_t i = 0; i < items_.size(); ++i)
    {
        const int w = std::min (chipWidth (items_[i]), width);
        const int lineWidth = y == 0 ? firstLineWidth : width;
        // A chip never goes under the options: when even the first one does
        // not fit beside them, the chips start on the next line.
        if (x + w > lineWidth && (x > 0 || y == 0))
        {
            x = 0;
            y += rowHeight + 4;
        }
        if (out != nullptr)
        {
            Chip chip;
            chip.bounds = { x, y, w, rowHeight };
            chip.eye = chip.bounds.withWidth (24);
            chip.item = i;
            out->push_back (chip);
        }
        x += w + gap;
    }
    return y + rowHeight;
}

int RollLegend::heightFor (int width) const
{
    return layoutChips (width, nullptr);
}

void RollLegend::resized()
{
    chips_.clear();
    layoutChips (getWidth(), &chips_);
    // Chips below the legend's height are hidden; a "+N" pill at the end of
    // the last line says how many and opens the full list.
    more_ = {};
    hiddenCount_ = 0;
    const auto fits = [this] (const Chip& c) { return c.bounds.getBottom() <= getHeight(); };
    if (! std::all_of (chips_.begin(), chips_.end(), fits))
    {
        int lastLine = 0;
        for (const auto& c : chips_)
            if (fits (c))
                lastLine = std::max (lastLine, c.bounds.getY());
        constexpr int moreWidth = 56;
        chips_.erase (std::remove_if (chips_.begin(), chips_.end(),
                                      [&] (const Chip& c) {
                                          return ! fits (c) || (c.bounds.getY() == lastLine && c.bounds.getRight() > getWidth() - moreWidth - gap);
                                      }),
                      chips_.end());
        hiddenCount_ = static_cast<int> (items_.size() - chips_.size());
        int x = 0;
        for (const auto& c : chips_)
            if (c.bounds.getY() == lastLine)
                x = std::max (x, c.bounds.getRight() + gap);
        more_ = { x, lastLine, moreWidth, rowHeight };
    }
    auto row = getLocalBounds().removeFromTop (rowHeight);
    for (auto* c : std::initializer_list<juce::Component*> { &clearSolos_, &showAll_, &hideDimmed_, &pianoOnly_, &byTrack_, &byHand_ })
    {
        if (! c->isVisible())
            continue;
        c->setBounds (row.removeFromRight (optionWidth (*c)));
        row.removeFromRight (c == &byTrack_ ? 0 : gap);
    }
}

void RollLegend::paint (juce::Graphics& g)
{
    for (const auto& chip : chips_)
    {
        const auto& item = items_[chip.item];
        auto r = chip.bounds.toFloat().reduced (0.5f);
        g.setColour (item.selected ? theme::control.brighter (0.08f) : theme::control);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (item.selected ? theme::accent : theme::outline);
        g.drawRoundedRectangle (r, 6.0f, item.selected ? 1.6f : 1.0f);

        // Eye: shown or hidden on the roll.
        const auto eye = chip.eye.toFloat().reduced (5.0f, 8.0f);
        juce::Path p;
        p.startNewSubPath (eye.getX(), eye.getCentreY());
        p.quadraticTo (eye.getCentreX(), eye.getY() - 3.0f, eye.getRight(), eye.getCentreY());
        p.quadraticTo (eye.getCentreX(), eye.getBottom() + 3.0f, eye.getX(), eye.getCentreY());
        g.setColour (item.visible ? theme::text : theme::mutedText);
        g.strokePath (p, juce::PathStrokeType (1.3f));
        if (item.visible)
            g.fillEllipse (eye.withSizeKeepingCentre (4.0f, 4.0f));
        else
            g.drawLine (eye.getX() + 1.0f, eye.getBottom() + 2.0f, eye.getRight() - 1.0f, eye.getY() - 2.0f, 1.4f);

        auto rest = chip.bounds.withTrimmedLeft (24).toFloat();
        const auto swatch = rest.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (item.colour.withAlpha (item.bright ? 1.0f : 0.4f));
        g.fillRoundedRectangle (swatch, 3.0f);
        rest.removeFromLeft (4.0f);

        juce::String extra = handText (item.hand);
        auto tags = rest;
        if (item.muted || item.solo || item.hand != 0)
        {
            const juce::Font tagFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.setFont (tagFont);
            auto right = rest.removeFromRight (static_cast<float> (juce::GlyphArrangement::getStringWidthInt (
                                                   tagFont, extra + (item.muted ? " M" : "") + (item.solo ? " S" : "")) + 8));
            tags = right;
            if (item.hand != 0)
            {
                g.setColour (item.hand == 2 ? theme::leftHand : theme::rightHand);
                const auto w = static_cast<float> (juce::GlyphArrangement::getStringWidthInt (tagFont, extra)) + 2.0f;
                g.drawText (extra, tags.removeFromLeft (w), juce::Justification::centredLeft, false);
            }
            if (item.muted)
            {
                g.setColour (theme::warning);
                g.drawText (" M", tags.removeFromLeft (14.0f), juce::Justification::centredLeft, false);
            }
            if (item.solo)
            {
                g.setColour (item.muted ? theme::mutedText : theme::good);
                g.drawText (" S", tags.removeFromLeft (14.0f), juce::Justification::centredLeft, false);
            }
        }
        g.setColour (item.bright ? theme::text : theme::mutedText);
        g.setFont (juce::FontOptions (fontSize, item.selected ? juce::Font::bold : juce::Font::plain));
        g.drawText (item.name, rest, juce::Justification::centredLeft, true);
    }
    if (hiddenCount_ > 0)
    {
        const auto r = more_.toFloat().reduced (0.5f);
        g.setColour (theme::control);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (theme::outline);
        g.drawRoundedRectangle (r, 6.0f, 1.0f);
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (fontSize, juce::Font::bold));
        g.drawText ("+" + juce::String (hiddenCount_), more_, juce::Justification::centred, false);
    }
}

void RollLegend::mouseMove (const juce::MouseEvent& e)
{
    juce::String tip;
    if (hiddenCount_ > 0 && more_.contains (e.getPosition()))
        tip = juce::String (hiddenCount_) + " " + TRANS ("more tracks do not fit here. Click to see every track in Tracks and instruments.");
    for (const auto& chip : chips_)
        if (chip.bounds.contains (e.getPosition()))
        {
            const auto& item = items_[chip.item];
            tip = chip.eye.contains (e.getPosition()) ? (item.visible ? TRANS ("Hide on the piano roll") : TRANS ("Show on the piano roll"))
                                                      : item.tooltip;
        }
    tooltip_ = tip;
}

void RollLegend::mouseDown (const juce::MouseEvent& e)
{
    if (hiddenCount_ > 0 && more_.contains (e.getPosition()))
    {
        if (onMore)
            onMore();
        return;
    }
    for (const auto& chip : chips_)
    {
        if (! chip.bounds.contains (e.getPosition()))
            continue;
        const auto& item = items_[chip.item];
        const int part = item.part;
        if (e.mods.isPopupMenu())
        {
            juce::PopupMenu menu;
            menu.addItem (TRANS ("Show only this track"), [this, part] { if (onShowOnly) onShowOnly (part); });
            menu.addItem (TRANS ("Show all"), [this] { if (onShowAll) onShowAll(); });
            menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
        }
        else if (chip.eye.contains (e.getPosition()))
        {
            if (onVisibleChanged)
                onVisibleChanged (part, ! item.visible);
        }
        else if (onSelect)
            onSelect (item.selected ? -1 : part);
        return;
    }
}

} // namespace pianoassist::ui
