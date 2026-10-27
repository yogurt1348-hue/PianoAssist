#include "ui/LibraryComponent.h"

#include "ui/AppLookAndFeel.h"

namespace pianoassist::ui
{
namespace
{
juce::String toJuce (const std::string& text) { return juce::String::fromUTF8 (text.c_str()); }
} // namespace

LibraryComponent::LibraryComponent (Intents intents) : intents_ (std::move (intents))
{
    root_.setFont (juce::FontOptions (13.0f));
    addAndMakeVisible (root_);
    empty_.setText (TRANS ("No songs yet. Open a MIDI file: a copy is added here with its own settings."), juce::dontSendNotification);
    empty_.setJustificationType (juce::Justification::centred);
    addChildComponent (empty_);
    list_.setRowHeight (46);
    list_.setTitle (TRANS ("Songs in the library"));
    addAndMakeVisible (list_);

    open_.setButtonText (TRANS ("Open"));
    open_.onClick = [this] { if (intents_.open && selected() < rows_.size()) intents_.open (selected()); };
    folder_.setButtonText (TRANS ("Open song folder"));
    folder_.onClick = [this] { if (intents_.openFolder && selected() < rows_.size()) intents_.openFolder (selected()); };
    remove_.setButtonText (TRANS ("Remove from library..."));
    remove_.setTooltip (TRANS ("Deletes the library's copy and this song's settings. The original MIDI file is not touched."));
    remove_.onClick = [this] { if (intents_.remove && selected() < rows_.size()) intents_.remove (selected()); };
    refresh_.setButtonText (TRANS ("Refresh"));
    refresh_.onClick = [this] { if (intents_.refresh) intents_.refresh(); };
    rootButton_.setButtonText (TRANS ("Open library folder"));
    rootButton_.onClick = [this] { if (intents_.openLibraryFolder) intents_.openLibraryFolder(); };
    for (auto* b : { &open_, &folder_, &remove_, &refresh_, &rootButton_ })
        addAndMakeVisible (b);
    lookAndFeelChanged();
    updateButtons();
    setSize (860, 520);
}

void LibraryComponent::setRows (std::vector<LibraryRow> rows, const std::string& rootFolder)
{
    rows_ = std::move (rows);
    root_.setText (TRANS ("Library folder") + ": " + toJuce (rootFolder), juce::dontSendNotification);
    root_.setTooltip (toJuce (rootFolder));
    empty_.setVisible (rows_.empty());
    empty_.toFront (false);
    list_.updateContent();
    if (! rows_.empty() && list_.getSelectedRow() < 0)
        list_.selectRow (0);
    list_.repaint();
    updateButtons();
}

void LibraryComponent::updateButtons()
{
    const bool any = selected() < rows_.size();
    open_.setEnabled (any && ! rows_[any ? selected() : 0].midiMissing);
    folder_.setEnabled (any);
    remove_.setEnabled (any);
}

void LibraryComponent::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || static_cast<std::size_t> (row) >= rows_.size())
        return;
    const auto& r = rows_[static_cast<std::size_t> (row)];
    g.fillAll (selected ? theme::control : theme::panel);
    g.setColour (theme::panelEdge);
    g.fillRect (0, height - 1, width, 1);

    auto area = juce::Rectangle<int> (0, 0, width, height).reduced (12, 5);
    auto right = area.removeFromRight (200);
    g.setColour (theme::text);
    g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    g.drawText (toJuce (r.name), area.removeFromTop (20), juce::Justification::centredLeft, true);
    g.setFont (juce::FontOptions (12.0f));
    g.setColour (r.midiMissing ? theme::danger : theme::mutedText);
    const auto detail = r.midiMissing ? TRANS ("The MIDI copy in the song folder is missing. Open the file again to re-import it.")
                                      : toJuce (r.folderName) + (r.sourcePath.empty() ? juce::String() : toJuce ("  ·  ") + toJuce (r.sourcePath));
    g.drawText (detail, area, juce::Justification::centredLeft, true);
    g.setColour (theme::mutedText);
    g.drawText (r.lastOpened.empty() ? TRANS ("not opened yet") : TRANS ("opened") + " " + toJuce (r.lastOpened), right,
                juce::Justification::centredRight, true);
}

void LibraryComponent::listBoxItemDoubleClicked (int row, const juce::MouseEvent&) { openRow (row); }

void LibraryComponent::openRow (int row)
{
    if (row >= 0 && static_cast<std::size_t> (row) < rows_.size() && ! rows_[static_cast<std::size_t> (row)].midiMissing && intents_.open)
        intents_.open (static_cast<std::size_t> (row));
}

void LibraryComponent::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void LibraryComponent::resized()
{
    auto area = getLocalBounds().reduced (14);
    root_.setBounds (area.removeFromTop (22));
    area.removeFromTop (6);
    auto buttons = area.removeFromBottom (32);
    area.removeFromBottom (10);
    list_.setBounds (area);
    empty_.setBounds (area);
    open_.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    folder_.setBounds (buttons.removeFromLeft (200));
    buttons.removeFromLeft (8);
    remove_.setBounds (buttons.removeFromLeft (210));
    rootButton_.setBounds (buttons.removeFromRight (190));
    buttons.removeFromRight (8);
    refresh_.setBounds (buttons.removeFromRight (110));
}

void LibraryComponent::lookAndFeelChanged()
{
    root_.setColour (juce::Label::textColourId, theme::mutedText);
    empty_.setColour (juce::Label::textColourId, theme::mutedText);
    list_.setColour (juce::ListBox::backgroundColourId, theme::panel);
    list_.setColour (juce::ListBox::outlineColourId, theme::panelEdge);
    list_.setOutlineThickness (1);
    repaint();
}

} // namespace pianoassist::ui
