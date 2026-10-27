#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoassist::ui
{

/** One dark theme for the whole app: calm slate panels, one blue accent,
    high-contrast text. Colours here are the only ones controls use. */
namespace theme
{
inline const juce::Colour window { 0xff14181c };
inline const juce::Colour panel { 0xff1f252b };
inline const juce::Colour panelEdge { 0xff2e3740 };
inline const juce::Colour control { 0xff28313a };
inline const juce::Colour outline { 0xff3d4954 };
inline const juce::Colour text { 0xffe8ecef };
inline const juce::Colour mutedText { 0xff9aa6b1 };
inline const juce::Colour accent { 0xff4a90e2 };
inline const juce::Colour warning { 0xfff2a33a };
inline const juce::Colour good { 0xff6fcf97 };
} // namespace theme

class AppLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel()
        : LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme { theme::window, theme::panel, theme::control,
                                                                 theme::outline, theme::text, theme::control,
                                                                 theme::text, theme::accent, theme::text })
    {
        setColour (juce::ResizableWindow::backgroundColourId, theme::window);
        setColour (juce::TextButton::buttonColourId, theme::control);
        setColour (juce::TextButton::textColourOffId, theme::text);
        setColour (juce::ComboBox::backgroundColourId, theme::control);
        setColour (juce::ComboBox::outlineColourId, theme::outline);
        setColour (juce::ComboBox::arrowColourId, theme::mutedText);
        setColour (juce::PopupMenu::backgroundColourId, theme::panel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent);
        setColour (juce::ToggleButton::tickColourId, theme::accent);
        setColour (juce::ToggleButton::tickDisabledColourId, theme::outline);
        setColour (juce::Slider::thumbColourId, theme::accent);
        setColour (juce::Slider::trackColourId, theme::accent.withAlpha (0.6f));
        setColour (juce::Slider::backgroundColourId, theme::control);
        setColour (juce::TooltipWindow::backgroundColourId, theme::panel.brighter (0.1f));
        setColour (juce::TooltipWindow::textColourId, theme::text);
        setColour (juce::Label::textColourId, theme::text);
        setColour (juce::ScrollBar::thumbColourId, theme::outline);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (std::min (15.0f, static_cast<float> (buttonHeight) * 0.55f));
    }
};

} // namespace pianoassist::ui
