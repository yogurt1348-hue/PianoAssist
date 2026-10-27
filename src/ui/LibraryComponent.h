#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** One song of the library as the list shows it. */
struct LibraryRow
{
    std::string name;
    std::string folderName;   ///< "Name [id]": tells same-named songs apart
    std::string lastOpened;   ///< already formatted, empty when never opened
    std::string sourcePath;   ///< where it was imported from
    bool midiMissing = false;
};

/** The song library window: recent and imported songs, most recent first.
    Opening restores the song's saved settings; removing deletes only the
    library's copy, never the original file. */
class LibraryComponent final : public juce::Component, private juce::ListBoxModel
{
public:
    struct Intents
    {
        std::function<void (std::size_t row)> open;
        std::function<void (std::size_t row)> openFolder;
        std::function<void (std::size_t row)> remove;
        std::function<void()> refresh;
        std::function<void()> openLibraryFolder;
    };

    explicit LibraryComponent (Intents intents);
    void setRows (std::vector<LibraryRow> rows, const std::string& rootFolder);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

private:
    int getNumRows() override { return static_cast<int> (rows_.size()); }
    void paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void selectedRowsChanged (int) override { updateButtons(); }
    void returnKeyPressed (int row) override { openRow (row); }
    void openRow (int row);
    void updateButtons();
    std::size_t selected() const { return static_cast<std::size_t> (list_.getSelectedRow()); }

    Intents intents_;
    std::vector<LibraryRow> rows_;
    juce::Label root_, empty_;
    juce::ListBox list_ { "songs", this };
    juce::TextButton open_, folder_, remove_, refresh_, rootButton_;
};

} // namespace pianoassist::ui
