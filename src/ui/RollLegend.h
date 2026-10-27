#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <vector>

namespace pianoassist::ui
{

/** The piano roll's legend: one chip per shown track (eye, colour, name,
    hand, Mute/Solo state) and the view options. Wraps onto more lines when
    the roll is narrow. Changes only what is shown, never what sounds. */
class RollLegend final : public juce::Component, public juce::TooltipClient
{
public:
    struct Item
    {
        int part = -1;
        juce::Colour colour;
        juce::String name;
        juce::String tooltip;
        bool visible = true;
        bool bright = true;   ///< heard now, or played by the learner
        bool selected = false;
        bool muted = false;
        bool solo = false;
        bool checked = false; ///< required in practice
        int hand = 0;         ///< 0 none, 1 right, 2 left, 3 both
    };

    std::function<void (int part, bool visible)> onVisibleChanged;
    std::function<void (int part)> onSelect;
    std::function<void (int part)> onShowOnly;
    std::function<void()> onShowAll;
    std::function<void()> onClearSolos;
    std::function<void (bool byTrack)> onColourByTrack;
    std::function<void (bool pianoOnly)> onPianoOnly;
    std::function<void (bool hide)> onHideDimmed;
    std::function<void()> onMore; ///< the "+N" pill: open the full track list

    RollLegend();

    /** tab 0 = my parts (hand or track colours), 1 = all tracks (piano filter). */
    void setItems (std::vector<Item> items, int tab, bool colourByTrack, bool pianoOnly, bool hideDimmed, bool anySolo);
    int heightFor (int width) const;

    void paint (juce::Graphics& g) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    juce::String getTooltip() override { return tooltip_; }

private:
    struct Chip
    {
        juce::Rectangle<int> bounds, eye;
        std::size_t item = 0;
    };
    int layoutChips (int width, std::vector<Chip>* out) const;
    int chipWidth (const Item& item) const;
    int optionWidth (const juce::Component& c) const;
    int optionsWidth() const;

    std::vector<Item> items_;
    std::vector<Chip> chips_;
    juce::Rectangle<int> more_;
    int hiddenCount_ = 0;
    int tab_ = 0;
    bool anySolo_ = false;
    juce::String tooltip_;
    juce::TextButton byHand_, byTrack_, showAll_, clearSolos_;
    juce::ToggleButton pianoOnly_, hideDimmed_;
};

} // namespace pianoassist::ui
