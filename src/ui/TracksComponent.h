#pragma once

#include "ui/SoundView.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** One track of the song as the tracks window shows it. */
struct TrackRowView
{
    std::string label;       ///< "Track 2 'Piano' · ch 1"
    std::string trackName;   ///< as in the file, may be empty
    std::string instrument;  ///< General MIDI name
    std::string detail;      ///< notes, range, why the role was guessed
    int track = 0;           ///< 1-based
    int channel = 1;         ///< 1-based
    int assignment = 3;      ///< 0 right, 1 left, 2 both (split at C4), 3 accompaniment, 4 excluded
    bool guessed = false;
    int soundId = 0;         ///< SoundView ids, 0 = main sound
    double volume = 1.0;
    bool muted = false;
    bool solo = false;
    bool hasEditor = false;  ///< the track has its own plug-in with a window
};

/** "Tracks and instruments": a resizable list with the columns Track, Role,
    Sound, Volume, Mute, Solo, and details of the selected track below.
    Role decides who plays a track and whether its notes are checked; Mute
    and Solo only decide what is heard. */
class TracksComponent final : public juce::Component, private juce::TableListBoxModel
{
public:
    struct Intents
    {
        std::function<void (std::size_t part, int assignment)> assignmentChanged;
        std::function<void (std::size_t part, int soundId)> soundChanged;
        std::function<void (std::size_t part, double volume)> volumeChanged;
        std::function<void (std::size_t part, bool muted)> muteChanged;
        std::function<void (std::size_t part, bool solo)> soloChanged;
        std::function<void (std::size_t part)> openEditor; ///< the track's own plug-in window
        std::function<void()> openMainEditor;
        std::function<void()> openSoundSettings;
        std::function<void (const std::string& columns)> columnsChanged;
    };

    explicit TracksComponent (Intents intents);
    ~TracksComponent() override;

    void setRows (std::vector<TrackRowView> rows);
    void setSound (const SoundView& view);
    std::string columnsState() const;
    void restoreColumns (const std::string& state);

    void paint (juce::Graphics& g) override;
    void resized() override;
    void lookAndFeelChanged() override;

    /** Short text for the side panel: who plays what. */
    static juce::String summary (const std::vector<TrackRowView>& rows);
    static juce::String roleName (int assignment);

private:
    enum Column { trackColumn = 1, roleColumn, soundColumn, volumeColumn, muteColumn, soloColumn };

    int getNumRows() override { return static_cast<int> (rows_.size()); }
    void paintRowBackground (juce::Graphics& g, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics& g, int row, int column, int width, int height, bool selected) override;
    juce::Component* refreshComponentForCell (int row, int column, bool selected, juce::Component* existing) override;
    juce::String getCellTooltip (int row, int column) override;
    void selectedRowsChanged (int row) override;
    void columnResized();
    void updateDetails();
    void fillSoundBox (juce::ComboBox& box) const;
    juce::String soundName (int soundId) const;

    class HeaderListener;
    Intents intents_;
    std::vector<TrackRowView> rows_;
    SoundView sound_;
    juce::Label hint_;
    juce::TableListBox table_ { "tracks", this };
    std::unique_ptr<HeaderListener> headerListener_;

    // Details of the selected track
    juce::Label detailTitle_, detailRole_, detailSound_, detailInfo_;
    juce::TextButton editorButton_, mainEditorButton_, soundSettingsButton_;
};

} // namespace pianoassist::ui
