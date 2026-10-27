#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace pianoassist::ui
{

/** The app's colours. Components read these at paint time, so switching the
    theme (applyTheme, then a look-and-feel change on the window) recolours
    everything at once. Message thread only. */
namespace theme
{
inline juce::Colour window { 0xff0f141b };
inline juce::Colour header { 0xff131922 };
inline juce::Colour panel { 0xff1a212b };
inline juce::Colour panelEdge { 0xff273140 };
inline juce::Colour control { 0xff222b37 };
inline juce::Colour outline { 0xff364252 };
inline juce::Colour text { 0xffe8ecf2 };
inline juce::Colour mutedText { 0xff98a4b5 };
inline juce::Colour accent { 0xffa9b4ff };
inline juce::Colour onAccent { 0xff121826 };
inline juce::Colour warning { 0xfff2b84b };
inline juce::Colour good { 0xff6fd3a0 };
inline juce::Colour danger { 0xffef6b6b };
inline juce::Colour rightHand { 0xffa9b4ff }; ///< lilac
inline juce::Colour leftHand { 0xff79d5c0 };  ///< mint
inline juce::Colour backing { 0xff7d8796 };
inline juce::Colour onHand { 0xff121826 };    ///< text on a hand-coloured chip
inline juce::Colour rollBackground { 0xff161d26 };
inline juce::Colour rollBlackRow { 0xff131921 };
inline juce::Colour rollOctaveLine { 0xff2a3442 };
inline juce::Colour rollBarLine { 0xff3a4656 };
inline juce::Colour playhead { 0xfff5c26b };
inline juce::Colour waiting { 0xfff2c94c };

inline bool isLight = false;

inline void applyTheme (bool light)
{
    isLight = light;
    if (light)
    {
        window = juce::Colour (0xfff3f5f9);
        header = juce::Colour (0xffffffff);
        panel = juce::Colour (0xffffffff);
        panelEdge = juce::Colour (0xffdde2ea);
        control = juce::Colour (0xffeef1f6);
        outline = juce::Colour (0xffc5ccd8);
        text = juce::Colour (0xff1b2330);
        mutedText = juce::Colour (0xff5b6677);
        accent = juce::Colour (0xff5b63d6);
        onAccent = juce::Colour (0xffffffff);
        warning = juce::Colour (0xffb26a00);
        good = juce::Colour (0xff1f8a5b);
        danger = juce::Colour (0xffc53030);
        rightHand = juce::Colour (0xff6d75e8);
        leftHand = juce::Colour (0xff16a088);
        backing = juce::Colour (0xff9aa3b1);
        onHand = juce::Colour (0xffffffff);
        rollBackground = juce::Colour (0xfffafbfd);
        rollBlackRow = juce::Colour (0xfff0f2f6);
        rollOctaveLine = juce::Colour (0xffd9dee7);
        rollBarLine = juce::Colour (0xffc3cad6);
        playhead = juce::Colour (0xffe08a00);
        waiting = juce::Colour (0xffd49b00);
    }
    else
    {
        window = juce::Colour (0xff0f141b);
        header = juce::Colour (0xff131922);
        panel = juce::Colour (0xff1a212b);
        panelEdge = juce::Colour (0xff273140);
        control = juce::Colour (0xff222b37);
        outline = juce::Colour (0xff364252);
        text = juce::Colour (0xffe8ecf2);
        mutedText = juce::Colour (0xff98a4b5);
        accent = juce::Colour (0xffa9b4ff);
        onAccent = juce::Colour (0xff121826);
        warning = juce::Colour (0xfff2b84b);
        good = juce::Colour (0xff6fd3a0);
        danger = juce::Colour (0xffef6b6b);
        rightHand = juce::Colour (0xffa9b4ff);
        leftHand = juce::Colour (0xff79d5c0);
        backing = juce::Colour (0xff7d8796);
        onHand = juce::Colour (0xff121826);
        rollBackground = juce::Colour (0xff161d26);
        rollBlackRow = juce::Colour (0xff131921);
        rollOctaveLine = juce::Colour (0xff2a3442);
        rollBarLine = juce::Colour (0xff3a4656);
        playhead = juce::Colour (0xfff5c26b);
        waiting = juce::Colour (0xfff2c94c);
    }
}
} // namespace theme

/** One look for every control, built from the theme colours. Call
    refreshColours after theme::applyTheme. */
class AppLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    AppLookAndFeel() { refreshColours(); }

    void refreshColours()
    {
        using namespace theme;
        setColourScheme ({ window, panel, control, outline, text, control, text, accent, text });
        setColour (juce::ResizableWindow::backgroundColourId, window);
        setColour (juce::DocumentWindow::textColourId, text);
        setColour (juce::TextButton::buttonColourId, control);
        setColour (juce::TextButton::buttonOnColourId, accent);
        setColour (juce::TextButton::textColourOffId, text);
        setColour (juce::TextButton::textColourOnId, onAccent);
        setColour (juce::ComboBox::backgroundColourId, control);
        setColour (juce::ComboBox::textColourId, text);
        setColour (juce::ComboBox::outlineColourId, outline);
        setColour (juce::ComboBox::arrowColourId, mutedText);
        setColour (juce::PopupMenu::backgroundColourId, panel);
        setColour (juce::PopupMenu::textColourId, text);
        setColour (juce::PopupMenu::headerTextColourId, mutedText);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, accent);
        setColour (juce::PopupMenu::highlightedTextColourId, onAccent);
        setColour (juce::ToggleButton::textColourId, text);
        setColour (juce::ToggleButton::tickColourId, accent);
        setColour (juce::ToggleButton::tickDisabledColourId, outline);
        setColour (juce::Slider::thumbColourId, accent);
        setColour (juce::Slider::trackColourId, accent.withAlpha (0.6f));
        setColour (juce::Slider::backgroundColourId, control);
        setColour (juce::Slider::textBoxTextColourId, text);
        setColour (juce::Slider::textBoxBackgroundColourId, control);
        setColour (juce::Slider::textBoxOutlineColourId, outline);
        setColour (juce::TooltipWindow::backgroundColourId, panel);
        setColour (juce::TooltipWindow::textColourId, text);
        setColour (juce::TooltipWindow::outlineColourId, outline);
        setColour (juce::Label::textColourId, text);
        setColour (juce::ScrollBar::thumbColourId, outline);
        setColour (juce::TabbedButtonBar::tabTextColourId, mutedText);
        setColour (juce::TabbedButtonBar::frontTextColourId, text);
        setColour (juce::TabbedButtonBar::tabOutlineColourId, panelEdge);
        setColour (juce::TabbedButtonBar::frontOutlineColourId, accent);
        setColour (juce::TabbedComponent::backgroundColourId, panel);
        setColour (juce::TabbedComponent::outlineColourId, panelEdge);
        setColour (juce::ListBox::backgroundColourId, panel);
        setColour (juce::ListBox::textColourId, text);
        setColour (juce::TextEditor::backgroundColourId, control);
        setColour (juce::TextEditor::textColourId, text);
        setColour (juce::TextEditor::outlineColourId, outline);
        setColour (juce::AlertWindow::backgroundColourId, panel);
        setColour (juce::AlertWindow::textColourId, text);
        setColour (juce::AlertWindow::outlineColourId, outline);
        setColour (juce::GroupComponent::outlineColourId, panelEdge);
        setColour (juce::GroupComponent::textColourId, mutedText);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return juce::FontOptions (std::min (15.0f, static_cast<float> (buttonHeight) * 0.5f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = backgroundColour;
        if (! button.isEnabled())
            colour = colour.withMultipliedAlpha (0.5f);
        else if (down)
            colour = colour.contrasting (0.15f);
        else if (highlighted)
            colour = colour.contrasting (0.07f);
        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (button.getToggleState() ? colour : theme::outline.withAlpha (0.8f));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
    }

    juce::Font getPopupMenuFont() override { return juce::FontOptions (15.0f); }
    juce::Font getMenuBarFont (juce::MenuBarComponent&, int, const juce::String&) override
    {
        return juce::FontOptions (15.0f);
    }

    void drawMenuBarBackground (juce::Graphics& g, int width, int height, bool, juce::MenuBarComponent&) override
    {
        g.fillAll (theme::header);
        juce::ignoreUnused (width, height);
    }

    void drawMenuBarItem (juce::Graphics& g, int width, int height, int, const juce::String& itemText, bool isMouseOverItem,
                          bool isMenuOpen, bool, juce::MenuBarComponent&) override
    {
        const auto box = juce::Rectangle<float> (0.0f, 4.0f, static_cast<float> (width), static_cast<float> (height - 8));
        if (isMenuOpen || isMouseOverItem)
        {
            g.setColour (theme::control);
            g.fillRoundedRectangle (box, 6.0f);
        }
        g.setColour (theme::text);
        g.setFont (juce::FontOptions (15.0f));
        g.drawFittedText (itemText, box.toNearestInt(), juce::Justification::centred, 1);
    }
};

} // namespace pianoassist::ui
