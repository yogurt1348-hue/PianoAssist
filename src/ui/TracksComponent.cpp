#include "ui/TracksComponent.h"

#include "ui/AppLookAndFeel.h"

#include <algorithm>

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

/** A control inside a table cell that remembers which row it belongs to. */
template <typename Base>
struct Cell final : public Base
{
    using Base::Base;
    int row = -1;
};
} // namespace

class TracksComponent::HeaderListener final : public juce::TableHeaderComponent::Listener
{
public:
    explicit HeaderListener (TracksComponent& owner) : owner_ (owner) {}
    void tableColumnsChanged (juce::TableHeaderComponent*) override { owner_.columnResized(); }
    void tableColumnsResized (juce::TableHeaderComponent*) override { owner_.columnResized(); }
    void tableSortOrderChanged (juce::TableHeaderComponent*) override {}

private:
    TracksComponent& owner_;
};

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

juce::String TracksComponent::summary (const std::vector<TrackRowView>& rows)
{
    if (rows.empty())
        return {};
    juce::StringArray right, left, both;
    int accompaniment = 0, excluded = 0, muted = 0;
    for (const auto& r : rows)
    {
        const auto name = r.trackName.empty() ? TRANS ("Track") + " " + juce::String (r.track) : toJuce (r.trackName);
        switch (r.assignment)
        {
            case 0: right.add (name); break;
            case 1: left.add (name); break;
            case 2: both.add (name); break;
            case 4: ++excluded; break;
            default: ++accompaniment; break;
        }
        muted += r.muted ? 1 : 0;
    }
    juce::StringArray lines;
    if (! right.isEmpty()) lines.add (TRANS ("Right hand") + ": " + right.joinIntoString (", "));
    if (! left.isEmpty()) lines.add (TRANS ("Left hand") + ": " + left.joinIntoString (", "));
    if (! both.isEmpty()) lines.add (TRANS ("Both hands") + ": " + both.joinIntoString (", "));
    juce::String rest = TRANS ("Accompaniment") + ": " + juce::String (accompaniment);
    if (excluded > 0) rest << "  ·  " << TRANS ("excluded") << ": " << excluded;
    if (muted > 0) rest << "  ·  " << TRANS ("muted") << ": " << muted;
    lines.add (rest);
    return lines.joinIntoString ("\n");
}

TracksComponent::TracksComponent (Intents intents) : intents_ (std::move (intents))
{
    hint_.setText (TRANS ("Role decides who plays a track and whether its notes are checked. Mute and Solo only change what you hear."),
                   juce::dontSendNotification);
    hint_.setFont (juce::FontOptions (14.0f));
    hint_.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (hint_);

    auto& header = table_.getHeader();
    const auto flags = juce::TableHeaderComponent::visible | juce::TableHeaderComponent::resizable;
    header.addColumn (TRANS ("Track"), trackColumn, 300, 160, 900, flags);
    header.addColumn (TRANS ("Role"), roleColumn, 200, 140, 400, flags);
    header.addColumn (TRANS ("Sound"), soundColumn, 220, 140, 500, flags);
    header.addColumn (TRANS ("Volume"), volumeColumn, 140, 90, 300, flags);
    header.addColumn (TRANS ("Mute"), muteColumn, 70, 60, 120, flags);
    header.addColumn (TRANS ("Solo"), soloColumn, 70, 60, 120, flags);
    header.setStretchToFitActive (false);
    headerListener_ = std::make_unique<HeaderListener> (*this);
    header.addListener (headerListener_.get());
    table_.setRowHeight (52);
    table_.setHeaderHeight (30);
    table_.setTitle (TRANS ("Tracks"));
    addAndMakeVisible (table_);

    detailTitle_.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    detailTitle_.setMinimumHorizontalScale (1.0f);
    for (auto* l : { &detailRole_, &detailSound_, &detailInfo_ })
    {
        l->setFont (juce::FontOptions (14.0f));
        l->setMinimumHorizontalScale (1.0f); // wrap instead of squeezing the letters
        l->setJustificationType (juce::Justification::topLeft);
    }
    editorButton_.setButtonText (TRANS ("Open the track's instrument window"));
    editorButton_.onClick = [this] {
        const auto row = table_.getSelectedRow();
        if (row >= 0 && intents_.openEditor)
            intents_.openEditor (static_cast<std::size_t> (row));
    };
    mainEditorButton_.setButtonText (TRANS ("Open the main instrument window"));
    mainEditorButton_.onClick = [this] { if (intents_.openMainEditor) intents_.openMainEditor(); };
    soundSettingsButton_.setButtonText (TRANS ("Sound settings..."));
    soundSettingsButton_.onClick = [this] { if (intents_.openSoundSettings) intents_.openSoundSettings(); };
    for (auto* c : std::initializer_list<juce::Component*> { &detailTitle_, &detailRole_, &detailSound_, &detailInfo_, &editorButton_,
                                                              &mainEditorButton_, &soundSettingsButton_ })
        addAndMakeVisible (c);
    lookAndFeelChanged();
    setSize (1000, 620);
}

TracksComponent::~TracksComponent()
{
    table_.getHeader().removeListener (headerListener_.get());
}

void TracksComponent::setRows (std::vector<TrackRowView> rows)
{
    const auto selected = table_.getSelectedRow();
    rows_ = std::move (rows);
    table_.updateContent();
    if (! rows_.empty())
        table_.selectRow (std::clamp (selected, 0, static_cast<int> (rows_.size()) - 1));
    table_.repaint();
    updateDetails();
}

void TracksComponent::setSound (const SoundView& view)
{
    sound_ = view;
    table_.updateContent();
    updateDetails();
}

std::string TracksComponent::columnsState() const { return table_.getHeader().toString().toStdString(); }

void TracksComponent::restoreColumns (const std::string& state)
{
    if (! state.empty())
        table_.getHeader().restoreFromString (toJuce (state));
}

void TracksComponent::columnResized()
{
    if (intents_.columnsChanged)
        intents_.columnsChanged (columnsState());
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

void TracksComponent::paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected)
{
    g.setColour (selected ? theme::control.brighter (0.15f) : (row % 2 == 0 ? theme::panel : theme::panel.brighter (0.03f)));
    g.fillRect (0, 0, width, height);
    g.setColour (theme::panelEdge);
    g.fillRect (0, height - 1, width, 1);
}

void TracksComponent::paintCell (juce::Graphics& g, int row, int column, int width, int height, bool)
{
    if (column != trackColumn || row < 0 || static_cast<std::size_t> (row) >= rows_.size())
        return;
    const auto& r = rows_[static_cast<std::size_t> (row)];
    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (10, 5);
    g.setColour (roleColourOf (r.assignment));
    g.fillRoundedRectangle (area.removeFromLeft (5).toFloat(), 2.0f);
    area.removeFromLeft (8);
    const auto name = r.trackName.empty() ? toJuce (r.instrument) : toJuce (r.trackName);
    g.setColour (r.assignment == 4 ? theme::mutedText : theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText ((r.guessed ? "? " : "") + name, area.removeFromTop (22), juce::Justification::centredLeft, true);
    g.setColour (theme::mutedText);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (TRANS ("Track") + " " + juce::String (r.track) + "  ·  " + TRANS ("channel") + " " + juce::String (r.channel) + "  ·  "
                    + toJuce (r.instrument),
                area, juce::Justification::centredLeft, true);
}

juce::String TracksComponent::getCellTooltip (int row, int column)
{
    if (row < 0 || static_cast<std::size_t> (row) >= rows_.size())
        return {};
    const auto& r = rows_[static_cast<std::size_t> (row)];
    switch (column)
    {
        case trackColumn: return toJuce (r.label) + "\n" + toJuce (r.instrument) + "\n" + toJuce (r.detail);
        case roleColumn:  return roleName (r.assignment);
        case soundColumn: return soundName (r.soundId);
        default: return {};
    }
}

juce::Component* TracksComponent::refreshComponentForCell (int row, int column, bool, juce::Component* existing)
{
    if (row < 0 || static_cast<std::size_t> (row) >= rows_.size() || column == trackColumn)
    {
        delete existing;
        return nullptr;
    }
    const auto& r = rows_[static_cast<std::size_t> (row)];
    const auto part = static_cast<std::size_t> (row);
    switch (column)
    {
        case roleColumn:
        {
            auto* box = dynamic_cast<Cell<juce::ComboBox>*> (existing);
            if (box == nullptr)
            {
                delete existing;
                box = new Cell<juce::ComboBox>();
                box->addItem (roleName (0), 1);
                box->addItem (roleName (1), 2);
                box->addItem (roleName (2), 3);
                box->addItem (roleName (3), 4);
                box->addItem (roleName (4), 5);
                box->setTitle (TRANS ("Role"));
                box->onChange = [this, box] {
                    const auto p = static_cast<std::size_t> (box->row);
                    const int chosen = box->getSelectedId() - 1;
                    if (p < rows_.size() && chosen != rows_[p].assignment && intents_.assignmentChanged)
                        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TracksComponent> (this), p, chosen] {
                            if (safe != nullptr && safe->intents_.assignmentChanged)
                                safe->intents_.assignmentChanged (p, chosen);
                        });
                };
            }
            box->row = row;
            box->setSelectedId (r.assignment + 1, juce::dontSendNotification);
            box->setTooltip (TRANS ("Right or left hand: you play it and it is checked. Accompaniment: the app plays it. "
                                    "Excluded: neither checked nor played."));
            return box;
        }
        case soundColumn:
        {
            auto* box = dynamic_cast<Cell<juce::ComboBox>*> (existing);
            if (box == nullptr)
            {
                delete existing;
                box = new Cell<juce::ComboBox>();
                box->setTitle (TRANS ("Sound"));
                box->onChange = [this, box] {
                    const auto p = static_cast<std::size_t> (box->row);
                    const int id = box->getSelectedId();
                    if (p >= rows_.size() || id == 0)
                        return;
                    if (id == addSoundsId)
                    {
                        box->setSelectedId (rows_[p].soundId == 0 ? mainSoundId : rows_[p].soundId, juce::dontSendNotification);
                        if (intents_.openSoundSettings)
                            juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<TracksComponent> (this)] {
                                if (safe != nullptr && safe->intents_.openSoundSettings)
                                    safe->intents_.openSoundSettings();
                            });
                        return;
                    }
                    const int chosen = id == mainSoundId ? 0 : id;
                    if (chosen == rows_[p].soundId)
                        return;
                    rows_[p].soundId = chosen;
                    updateDetails();
                    if (intents_.soundChanged)
                        intents_.soundChanged (p, chosen);
                };
            }
            box->row = row;
            fillSoundBox (*box);
            box->setSelectedId (r.soundId == 0 ? mainSoundId : r.soundId, juce::dontSendNotification);
            box->setTooltip (soundName (r.soundId));
            box->setEnabled (r.assignment != 4);
            return box;
        }
        case volumeColumn:
        {
            auto* slider = dynamic_cast<Cell<juce::Slider>*> (existing);
            if (slider == nullptr)
            {
                delete existing;
                slider = new Cell<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::NoTextBox);
                slider->setRange (0.0, 1.0);
                slider->setTitle (TRANS ("Track volume"));
                slider->onDragEnd = [this, slider] {
                    const auto p = static_cast<std::size_t> (slider->row);
                    if (p < rows_.size())
                    {
                        rows_[p].volume = slider->getValue();
                        if (intents_.volumeChanged)
                            intents_.volumeChanged (p, slider->getValue());
                    }
                };
            }
            slider->row = row;
            slider->setValue (r.volume, juce::dontSendNotification);
            slider->setEnabled (r.assignment != 4);
            return slider;
        }
        case muteColumn:
        case soloColumn:
        {
            auto* toggle = dynamic_cast<Cell<juce::ToggleButton>*> (existing);
            if (toggle == nullptr)
            {
                delete existing;
                toggle = new Cell<juce::ToggleButton>();
                const bool mute = column == muteColumn;
                toggle->setTitle (mute ? TRANS ("Mute") : TRANS ("Solo"));
                // The box outline must stay visible on the highlighted (selected) row too.
                toggle->setColour (juce::ToggleButton::tickDisabledColourId, theme::text.withAlpha (0.75f));
                toggle->setTooltip (mute ? TRANS ("Silence this track. What you have to play does not change.")
                                         : TRANS ("Hear only the solo tracks (and your own playing). What you have to play does not change."));
                toggle->onClick = [this, toggle, mute] {
                    const auto p = static_cast<std::size_t> (toggle->row);
                    if (p >= rows_.size())
                        return;
                    (mute ? rows_[p].muted : rows_[p].solo) = toggle->getToggleState();
                    if (mute && intents_.muteChanged)
                        intents_.muteChanged (p, toggle->getToggleState());
                    if (! mute && intents_.soloChanged)
                        intents_.soloChanged (p, toggle->getToggleState());
                };
            }
            toggle->row = row;
            toggle->setToggleState (column == muteColumn ? r.muted : r.solo, juce::dontSendNotification);
            toggle->setEnabled (r.assignment != 4);
            return toggle;
        }
        default:
            break;
    }
    delete existing;
    return nullptr;
}

void TracksComponent::selectedRowsChanged (int) { updateDetails(); }

void TracksComponent::updateDetails()
{
    const auto row = table_.getSelectedRow();
    const bool any = row >= 0 && static_cast<std::size_t> (row) < rows_.size();
    editorButton_.setVisible (any && rows_[static_cast<std::size_t> (row)].hasEditor);
    if (! any)
    {
        detailTitle_.setText (rows_.empty() ? TRANS ("No song open") : TRANS ("Choose a track to see its details"), juce::dontSendNotification);
        for (auto* l : { &detailRole_, &detailSound_, &detailInfo_ })
            l->setText ({}, juce::dontSendNotification);
        return;
    }
    const auto& r = rows_[static_cast<std::size_t> (row)];
    const auto name = r.trackName.empty() ? toJuce (r.instrument) : toJuce (r.trackName);
    detailTitle_.setText (name + "  ·  " + TRANS ("Track") + " " + juce::String (r.track) + "  ·  " + TRANS ("channel") + " "
                              + juce::String (r.channel) + "  ·  " + toJuce (r.instrument),
                          juce::dontSendNotification);
    detailTitle_.setTooltip (toJuce (r.label));

    juce::String role = roleName (r.assignment) + ". ";
    switch (r.assignment)
    {
        case 0: case 1: case 2:
            role << TRANS ("You play this track: in practice with waiting its notes are checked (when the mode includes this hand). "
                           "Otherwise the app can play it for you (\"Other hand\").");
            break;
        case 4: role << TRANS ("Neither checked nor played. Use it for guide or duplicate tracks."); break;
        default: role << TRANS ("The app plays it when \"Other tracks\" is on. Its notes are never checked."); break;
    }
    if (r.muted || r.solo)
        role << "\n" << (r.muted ? TRANS ("Muted: not heard.") : TRANS ("Solo: heard together with the other solo tracks."))
             << " " << TRANS ("What you have to play does not change.");
    detailRole_.setText (role, juce::dontSendNotification);

    // Which instrument actually plays it, and who else shares it.
    juce::StringArray sharing, sameChannel;
    for (std::size_t i = 0; i < rows_.size(); ++i)
    {
        if (i == static_cast<std::size_t> (row) || rows_[i].assignment == 4)
            continue;
        const auto name = rows_[i].trackName.empty() ? TRANS ("Track") + " " + juce::String (rows_[i].track) : toJuce (rows_[i].trackName);
        if (r.soundId == 0 && rows_[i].soundId == 0)
            sharing.add (name);
        if (rows_[i].channel == r.channel)
            sameChannel.add (name);
    }
    juce::String sound = TRANS ("Sound") + ": " + soundName (r.soundId);
    if (r.soundId == 0)
    {
        const auto mainName = sound_.selected == SoundView::builtIn ? TRANS ("Built-in sound")
                            : sound_.selected == SoundView::soundFont ? "SoundFont: " + toJuce (sound_.soundFontName)
                            : (sound_.selected >= SoundView::firstPlugin
                               && static_cast<std::size_t> (sound_.selected - SoundView::firstPlugin) < sound_.plugins.size())
                                ? "VST3: " + toJuce (sound_.plugins[static_cast<std::size_t> (sound_.selected - SoundView::firstPlugin)])
                                : juce::String();
        sound << " (" << mainName << ")";
        sound << "\n" << TRANS ("One shared instrument; it gets this track on MIDI channel") << " " << r.channel << ".";
        if (! sharing.isEmpty())
            sound << " " << TRANS ("Also used by") << ": " << sharing.joinIntoString (", ") << ".";
    }
    else if (r.soundId >= SoundView::firstPlugin)
        sound << "\n" << TRANS ("A separate copy of this plug-in for this track, with its own presets.");
    else
        sound << "\n" << TRANS ("Played by its own instrument on channel") << " " << r.channel << ".";
    if (! sameChannel.isEmpty())
        sound << "\n" << TRANS ("Shares MIDI channel") << " " << r.channel << " " << TRANS ("with") << ": " << sameChannel.joinIntoString (", ")
              << ". " << TRANS ("Tracks on one channel sound through one instrument.");
    detailSound_.setText (sound, juce::dontSendNotification);
    detailInfo_.setText (toJuce (r.detail), juce::dontSendNotification);
    resized();
}

void TracksComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::window);
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().reduced (12).removeFromBottom (190).toFloat(), 8.0f);
}

void TracksComponent::resized()
{
    auto area = getLocalBounds().reduced (12);
    hint_.setBounds (area.removeFromTop (26));
    area.removeFromTop (6);
    auto details = area.removeFromBottom (190).reduced (14, 10);
    area.removeFromBottom (10);
    table_.setBounds (area);

    detailTitle_.setBounds (details.removeFromTop (24));
    auto buttons = details.removeFromBottom (32);
    soundSettingsButton_.setBounds (buttons.removeFromRight (190));
    buttons.removeFromRight (8);
    mainEditorButton_.setBounds (buttons.removeFromRight (290));
    buttons.removeFromRight (8);
    if (editorButton_.isVisible())
        editorButton_.setBounds (buttons.removeFromRight (290));
    details.removeFromBottom (6);
    const int half = details.getWidth() / 2;
    auto left = details.removeFromLeft (half - 8);
    details.removeFromLeft (16);
    detailRole_.setBounds (left.removeFromTop (left.getHeight() - 20));
    detailInfo_.setBounds (left);
    detailSound_.setBounds (details);
}

void TracksComponent::lookAndFeelChanged()
{
    hint_.setColour (juce::Label::textColourId, theme::mutedText);
    detailTitle_.setColour (juce::Label::textColourId, theme::text);
    for (auto* l : { &detailRole_, &detailSound_ })
        l->setColour (juce::Label::textColourId, theme::text);
    detailInfo_.setColour (juce::Label::textColourId, theme::mutedText);
    table_.setColour (juce::ListBox::backgroundColourId, theme::panel);
    table_.setColour (juce::ListBox::outlineColourId, theme::panelEdge);
    table_.setOutlineThickness (1);
    table_.getHeader().setColour (juce::TableHeaderComponent::backgroundColourId, theme::header);
    table_.getHeader().setColour (juce::TableHeaderComponent::textColourId, theme::mutedText);
    table_.getHeader().setColour (juce::TableHeaderComponent::outlineColourId, theme::panelEdge);
    repaint();
}

} // namespace pianoassist::ui
