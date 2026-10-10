#pragma once

#include "ApplicationState.h"
#include "BridgeRouter.h"
#include "Audio/AudioEngine.h"
#include "Audio/MixerModel.h"
#include "Audio/VoiceMap.h"
#include "Midi/MidiRouter.h"
#include "Arranger/StyleEngine.h"
#include "Arranger/Style.h"
#include "Arranger/StyleLibraryIndex.h"
#include "Arranger/SongPlayer.h"
#include "Arranger/StyleRecorder.h"
#include "Arranger/MidiStyleConverter.h"
#include "Adaptive/AdaptiveSongEngine.h"

#include <atomic>
#include "Settings/SettingsStore.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>
#include <string>
#include <vector>

namespace cadenza::ui { class NativePanel; class StylePartEditorWindow; }
namespace cadenza::ai { struct StyleGenResult; }

enum class AiStyleAction
{
    None,
    AddFillsIntroEnding,
    Polish
};

class MainComponent final : public juce::Component,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    MainComponent();
    ~MainComponent() override;

    void resized() override;

    // juce::Timer — drives song-mode auto-stepping on the message thread.
    void timerCallback() override;

    // juce::ChangeListener — persists the audio-device choice when it changes.
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void showAudioSettings();
    void showMidiSettings();        // MIDI device list + button-mapping (learn) window
    std::string audioStateFilePath() const;

private:
    static juce::File findResourcesRoot();

    void installBridgeHooks();
    void applyRuntimeStateToEngines();
    void tryLoadFactoryStyle();
    bool tryLoadLastStyle();
    void tryLoadFactorySoundFont();
    void openStyleFileChooser();
    void openMidiStyleImportChooser();
    void openSoundFontFileChooser();
    void openSongFileChooser();
    void openChordAnalysisChooser();
    void openPluginFileChooser();

    // First-launch Quick Start assistant.
    void showFirstLaunchAssistant();
    void closeFirstLaunchAssistant();
    void playTestChord();             // a short C-major triad so the user confirms sound
    void choosePartInstrument(int channel);   // load a VST3 instrument onto a mixer channel
    void chooseMasterInstrument();            // load/open/clear the master multitimbral instrument
    void chooseRightLayerInstrument(int layer); // load a VST3 instrument onto a Right 1/2/3 layer
    bool loadAndApplyPluginFile(const juce::File& file);
    void clearMasterEffect();
    void pushPluginStateToWeb();
    void applyRightHand();       // (re)assert Right 1/2/3 enable + program + volume + octave
    void applyLeftVoice();       // (re)assert the left split voice on its channel
    void sendPitchBendToManual(int value14);  // pitch wheel -> Right 1/2/3 + Left channels
    void sendModToManual(int value);          // mod wheel (CC1) -> the same channels

    // Native control panel (source of truth for live performance controls).
    void buildNativePanel();
    void updateNativePanelStyle();   // push current style name + sections + mixer to the panel
    void applyMixerState();          // send each channel's effective volume (CC7) to the synth
    void applyStyleMix(const std::string& styleId);  // apply saved per-style mixer overrides
    void persistStyleMix();          // save current mixer strips for the current style

    // Auto-SFZ voices (sforzando-hosting via the VoiceMap). loadVoiceMap reads
    // voicemap.json; applyVoiceMapToStyle loads mapped voices on style channels
    // that have no per-style override (gated by useProVoices, GM fallback);
    // setDefaultVoice captures a part's current plugin into voicemap.json.
    juce::File voiceMapFile() const;
    void loadVoiceMap();
    void applyVoiceMapToStyle();
    void setDefaultVoice(int channel);
    void captureRegistration(int slot);   // snapshot the live setup into a registration
    void recallRegistration(int slot);    // restore a saved registration
    void applyOts(int slot);                                // recall one One Touch Setting slot
    void applyOtsLinkForSection(const std::string& name);   // OTS Link on Main A-D changes
    void refreshOtsAvailability();                          // dim/enable the panel's OTS buttons
    void exportPlaybackDiagnostics();
    void triggerSection(const std::string& id);   // select a section, with one-shot intro/fill/ending handling
    void togglePlayback();                         // start/stop (shared by the Play button + MIDI map)
    void startFadeOut();                           // fade the master out, then stop
    void executeControlCommand(const std::string& command);  // run a MIDI-mapped command
    bool loadAndApplyStyleFile(const juce::File& file);
    bool previewMidiStyleImport(const juce::File& file,
                                const cadenza::arranger::MidiStyleConvertOptions& options,
                                const cadenza::midi::Chord& sourceChord);
    void stopMidiStyleImportPreview();
    bool loadAndApplySongFile(const juce::File& file);
    bool selectStyleById(const std::string& styleId);
    bool analyzeAudioFile(const juce::File& file);
    void setSongMode(bool enabled);

    // Adaptive Song Mode (the band follows a pianist playing a reference part).
    void showAdaptiveSong();
    void setAdaptiveModeActive(bool active);   // performance keyboard + arranger off while active

    // Style Recorder (record your own style patterns into a .cstyle).
    void recorderNewSession(int bars);
    void recorderSetBars(int bars);
    void recorderSetPart(int partIndex);
    void recorderArm(bool on);            // off commits the take into the style
    void recorderClearPart();
    void recorderSave();
    void recorderExit();
    void makeLoadedStyleEditable();       // convert the loaded Yamaha style into an editable .cstyle session
    void recorderRefreshStyle();          // republish the in-progress style + UI
    void recorderPrepareTargetChannel();  // give the target part channel an audible voice
    juce::String recorderStatusText() const;
    void recorderOpenEditor();            // piano-roll editor for the target part
    void recorderReloadEditor();          // refresh the editor contents (if open)
    void refreshStyleEditorPage();        // push current style/section/part to the Editor page

    // AI: make a style from text (Anthropic API, bring-your-own-key).
    void showAiSettingsDialog();          // API key + model picker
    void showGenerateStyleDialog();       // "describe a style" prompt
    void generateStyleFromText(const juce::String& prompt);   // async API call
    void generateAiFillsIntroEnding();
    void polishStyleWithAi();
    void generateStyleEditAction(const juce::String& prompt,
                                 const juce::String& status,
                                 AiStyleAction action);
    void applyGeneratedStyle(const cadenza::ai::StyleGenResult& result);
    void applyGeneratedStyle(const cadenza::ai::StyleGenResult& result,
                             std::shared_ptr<const cadenza::arranger::Style> originalStyle,
                             AiStyleAction action);
    void applyPolishedStyle(const cadenza::ai::StyleGenResult& result,
                            int polishedSections,
                            int totalSections,
                            int warningCount);
    bool beginAiWorking(const juce::String& workingMessage, const juce::String& activeButtonText);
    void finishAiWorking(const juce::String& resultMessage);
    void recorderCloseEditor();
    void applySongStepForBar(int bar, bool applySection = true);
    void queueSongSectionForBar(int bar);
    bool loadAndApplySoundFontFile(const juce::File& file, bool persist);
    juce::Array<juce::File> factoryStyleFiles() const;
    const std::vector<cadenza::arranger::StyleMetadata>& factoryStyleMetadata() const;
    void pushFactoryStylesToWeb();
    void pushRuntimeStateToWeb();

    void pushToWeb(const juce::String& js);

    std::string settingsFilePath() const;
    void loadSettings();
    void saveSettings();

    // State / message routing.
    cadenza::ApplicationState m_state;
    cadenza::BridgeRouter m_router { m_state };

    // Audio + MIDI + arranger.
    cadenza::audio::AudioEngine m_audio;
    cadenza::midi::MidiRouter   m_midi;
    cadenza::arranger::StyleEngine m_styleEngine { m_audio };
    cadenza::adaptive::AdaptiveSongEngine m_adaptive { m_audio };
    bool m_adaptiveActive = false;
    bool m_adaptiveSuspendedCapture = false;   // Style Recorder capture paused by Adaptive mode
    mutable cadenza::arranger::StyleLibraryIndex m_factoryStyleIndex;

    // Song mode (chord-chart auto-stepping).
    cadenza::arranger::SongPlayer m_songPlayer;
    bool m_songModeActive = false;
    int  m_lastSongBar = -1;

    // Style Recorder. m_recordArmed is read on the MIDI thread (capture callback).
    cadenza::arranger::StyleRecorder m_recorder;
    std::atomic<bool> m_recordArmed { false };
    bool m_metronomeOn = true;   // recorder click track on/off (message thread)

    // Left-hand split voice (sounds below-split notes). Not persisted in v1.
    bool m_leftEnabled = false;
    int  m_leftProgram = 33;     // GM Electric Bass (finger)
    int  m_leftVolume  = 100;
    int  m_leftOctave  = 0;

    std::unique_ptr<juce::FileChooser> m_recSaveChooser;
    std::unique_ptr<cadenza::ui::StylePartEditorWindow> m_partEditor;
    std::unique_ptr<juce::AlertWindow> m_aiWindow;   // AI dialog (settings / prompt)

    // Live sections: one-shot returns / ending stops are sequenced inside the
    // StyleEngine (sample-tight); we only track the last Main as the return target.
    std::string m_currentMain = "mainA";
    std::shared_ptr<const cadenza::arranger::Style> m_midiImportPreviewPreviousStyle;
    std::string m_midiImportPreviewPreviousSection;
    std::string m_midiImportPreviewPreviousMain;
    bool m_midiImportPreviewActive = false;
    bool m_midiImportPreviewWasPlaying = false;

    // Last Main that OTS Link applied, so a fill returning to the same Main
    // doesn't re-trigger the OTS (which would undo manual voice tweaks).
    std::string m_lastLinkedMain;

    cadenza::audio::MixerModel m_mixer;
    cadenza::audio::VoiceMap m_voiceMap;   // GM program/family -> SFZ voice (sforzando)
    int m_midiRescanTicks = 0;   // timer ticks since last MIDI hot-plug rescan

    // Persistent settings.
    std::unique_ptr<cadenza::settings::SettingsStore> m_settings;
    std::unique_ptr<juce::FileChooser> m_styleChooser;
    std::unique_ptr<juce::FileChooser> m_midiStyleChooser;
    std::unique_ptr<juce::FileChooser> m_soundFontChooser;
    std::unique_ptr<juce::FileChooser> m_songChooser;
    std::unique_ptr<juce::FileChooser> m_analysisChooser;
    std::unique_ptr<juce::FileChooser> m_pluginChooser;
    std::unique_ptr<juce::FileChooser> m_partPluginChooser;
    std::unique_ptr<juce::FileChooser> m_masterPluginChooser;
    std::string m_learnCommand;        // command currently waiting for a MIDI-learn press

    std::unique_ptr<cadenza::ui::NativePanel> m_panel;
    bool m_aiInFlight = false;

    // First-launch "Quick Start" assistant (shown once; see setupAssistantSeen).
    std::unique_ptr<juce::DialogWindow> m_firstLaunchWindow;

    // Adaptive Song window (declared last: destroyed before the engines it uses).
    std::unique_ptr<juce::DialogWindow> m_adaptiveWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
