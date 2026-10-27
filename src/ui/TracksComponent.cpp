#include "ui/TracksComponent.h"

#include "ui/AppLookAndFeel.h"
#include "ui/TrackColours.h"

#include <algorithm>
#include <cmath>

namespace pianoassist::ui
{
namespace
{
juce::String toJuce (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }

constexpr int mainSoundId = 1000;
constexpr int addSoundsId = 900;

juce::Colour roleColourOf (int assignment)
{
    switch (assignment)
    {
        case 0: return theme::rightHand;
        case 1: return theme::leftHand;
        case 2: return theme::rightHand.interpolatedWith (theme::leftHand, 0.5f);
        case 4: return theme::mutedText.withAlpha (0.6f);
        default: return theme::backing.brighter (0.3f);
    }
}

void setLabel (juce::Label& label, float size, bool bold, juce::Colour colour)
{
    label.setFont (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
    label.setColour (juce::Label::textColourId, colour);
    label.setMinimumHorizontalScale (1.0f); // wrap, never squeeze letters
    label.setJustificationType (juce::Justification::topLeft);
    label.setInterceptsMouseClicks (false, false);
}

/** Mute and Solo: the state must read without colour too (filled vs outline, and the text). */
class StateButton final : public juce::Button
{
public:
    StateButton (const juce::String& letter, juce::Colour onColour) : juce::Button (letter), onColour_ (onColour) {}

    bool overridden = false; ///< Solo on a muted track: drawn crossed, Mute wins

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        const bool on = getToggleState();
        if (on && ! overridden)
        {
            g.setColour (onColour_.withMultipliedBrightness (down ? 0.85f : 1.0f));
            g.fillRoundedRectangle (r, 5.0f);
        }
        else
        {
            g.setColour (theme::control.brighter (over ? 0.12f : 0.0f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (on ? onColour_ : theme::outline);
            g.drawRoundedRectangle (r, 5.0f, on ? 2.0f : 1.0f);
        }
        g.setColour (on && ! overridden ? theme::onHand : theme::text);
        g.setFont (juce::FontOptions (14.0f, juce::Font::bold));
        g.drawText (getButtonText(), r, juce::Justification::centred);
        if (overridden && on)
        {
            g.setColour (onColour_);
            g.drawLine (r.getX() + 5.0f, r.getBottom() - 5.0f, r.getRight() - 5.0f, r.getY() + 5.0f, 1.5f);
        }
    }

private:
    juce::Colour onColour_;
};

/** The "eye": shown or hidden on the piano roll. */
class EyeButton final : public juce::Button
{
public:
    EyeButton() : juce::Button ("eye") {}

    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        const bool shown = getToggleState();
        const auto c = (shown ? theme::text : theme::mutedText).withAlpha (over ? 1.0f : 0.85f);
        const auto eye = r.withSizeKeepingCentre (std::min (r.getWidth(), 20.0f), std::min (r.getHeight(), 12.0f));
        juce::Path p;
        p.startNewSubPath (eye.getX(), eye.getCentreY());
        p.quadraticTo (eye.getCentreX(), eye.getY() - 3.0f, eye.getRight(), eye.getCentreY());
        p.quadraticTo (eye.getCentreX(), eye.getBottom() + 3.0f, eye.getX(), eye.getCentreY());
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f));
        if (shown)
            g.fillEllipse (eye.withSizeKeepingCentre (5.0f, 5.0f));
        else
            g.drawLine (eye.getX() + 2.0f, eye.getBottom() + 1.0f, eye.getRight() - 2.0f, eye.getY() - 1.0f, 1.6f);
    }
};

/** A square in the track colour that opens the colour choice. */
class SwatchButton final : public juce::Button
{
public:
    SwatchButton() : juce::Button ("colour") {}
    juce::Colour colour;
    void paintButton (juce::Graphics& g, bool over, bool) override
    {
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (r, 4.0f);
        g.setColour (over ? theme::text : theme::outline);
        g.drawRoundedRectangle (r, 4.0f, 1.0f);
    }
};

/** Colour choice: swatches, a HEX field and "Automatic". */
class ColourPanel final : public juce::Component
{
public:
    ColourPanel (juce::Colour current, bool automatic, std::function<void (std::optional<std::uint32_t>)> chosen)
        : chosen_ (std::move (chosen))
    {
        for (const auto argb : trackcolours::swatches)
        {
            auto* b = swatches_.add (new SwatchButton());
            b->colour = juce::Colour (argb);
            b->setTooltip (toJuce (hexColour (argb)));
            b->onClick = [this, argb] { pick (argb); };
            addAndMakeVisible (b);
        }
        hex_.setText (toJuce (hexColour (current.getARGB())), false);
        hex_.setTooltip (TRANS ("Colour as #RRGGBB"));
        hex_.onReturnKey = [this] { applyHex(); };
        hex_.onFocusLost = [this] { applyHex(); };
        addAndMakeVisible (hex_);
        automatic_.setButtonText (automatic ? TRANS ("Automatic (now)") : TRANS ("Automatic"));
        automatic_.onClick = [this] { if (chosen_) chosen_ (std::nullopt); dismiss(); };
        addAndMakeVisible (automatic_);
        setSize (6 * 30 + 16, 3 * 30 + 16 + 34 + 8);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8);
        for (int i = 0; i < swatches_.size(); ++i)
            swatches_[i]->setBounds (r.getX() + (i % 6) * 30, r.getY() + (i / 6) * 30, 28, 28);
        auto bottom = r.removeFromBottom (30);
        hex_.setBounds (bottom.removeFromLeft (92));
        bottom.removeFromLeft (6);
        automatic_.setBounds (bottom);
    }

private:
    void pick (std::uint32_t argb)
    {
        if (chosen_)
            chosen_ (argb);
        dismiss();
    }
    void applyHex()
    {
        if (const auto value = parseHexColour (hex_.getText().toStdString()))
        {
            if (chosen_)
                chosen_ (*value);
        }
        else
            hex_.setColour (juce::TextEditor::outlineColourId, theme::danger);
    }
    void dismiss()
    {
        if (auto* box = findParentComponentOfClass<juce::CallOutBox>())
            box->dismiss();
    }

    std::function<void (std::optional<std::uint32_t>)> chosen_;
    juce::OwnedArray<SwatchButton> swatches_;
    juce::TextEditor hex_;
    juce::TextButton automatic_;
};
} // namespace

// --- one track's channel ----------------------------------------------------------------------

class TracksComponent::Strip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    Strip (TracksComponent& owner, std::size_t part) : owner_ (owner), part_ (part)
    {
        swatch_.setTooltip (TRANS ("Track colour"));
        swatch_.onClick = [this] { owner_.showColourMenu (part_, swatch_); };
        addAndMakeVisible (swatch_);
        eye_.setClickingTogglesState (false);
        eye_.onClick = [this] {
            if (owner_.intents_.visibleChanged)
                owner_.intents_.visibleChanged (part_, ! row().visible);
        };
        addAndMakeVisible (eye_);
        setLabel (name_, 14.0f, true, theme::text);
        setLabel (sub_, 11.5f, false, theme::mutedText);
        setLabel (role_, 12.0f, false, theme::text);
        setLabel (state_, 12.0f, false, theme::mutedText);
        for (auto* l : { &name_, &sub_, &role_, &state_ })
            addAndMakeVisible (*l);

        mute_.setTooltip (TRANS ("Mute: silence this track. What you have to play does not change."));
        solo_.setTooltip (TRANS ("Solo: hear only the solo tracks. Several can be solo; Mute wins over Solo."));
        mute_.onClick = [this] { if (owner_.intents_.muteChanged) owner_.intents_.muteChanged (part_, ! row().muted); };
        solo_.onClick = [this] { if (owner_.intents_.soloChanged) owner_.intents_.soloChanged (part_, ! row().solo); };
        addAndMakeVisible (mute_);
        addAndMakeVisible (solo_);

        fader_.setRange (0.0, 1.0, 0.01);
        fader_.setTitle (TRANS ("Volume"));
        fader_.onValueChange = [this] {
            value_.setText (juce::String (juce::roundToInt (fader_.getValue() * 100.0)) + "%", juce::dontSendNotification);
            if (owner_.intents_.volumeChanged)
                owner_.intents_.volumeChanged (part_, fader_.getValue());
        };
        fader_.setDoubleClickReturnValue (true, 1.0);
        addAndMakeVisible (fader_);
        setLabel (value_, 12.0f, false, theme::text);
        value_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (value_);

        editor_.setButtonText (TRANS ("Instrument"));
        editor_.setTooltip (TRANS ("Open this track's instrument window"));
        editor_.onClick = [this] { if (owner_.intents_.openEditor) owner_.intents_.openEditor (part_); };
        addChildComponent (editor_);
        setWantsKeyboardFocus (false);
    }

    const TrackRowView& row() const { return owner_.rows_[part_]; }
    std::size_t part() const { return part_; }

    void refresh()
    {
        const auto& r = row();
        swatch_.colour = juce::Colour (r.colour);
        swatch_.repaint();
        eye_.setToggleState (r.visible, juce::dontSendNotification);
        eye_.setTooltip (r.visible ? TRANS ("Shown on the piano roll. Click to hide (sound and practice stay as they are).")
                                   : TRANS ("Hidden on the piano roll. Click to show."));
        name_.setText (r.displayName(), juce::dontSendNotification);
        name_.setTooltip (toJuce (r.label));
        setTooltip (toJuce (r.label));
        // The file's instrument, or the preset chosen instead (marked).
        sub_.setText ((r.presetName.empty() ? toJuce (r.instrument) : juce::String::fromUTF8 ("\xe2\x99\xaa ") + toJuce (r.presetName)) + "\n"
                          + TRANS ("Track") + " " + juce::String (r.track) + " · " + TRANS ("ch") + " " + juce::String (r.channel),
                      juce::dontSendNotification);
        role_.setText (roleName (r.assignment) + (r.guessed ? " ?" : ""), juce::dontSendNotification);
        role_.setColour (juce::Label::textColourId, roleColourOf (r.assignment));
        juce::String state = r.checked ? TRANS ("Checked in practice") : juce::String();
        const auto quiet = quietText (r);
        if (quiet.isNotEmpty())
            state << (state.isEmpty() ? "" : "\n") << quiet;
        state_.setText (state, juce::dontSendNotification);
        state_.setColour (juce::Label::textColourId, r.checked ? theme::waiting : theme::mutedText);

        mute_.setToggleState (r.muted, juce::dontSendNotification);
        solo_.setToggleState (r.solo, juce::dontSendNotification);
        solo_.overridden = r.muted && r.solo;
        solo_.setTooltip (r.muted && r.solo ? TRANS ("Solo is on, but Mute wins: this track stays silent.")
                                            : TRANS ("Solo: hear only the solo tracks. Several can be solo; Mute wins over Solo."));
        mute_.repaint();
        solo_.repaint();
        if (std::abs (fader_.getValue() - r.volume) > 1.0e-6)
            fader_.setValue (r.volume, juce::dontSendNotification);
        value_.setText (juce::String (juce::roundToInt (r.volume * 100.0)) + "%", juce::dontSendNotification);
        const bool excluded = r.assignment == 4;
        for (auto* c : std::initializer_list<juce::Component*> { &mute_, &solo_, &fader_ })
            c->setEnabled (! excluded);
        editor_.setVisible (r.hasEditor);
        resized();
        repaint();
    }

    void setActive (bool active)
    {
        if (active_ != active)
        {
            active_ = active;
            repaint (ledArea_);
        }
    }

    void paint (juce::Graphics& g) override
    {
        const auto& r = row();
        const bool selected = static_cast<int> (part_) == owner_.selected_;
        auto bounds = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
        g.setColour (selected ? theme::control.brighter (0.06f) : theme::panel);
        g.fillRoundedRectangle (bounds, 7.0f);
        // Colour only in the strip at the top: text and surfaces stay neutral.
        g.setColour (juce::Colour (r.colour).withAlpha (r.heard || r.checked ? 1.0f : 0.45f));
        g.fillRoundedRectangle (bounds.removeFromTop (7.0f), 3.0f);
        g.setColour (selected ? theme::accent : theme::panelEdge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f, 2.0f), 7.0f, selected ? 2.0f : 1.0f);

        // MIDI activity: a note of this track is playing (from the song, not a level).
        auto led = ledArea_.toFloat();
        const bool lit = active_ && r.heard;
        g.setColour (lit ? juce::Colour (r.colour) : theme::control.brighter (0.1f));
        g.fillEllipse (led.removeFromLeft (led.getHeight()).reduced (2.0f));
        g.setColour (lit ? theme::text : theme::mutedText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("MIDI", led.withTrimmedLeft (4.0f), juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        r.removeFromTop (8);
        auto top = r.removeFromTop (22);
        swatch_.setBounds (top.removeFromLeft (22));
        eye_.setBounds (top.removeFromRight (26));
        r.removeFromTop (4);
        name_.setBounds (r.removeFromTop (38));
        sub_.setBounds (r.removeFromTop (46));
        role_.setBounds (r.removeFromTop (18));
        state_.setBounds (r.removeFromTop (32));
        r.removeFromTop (4);
        auto ms = r.removeFromTop (28);
        mute_.setBounds (ms.removeFromLeft (ms.getWidth() / 2 - 2));
        ms.removeFromLeft (4);
        solo_.setBounds (ms);
        r.removeFromTop (6);
        if (row().hasEditor)
        {
            editor_.setBounds (r.removeFromBottom (26));
            r.removeFromBottom (6);
        }
        ledArea_ = r.removeFromBottom (18);
        r.removeFromBottom (4);
        value_.setBounds (r.removeFromBottom (18));
        fader_.setBounds (r.withSizeKeepingCentre (36, r.getHeight()));
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
            owner_.showStripMenu (part_);
        else if (owner_.intents_.selectPart)
            owner_.intents_.selectPart (static_cast<int> (part_));
    }

private:
    TracksComponent& owner_;
    std::size_t part_;
    SwatchButton swatch_;
    EyeButton eye_;
    juce::Label name_, sub_, role_, state_, value_;
    StateButton mute_ { "M", theme::warning }, solo_ { "S", theme::good };
    juce::Slider fader_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::TextButton editor_;
    juce::Rectangle<int> ledArea_;
    bool active_ = false;
};

// --- Master --------------------------------------------------------------------------------

class TracksComponent::MasterStrip final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit MasterStrip (TracksComponent& owner) : owner_ (owner)
    {
        setLabel (title_, 14.0f, true, theme::text);
        title_.setText (TRANS ("Master"), juce::dontSendNotification);
        setLabel (sub_, 12.0f, false, theme::mutedText);
        sub_.setText (TRANS ("All sound from PianoAssist"), juce::dontSendNotification);
        addAndMakeVisible (title_);
        addAndMakeVisible (sub_);
        fader_.setRange (0.0, 1.0, 0.01);
        fader_.setTitle (TRANS ("Main volume"));
        fader_.onValueChange = [this] {
            value_.setText (juce::String (juce::roundToInt (fader_.getValue() * 100.0)) + "%", juce::dontSendNotification);
            if (owner_.intents_.masterChanged)
                owner_.intents_.masterChanged (fader_.getValue());
        };
        addAndMakeVisible (fader_);
        setLabel (value_, 12.0f, false, theme::text);
        value_.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (value_);
        setTooltip (TRANS ("Master: the measured level of everything PianoAssist plays. Your piano's own sound is not included."));
    }

    void set (const MasterView& v)
    {
        if (std::abs (fader_.getValue() - v.gain) > 1.0e-6 && ! fader_.isMouseButtonDown())
            fader_.setValue (v.gain, juce::dontSendNotification);
        value_.setText (juce::String (juce::roundToInt (fader_.getValue() * 100.0)) + "%", juce::dontSendNotification);
        // Peaks fall back slowly so short notes are still seen.
        levels_[0] = std::max (v.peakLeft, levels_[0] * 0.82f);
        levels_[1] = std::max (v.peakRight, levels_[1] * 0.82f);
        repaint (meter_);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (3.0f, 2.0f);
        g.setColour (theme::panel);
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (theme::accent);
        g.fillRoundedRectangle (bounds.removeFromTop (7.0f), 3.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (3.0f, 2.0f), 7.0f, 1.0f);

        // Real output level, in dB from -48 to 0.
        auto m = meter_.toFloat();
        const float w = (m.getWidth() - 4.0f) / 2.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            auto bar = juce::Rectangle<float> (m.getX() + static_cast<float> (ch) * (w + 4.0f), m.getY(), w, m.getHeight());
            g.setColour (theme::control);
            g.fillRoundedRectangle (bar, 2.0f);
            const float db = levels_[ch] > 1.0e-5f ? 20.0f * std::log10 (levels_[ch]) : -100.0f;
            const float fill = std::clamp ((db + 48.0f) / 48.0f, 0.0f, 1.0f);
            const auto lit = bar.withTrimmedTop (bar.getHeight() * (1.0f - fill));
            g.setColour (db > -1.0f ? theme::danger : db > -9.0f ? theme::warning : theme::good);
            g.fillRoundedRectangle (lit, 2.0f);
        }
        g.setColour (theme::mutedText);
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (TRANS ("Level"), meter_.withY (meter_.getBottom() + 2).withHeight (14).expanded (20, 0), juce::Justification::centred);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (10, 6);
        r.removeFromTop (8);
        title_.setBounds (r.removeFromTop (22));
        sub_.setBounds (r.removeFromTop (34));
        r.removeFromTop (8);
        value_.setBounds (r.removeFromBottom (18));
        r.removeFromBottom (4);
        meter_ = r.removeFromLeft (26).withTrimmedBottom (18);
        r.removeFromLeft (6);
        fader_.setBounds (r);
    }

private:
    TracksComponent& owner_;
    juce::Label title_, sub_, value_;
    juce::Slider fader_ { juce::Slider::LinearVertical, juce::Slider::NoTextBox };
    juce::Rectangle<int> meter_;
    float levels_[2] { 0.0f, 0.0f };
};

// --- inspector -----------------------------------------------------------------------------

class TracksComponent::Inspector final : public juce::Component
{
public:
    explicit Inspector (TracksComponent& owner) : owner_ (owner)
    {
        setLabel (title_, 16.0f, true, theme::text);
        setLabel (info_, 13.0f, false, theme::mutedText);
        setLabel (roleCaption_, 12.0f, true, theme::mutedText);
        setLabel (soundCaption_, 12.0f, true, theme::mutedText);
        setLabel (routing_, 13.0f, false, theme::text);
        setLabel (status_, 13.0f, false, theme::text);
        setLabel (colourCaption_, 12.0f, true, theme::mutedText);
        roleCaption_.setText (TRANS ("Role"), juce::dontSendNotification);
        soundCaption_.setText (TRANS ("Sound source"), juce::dontSendNotification);
        colourCaption_.setText (TRANS ("Colour"), juce::dontSendNotification);
        for (auto* l : { &title_, &info_, &roleCaption_, &soundCaption_, &routing_, &status_, &colourCaption_ })
            addAndMakeVisible (*l);

        for (int a = 0; a <= 4; ++a)
            role_.addItem (roleName (a), a + 1);
        role_.setTooltip (TRANS ("Right or left hand: you play it and it is checked. Accompaniment: the app plays it. "
                                 "Excluded: neither checked nor played."));
        role_.onChange = [this] {
            const int chosen = role_.getSelectedId() - 1;
            const auto p = owner_.selected_;
            if (p < 0 || chosen < 0 || chosen == owner_.rows_[static_cast<std::size_t> (p)].assignment)
                return;
            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TracksComponent> (&owner_), p, chosen] {
                if (safe != nullptr && safe->intents_.assignmentChanged)
                    safe->intents_.assignmentChanged (static_cast<std::size_t> (p), chosen);
            });
        };
        addAndMakeVisible (role_);
        sound_.onChange = [this] {
            const int id = sound_.getSelectedId();
            const auto p = owner_.selected_;
            if (p < 0 || id == 0)
                return;
            const auto part = static_cast<std::size_t> (p);
            if (id == addSoundsId)
            {
                sound_.setSelectedId (owner_.rows_[part].soundId == 0 ? mainSoundId : owner_.rows_[part].soundId, juce::dontSendNotification);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TracksComponent> (&owner_)] {
                    if (safe != nullptr && safe->intents_.openSoundSettings)
                        safe->intents_.openSoundSettings();
                });
                return;
            }
            const int chosen = id == mainSoundId ? 0 : id;
            if (chosen != owner_.rows_[part].soundId && owner_.intents_.soundChanged)
                owner_.intents_.soundChanged (part, chosen);
        };
        addAndMakeVisible (sound_);

        swatch_.onClick = [this] {
            if (owner_.selected_ >= 0)
                owner_.showColourMenu (static_cast<std::size_t> (owner_.selected_), swatch_);
        };
        addAndMakeVisible (swatch_);
        hex_.setTooltip (TRANS ("Colour as #RRGGBB; Enter applies it"));
        hex_.onReturnKey = [this] { applyHex(); };
        addAndMakeVisible (hex_);
        auto_.setButtonText (TRANS ("Automatic"));
        auto_.setTooltip (TRANS ("Back to the colour chosen from the instrument"));
        auto_.onClick = [this] {
            if (owner_.selected_ >= 0 && owner_.intents_.colourChanged)
                owner_.intents_.colourChanged (static_cast<std::size_t> (owner_.selected_), std::nullopt);
        };
        addAndMakeVisible (auto_);

        only_.setButtonText (TRANS ("Show only this track"));
        only_.setTooltip (TRANS ("Changes only what the piano roll shows"));
        only_.onClick = [this] {
            if (owner_.selected_ >= 0 && owner_.intents_.showOnly)
                owner_.intents_.showOnly (static_cast<std::size_t> (owner_.selected_));
        };
        addAndMakeVisible (only_);
        editor_.setButtonText (TRANS ("Open the track's instrument window"));
        editor_.onClick = [this] {
            if (owner_.selected_ >= 0 && owner_.intents_.openEditor)
                owner_.intents_.openEditor (static_cast<std::size_t> (owner_.selected_));
        };
        addChildComponent (editor_);
        mainEditor_.setButtonText (TRANS ("Open the main instrument window"));
        mainEditor_.onClick = [this] { if (owner_.intents_.openMainEditor) owner_.intents_.openMainEditor(); };
        addAndMakeVisible (mainEditor_);
        settings_.setButtonText (TRANS ("Sound settings..."));
        settings_.onClick = [this] { if (owner_.intents_.openSoundSettings) owner_.intents_.openSoundSettings(); };
        addAndMakeVisible (settings_);

        // Preset inside the source: what this track plays instead of the file's instrument.
        setLabel (presetCaption_, 12.0f, true, theme::mutedText);
        presetCaption_.setText (TRANS ("Instrument in the source"), juce::dontSendNotification);
        setLabel (presetNow_, 13.0f, false, theme::text);
        setLabel (presetNote_, 12.0f, false, theme::mutedText);
        search_.setTextToShowWhenEmpty (TRANS ("Search by name or number"), theme::mutedText);
        search_.onTextChange = [this] { filter(); };
        list_.setModel (&model_);
        list_.setRowHeight (24);
        list_.setColour (juce::ListBox::backgroundColourId, theme::control);
        list_.setColour (juce::ListBox::outlineColourId, theme::outline);
        list_.setOutlineThickness (1);
        midi_.setButtonText (TRANS ("Use instrument from MIDI"));
        midi_.setTooltip (TRANS ("Play what the MIDI file selects again, program changes during the song included."));
        midi_.onClick = [this] {
            if (owner_.selected_ >= 0 && owner_.intents_.useMidiInstrument)
                owner_.intents_.useMidiInstrument (static_cast<std::size_t> (owner_.selected_));
        };
        for (auto* c : std::initializer_list<juce::Component*> { &presetCaption_, &presetNow_, &presetNote_, &search_, &list_, &midi_ })
            addChildComponent (c);
    }

    int idealHeight() const { return 620 + (list_.isVisible() ? 330 : presetCaption_.isVisible() ? 110 : 0) + (editor_.isVisible() ? 34 : 0); }

    void choose (int row)
    {
        if (row < 0 || static_cast<std::size_t> (row) >= shown_.size() || owner_.selected_ < 0 || ! owner_.intents_.presetChosen)
            return;
        const auto& preset = owner_.presets_[shown_[static_cast<std::size_t> (row)]];
        const auto part = static_cast<std::size_t> (owner_.selected_);
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TracksComponent> (&owner_), part, bank = preset.bank,
                                          program = preset.program] {
            if (safe != nullptr && safe->intents_.presetChosen)
                safe->intents_.presetChosen (part, bank, program);
        });
    }

    void refresh()
    {
        const auto p = owner_.selected_;
        const bool any = p >= 0 && static_cast<std::size_t> (p) < owner_.rows_.size();
        for (auto* c : std::initializer_list<juce::Component*> { &role_, &sound_, &swatch_, &hex_, &auto_, &only_, &roleCaption_,
                                                                 &soundCaption_, &colourCaption_, &routing_, &status_ })
            c->setVisible (any);
        editor_.setVisible (any && owner_.rows_[static_cast<std::size_t> (p)].hasEditor);
        if (! any)
        {
            title_.setText (owner_.rows_.empty() ? TRANS ("No song open") : TRANS ("Click a channel to see its details"),
                            juce::dontSendNotification);
            info_.setText ({}, juce::dontSendNotification);
            resized();
            return;
        }
        const auto part = static_cast<std::size_t> (p);
        const auto& r = owner_.rows_[part];
        title_.setText (r.displayName(), juce::dontSendNotification);
        title_.setTooltip (toJuce (r.label));
        info_.setText (toJuce (r.instrument) + "\n" + TRANS ("Track") + " " + juce::String (r.track) + "  ·  " + TRANS ("MIDI channel")
                           + " " + juce::String (r.channel) + "\n" + toJuce (r.detail),
                       juce::dontSendNotification);
        role_.setSelectedId (r.assignment + 1, juce::dontSendNotification);
        owner_.fillSoundBox (sound_);
        sound_.setSelectedId (r.soundId == 0 ? mainSoundId : r.soundId, juce::dontSendNotification);
        sound_.setEnabled (r.assignment != 4);
        routing_.setText (owner_.routingText (part), juce::dontSendNotification);

        juce::String status = r.checked ? TRANS ("Checked in practice: you play these notes, the song waits for them.")
                                        : TRANS ("Not checked in practice.");
        const auto quiet = quietText (r);
        status << "\n" << (quiet.isEmpty() ? TRANS ("Heard now.") : quiet + ".");
        if (! r.visible)
            status << "\n" << TRANS ("Hidden on the piano roll.");
        status_.setText (status, juce::dontSendNotification);
        swatch_.colour = juce::Colour (r.colour);
        swatch_.repaint();
        if (! hex_.hasKeyboardFocus (true))
            hex_.setText (toJuce (hexColour (r.colour)), false);
        auto_.setEnabled (! r.autoColour);
        refreshPresets (r);
        resized();
    }

    void refreshPresets (const TrackRowView& r)
    {
        const bool mine = owner_.presetsPart_ == owner_.selected_;
        const auto& kind = owner_.presetsSource_;
        const bool listed = mine && ! owner_.presets_.empty() && (kind == "soundFont" || kind == "plugin");
        presetCaption_.setVisible (true);
        presetNow_.setVisible (true);
        presetNote_.setVisible (true);
        search_.setVisible (listed);
        list_.setVisible (listed);
        midi_.setVisible (! r.presetName.empty());
        juce::String now = r.presetName.empty() ? TRANS ("From the MIDI file") + ": " + toJuce (r.instrument)
                                                : TRANS ("Chosen") + ": " + toJuce (r.presetName);
        presetNow_.setText (now, juce::dontSendNotification);
        juce::String note;
        if (! r.soundProblem.empty())
            note = toJuce (r.soundProblem);
        else if (r.presetDrums)
            note = TRANS ("The track's notes play as separate drums; the rhythm is not converted automatically.");
        else if (kind == "main" || kind.empty())
            note = TRANS ("To pick an instrument for this track only, choose a SoundFont or VST3 as its sound above.");
        else if (kind == "builtIn")
            note = TRANS ("The built-in sound has one instrument. Choose a SoundFont or VST3 above to pick others.");
        else if (kind == "plugin" && ! listed)
            note = TRANS ("This plug-in chooses its sounds in its own window; PianoAssist keeps its settings with the song.");
        else
            note = TRANS ("Only this track changes; its notes and the MIDI file stay as they are.");
        presetNote_.setText (note, juce::dontSendNotification);
        presetNote_.setColour (juce::Label::textColourId, r.soundProblem.empty() ? theme::mutedText : theme::warning);
        filter();
    }

    void filter()
    {
        shown_.clear();
        const auto query = search_.getText().trim().toLowerCase();
        int selectedRow = -1;
        const int part = owner_.selected_;
        for (std::size_t i = 0; i < owner_.presets_.size(); ++i)
        {
            const auto& p = owner_.presets_[i];
            const auto text = presetText (p).toLowerCase();
            if (query.isNotEmpty() && ! text.contains (query))
                continue;
            if (part >= 0 && static_cast<std::size_t> (part) < owner_.rows_.size()
                && owner_.rows_[static_cast<std::size_t> (part)].presetName == p.name && ! p.name.empty())
                selectedRow = static_cast<int> (shown_.size());
            shown_.push_back (i);
        }
        list_.updateContent();
        list_.selectRow (selectedRow, false, true); // scrolled into view
        list_.repaint();
    }

    static juce::String presetText (const PresetView& p)
    {
        const auto number = p.bank >= 0 ? juce::String (p.bank).paddedLeft ('0', 3) + ":" + juce::String (p.program + 1).paddedLeft ('0', 3)
                                        : juce::String (p.program + 1).paddedLeft ('0', 3);
        return number + "  " + toJuce (p.name) + (p.drums ? "  [" + TRANS ("drums") + "]" : juce::String());
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (theme::panel);
        g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f);
        g.setColour (theme::panelEdge);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (2.0f), 8.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (14, 12);
        title_.setBounds (r.removeFromTop (44));
        info_.setBounds (r.removeFromTop (54));
        r.removeFromTop (6);
        roleCaption_.setBounds (r.removeFromTop (18));
        role_.setBounds (r.removeFromTop (30));
        r.removeFromTop (8);
        soundCaption_.setBounds (r.removeFromTop (18));
        sound_.setBounds (r.removeFromTop (30));
        r.removeFromTop (6);
        routing_.setBounds (r.removeFromTop (74));
        status_.setBounds (r.removeFromTop (54));
        r.removeFromTop (4);
        colourCaption_.setBounds (r.removeFromTop (18));
        auto colourRow = r.removeFromTop (30);
        swatch_.setBounds (colourRow.removeFromLeft (30));
        colourRow.removeFromLeft (6);
        hex_.setBounds (colourRow.removeFromLeft (92));
        colourRow.removeFromLeft (6);
        auto_.setBounds (colourRow);
        r.removeFromTop (10);
        only_.setBounds (r.removeFromTop (28));
        r.removeFromTop (8);
        if (editor_.isVisible())
        {
            editor_.setBounds (r.removeFromTop (28));
            r.removeFromTop (6);
        }
        mainEditor_.setBounds (r.removeFromTop (28));
        r.removeFromTop (6);
        settings_.setBounds (r.removeFromTop (28));
        if (presetCaption_.isVisible())
        {
            r.removeFromTop (12);
            presetCaption_.setBounds (r.removeFromTop (18));
            presetNow_.setBounds (r.removeFromTop (22));
            presetNote_.setBounds (r.removeFromTop (48));
            if (midi_.isVisible())
            {
                midi_.setBounds (r.removeFromTop (28));
                r.removeFromTop (6);
            }
            if (list_.isVisible())
            {
                search_.setBounds (r.removeFromTop (28));
                r.removeFromTop (4);
                list_.setBounds (r.removeFromTop (std::max (120, std::min (240, r.getHeight()))));
                if (list_.getSelectedRow() >= 0)
                    list_.scrollToEnsureRowIsOnscreen (list_.getSelectedRow());
            }
        }
    }

private:
    void applyHex()
    {
        const auto value = parseHexColour (hex_.getText().toStdString());
        hex_.setColour (juce::TextEditor::outlineColourId, value ? theme::outline : theme::danger);
        if (value && owner_.selected_ >= 0 && owner_.intents_.colourChanged)
            owner_.intents_.colourChanged (static_cast<std::size_t> (owner_.selected_), *value);
    }

    class Model final : public juce::ListBoxModel
    {
    public:
        explicit Model (Inspector& owner) : owner_ (owner) {}
        int getNumRows() override { return static_cast<int> (owner_.shown_.size()); }
        void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override
        {
            if (row < 0 || static_cast<std::size_t> (row) >= owner_.shown_.size())
                return;
            const auto& p = owner_.owner_.presets_[owner_.shown_[static_cast<std::size_t> (row)]];
            if (selected)
            {
                g.setColour (theme::accent.withAlpha (0.25f));
                g.fillRect (0, 0, width, height);
            }
            g.setColour (theme::text);
            g.setFont (juce::FontOptions (13.0f, selected ? juce::Font::bold : juce::Font::plain));
            g.drawText (presetText (p), 8, 0, width - 12, height, juce::Justification::centredLeft, true);
        }
        void listBoxItemClicked (int row, const juce::MouseEvent&) override { owner_.choose (row); }
        void returnKeyPressed (int row) override { owner_.choose (row); }

    private:
        Inspector& owner_;
    };

    TracksComponent& owner_;
    Model model_ { *this };
    std::vector<std::size_t> shown_;
    juce::Label presetCaption_, presetNow_, presetNote_;
    juce::TextEditor search_;
    juce::ListBox list_;
    juce::TextButton midi_;
    juce::Label title_, info_, roleCaption_, soundCaption_, routing_, status_, colourCaption_;
    juce::ComboBox role_, sound_;
    SwatchButton swatch_;
    juce::TextEditor hex_;
    juce::TextButton auto_, only_, editor_, mainEditor_, settings_;
};

// --- the window content --------------------------------------------------------------------

juce::String TracksComponent::roleName (int assignment)
{
    switch (assignment)
    {
        case 0: return TRANS ("Right hand");
        case 1: return TRANS ("Left hand");
        case 2: return TRANS ("Both hands (split at C4)");
        case 4: return TRANS ("Excluded from practice");
        default: return TRANS ("Accompaniment");
    }
}

juce::String TracksComponent::quietText (const TrackRowView& r)
{
    switch (r.quiet)
    {
        case TrackRowView::mutedQuiet:    return r.solo ? TRANS ("Silent: Mute wins over Solo") : TRANS ("Silent: Mute");
        case TrackRowView::otherSolo:     return TRANS ("Silent: other tracks are Solo");
        case TrackRowView::backingOff:    return TRANS ("Silent: \"Other tracks\" is off");
        case TrackRowView::otherHandOff:  return TRANS ("Silent: \"Other hand\" is off");
        case TrackRowView::playedByYou:   return TRANS ("You play it");
        case TrackRowView::excludedQuiet: return TRANS ("Neither played nor checked");
        default: return {};
    }
}

juce::String TracksComponent::summary (const std::vector<TrackRowView>& rows)
{
    if (rows.empty())
        return {};
    juce::StringArray right, left, both;
    int accompaniment = 0, excluded = 0, muted = 0, solo = 0;
    for (const auto& r : rows)
    {
        switch (r.assignment)
        {
            case 0: right.add (r.displayName()); break;
            case 1: left.add (r.displayName()); break;
            case 2: both.add (r.displayName()); break;
            case 4: ++excluded; break;
            default: ++accompaniment; break;
        }
        muted += r.muted ? 1 : 0;
        solo += r.solo ? 1 : 0;
    }
    juce::StringArray lines;
    if (! right.isEmpty()) lines.add (TRANS ("Right hand") + ": " + right.joinIntoString (", "));
    if (! left.isEmpty()) lines.add (TRANS ("Left hand") + ": " + left.joinIntoString (", "));
    if (! both.isEmpty()) lines.add (TRANS ("Both hands") + ": " + both.joinIntoString (", "));
    juce::String rest = TRANS ("Accompaniment") + ": " + juce::String (accompaniment);
    if (excluded > 0) rest << "  ·  " << TRANS ("excluded") << ": " << excluded;
    if (muted > 0) rest << "  ·  " << TRANS ("muted") << ": " << muted;
    if (solo > 0) rest << "  ·  Solo: " << solo;
    lines.add (rest);
    return lines.joinIntoString ("\n");
}

TracksComponent::TracksComponent (Intents intents) : intents_ (std::move (intents))
{
    setLabel (hint_, 14.0f, false, theme::mutedText);
    hint_.setJustificationType (juce::Justification::centredLeft);
    hint_.setText (TRANS ("Role decides who plays a track and whether its notes are checked. Mute and Solo change only what you hear; "
                          "the eye only what the piano roll shows."),
                   juce::dontSendNotification);
    addAndMakeVisible (hint_);
    clearSolos_.setButtonText (TRANS ("Clear all Solo"));
    clearSolos_.onClick = [this] { if (intents_.clearSolos) intents_.clearSolos(); };
    addAndMakeVisible (clearSolos_);
    showAll_.setButtonText (TRANS ("Show all"));
    showAll_.setTooltip (TRANS ("Show every track on the piano roll"));
    showAll_.onClick = [this] { if (intents_.showAll) intents_.showAll(); };
    addAndMakeVisible (showAll_);
    inspectorToggle_.setClickingTogglesState (false);
    inspectorToggle_.onClick = [this] {
        setInspectorShown (! inspectorShown_);
        if (intents_.inspectorShownChanged)
            intents_.inspectorShownChanged (inspectorShown_);
    };
    addAndMakeVisible (inspectorToggle_);

    master_ = std::make_unique<MasterStrip> (*this);
    addAndMakeVisible (*master_);
    stripsView_.setViewedComponent (&stripsContent_, false);
    stripsView_.setScrollBarsShown (false, true);
    addAndMakeVisible (stripsView_);
    inspector_ = std::make_unique<Inspector> (*this);
    inspectorView_.setViewedComponent (inspector_.get(), false);
    inspectorView_.setScrollBarsShown (true, false);
    addAndMakeVisible (inspectorView_);
    setLabel (empty_, 15.0f, false, theme::mutedText);
    empty_.setJustificationType (juce::Justification::centred);
    empty_.setText (TRANS ("Open a MIDI file to see its tracks here."), juce::dontSendNotification);
    addChildComponent (empty_);
    setInspectorShown (true);
    setSize (1100, 640);
    startTimerHz (30);
}

TracksComponent::~TracksComponent() = default;

void TracksComponent::setRows (std::vector<TrackRowView> rows)
{
    const bool rebuild = rows.size() != rows_.size()
                         || ! std::equal (rows.begin(), rows.end(), rows_.begin(), [] (const auto& a, const auto& b) { return a.order == b.order; });
    rows_ = std::move (rows);
    if (selected_ >= static_cast<int> (rows_.size()))
        selected_ = -1;
    if (rebuild)
        rebuildStrips();
    for (auto* s : strips_)
        s->refresh();
    bool anySolo = false;
    for (const auto& r : rows_)
        anySolo = anySolo || r.solo;
    clearSolos_.setEnabled (anySolo);
    empty_.setVisible (rows_.empty());
    inspector_->refresh();
}

void TracksComponent::rebuildStrips()
{
    strips_.clear();
    std::vector<std::size_t> order (rows_.size());
    for (std::size_t i = 0; i < order.size(); ++i)
        order[i] = i;
    std::stable_sort (order.begin(), order.end(), [this] (std::size_t a, std::size_t b) { return rows_[a].order < rows_[b].order; });
    for (const auto part : order)
    {
        auto* strip = strips_.add (new Strip (*this, part));
        stripsContent_.addAndMakeVisible (strip);
    }
    resized();
}

void TracksComponent::setSelected (int part)
{
    if (part >= static_cast<int> (rows_.size()))
        part = -1;
    if (selected_ == part)
        return;
    selected_ = part;
    for (auto* s : strips_)
    {
        s->repaint();
        if (static_cast<int> (s->part()) == part)
            stripsView_.setViewPosition (std::clamp (stripsView_.getViewPositionX(), s->getRight() - stripsView_.getWidth(), s->getX()), 0);
    }
    inspector_->refresh();
}

void TracksComponent::setSound (const SoundView& view)
{
    sound_ = view;
    inspector_->refresh();
}

void TracksComponent::setMaster (const MasterView& view)
{
    master_->set (view);
}

void TracksComponent::setActivity (const std::vector<bool>& active)
{
    activity_ = active;
}

void TracksComponent::timerCallback()
{
    for (auto* s : strips_)
        s->setActive (s->part() < activity_.size() && activity_[s->part()]);
}

void TracksComponent::setInspectorShown (bool shown)
{
    inspectorShown_ = shown;
    inspectorView_.setVisible (shown);
    inspectorToggle_.setButtonText (shown ? TRANS ("Hide details") : TRANS ("Show details"));
    resized();
}

void TracksComponent::setPresets (int part, std::vector<PresetView> presets, std::string sourceKind)
{
    presetsPart_ = part;
    presets_ = std::move (presets);
    presetsSource_ = std::move (sourceKind);
    inspector_->refresh();
}

void TracksComponent::fillSoundBox (juce::ComboBox& box) const
{
    box.clear (juce::dontSendNotification);
    box.addItem (TRANS ("Main sound"), mainSoundId);
    box.addItem (TRANS ("Built-in sound"), SoundView::builtIn);
    if (! sound_.soundFontName.empty())
        box.addItem ("SoundFont: " + toJuce (sound_.soundFontName), SoundView::soundFont);
    if (! sound_.plugins.empty())
        box.addSectionHeading (TRANS ("VST3 instruments"));
    for (std::size_t i = 0; i < sound_.plugins.size(); ++i)
        box.addItem (toJuce (sound_.plugins[i]), SoundView::firstPlugin + static_cast<int> (i));
    box.addSeparator();
    box.addItem (TRANS ("Add sounds..."), addSoundsId);
}

juce::String TracksComponent::soundName (int soundId) const
{
    if (soundId == 0)
        return TRANS ("Main sound");
    if (soundId == SoundView::builtIn)
        return TRANS ("Built-in sound");
    if (soundId == SoundView::soundFont)
        return "SoundFont: " + toJuce (sound_.soundFontName);
    const auto index = static_cast<std::size_t> (soundId - SoundView::firstPlugin);
    return soundId >= SoundView::firstPlugin && index < sound_.plugins.size() ? "VST3: " + toJuce (sound_.plugins[index])
                                                                               : TRANS ("Unknown sound");
}

juce::String TracksComponent::routingText (std::size_t part) const
{
    const auto& r = rows_[part];
    juce::StringArray sharing, sameChannel;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (i == part || rows_[i].assignment == 4)
            continue;
        if (r.soundId == 0 && rows_[i].soundId == 0)
            sharing.add (rows_[i].displayName());
        if (rows_[i].channel == r.channel)
            sameChannel.add (rows_[i].displayName());
    }
    juce::String text = soundName (r.soundId);
    if (r.soundId == 0)
    {
        const auto mainName = sound_.selected == SoundView::builtIn ? TRANS ("Built-in sound")
                            : sound_.selected == SoundView::soundFont ? "SoundFont: " + toJuce (sound_.soundFontName)
                            : (sound_.selected >= SoundView::firstPlugin
                               && static_cast<std::size_t> (sound_.selected - SoundView::firstPlugin) < sound_.plugins.size())
                                ? "VST3: " + toJuce (sound_.plugins[static_cast<std::size_t> (sound_.selected - SoundView::firstPlugin)])
                                : juce::String();
        text << " (" << mainName << "): " << TRANS ("one shared instrument, this track on MIDI channel") << " " << r.channel << ".";
        if (! sharing.isEmpty())
            text << " " << TRANS ("Also used by") << ": " << sharing.joinIntoString (", ") << ".";
    }
    else if (r.soundId >= SoundView::firstPlugin)
        text << ": " << TRANS ("a separate copy of this plug-in for this track.");
    else if (r.soundId == SoundView::soundFont)
        text << ": " << TRANS ("its own SoundFont player (shared samples), only for this track.");
    else
        text << ": " << TRANS ("the built-in sound, shared with other tracks that use it, on channel") << " " << r.channel << ".";
    if (! sameChannel.isEmpty())
    {
        text << "\n" << TRANS ("Shares MIDI channel") << " " << r.channel << " " << TRANS ("with") << ": " << sameChannel.joinIntoString (", ") << ". ";
        text << (r.soundId == 0 || r.soundId == SoundView::builtIn
                     ? TRANS ("On a shared instrument they share the channel's program: give a track its own SoundFont or VST3 to make it independent.")
                     : TRANS ("This track's sound is independent of them."));
    }
    return text;
}

void TracksComponent::showColourMenu (std::size_t part, juce::Component& anchor)
{
    if (part >= rows_.size())
        return;
    const auto& r = rows_[part];
    auto panel = std::make_unique<ColourPanel> (juce::Colour (r.colour), r.autoColour,
                                                [safe = juce::Component::SafePointer<TracksComponent> (this), part] (std::optional<std::uint32_t> c) {
                                                    if (safe != nullptr && safe->intents_.colourChanged)
                                                        safe->intents_.colourChanged (part, c);
                                                });
    juce::CallOutBox::launchAsynchronously (std::move (panel), anchor.getScreenBounds(), nullptr);
}

void TracksComponent::showStripMenu (std::size_t part)
{
    if (part >= rows_.size())
        return;
    if (intents_.selectPart)
        intents_.selectPart (static_cast<int> (part));
    juce::PopupMenu menu;
    menu.addItem (TRANS ("Colour..."), [this, part] {
        for (auto* s : strips_)
            if (s->part() == part)
                showColourMenu (part, *s);
    });
    menu.addItem (TRANS ("Automatic colour"), ! rows_[part].autoColour, false, [this, part] {
        if (intents_.colourChanged)
            intents_.colourChanged (part, std::nullopt);
    });
    menu.addSeparator();
    menu.addItem (TRANS ("Show only this track"), [this, part] { if (intents_.showOnly) intents_.showOnly (part); });
    menu.addItem (TRANS ("Show all"), [this] { if (intents_.showAll) intents_.showAll(); });
    menu.addSeparator();
    menu.addItem (TRANS ("Move left"), [this, part] { if (intents_.moveTrack) intents_.moveTrack (part, -1); });
    menu.addItem (TRANS ("Move right"), [this, part] { if (intents_.moveTrack) intents_.moveTrack (part, 1); });
    menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
}

void TracksComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
}

void TracksComponent::resized()
{
    auto area = getLocalBounds().reduced (10);
    auto top = area.removeFromTop (32);
    inspectorToggle_.setBounds (top.removeFromRight (150));
    top.removeFromRight (8);
    showAll_.setBounds (top.removeFromRight (130));
    top.removeFromRight (8);
    clearSolos_.setBounds (top.removeFromRight (150));
    top.removeFromRight (12);
    hint_.setBounds (top);
    area.removeFromTop (8);

    if (inspectorShown_)
    {
        inspectorView_.setBounds (area.removeFromRight (std::min (inspectorWidth, std::max (240, area.getWidth() / 3))));
        inspector_->setSize (inspectorView_.getWidth() - inspectorView_.getScrollBarThickness(),
                             std::max (inspectorView_.getHeight(), inspector_->idealHeight()));
        area.removeFromRight (8);
    }
    master_->setBounds (area.removeFromLeft (stripWidth - 8));
    area.removeFromLeft (8);
    stripsView_.setBounds (area);
    empty_.setBounds (area);
    const int scrollbar = static_cast<int> (strips_.size()) * stripWidth > area.getWidth() ? stripsView_.getScrollBarThickness() : 0;
    const int height = std::max (360, area.getHeight() - scrollbar);
    stripsContent_.setSize (std::max (area.getWidth(), static_cast<int> (strips_.size()) * stripWidth), height);
    for (int i = 0; i < strips_.size(); ++i)
        strips_[i]->setBounds (i * stripWidth, 0, stripWidth, height);
    stripsView_.setScrollBarsShown (height > area.getHeight() - scrollbar, true);
}

} // namespace pianoassist::ui
