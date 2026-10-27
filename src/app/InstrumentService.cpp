#include "app/InstrumentService.h"

#include "app/AppPaths.h"
#include "audio/PluginInstrumentRenderer.h"
#include "audio/SoundFontRenderer.h"
#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

#include <algorithm>

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;
constexpr int drumChannel = 9;

juce::File toFile (const std::filesystem::path& path)
{
    return juce::File (juce::String::fromUTF8 (pathToUtf8 (path).c_str()));
}

juce::File knownListFile() { return toFile (dataDirectory() / "plugins.xml"); }

/** One file per plug-in, so switching between several keeps each one's sounds. */
std::filesystem::path stateFile (const std::string& pluginId)
{
    const auto hash = juce::String::toHexString (static_cast<juce::int64> (std::hash<std::string> {} (pluginId)));
    return dataDirectory() / pathFromUtf8 ("instrument-" + hash.toStdString() + ".state");
}

juce::String text (const std::string& utf8) { return juce::String::fromUTF8 (utf8.c_str()); }

/** The plug-in's window. Closing it only hides the editor; the plug-in keeps playing. */
class PluginWindow final : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessorEditor* editor, const juce::String& name, std::function<void()> onClose)
        : DocumentWindow (name, juce::Colours::black, DocumentWindow::closeButton), onClose_ (std::move (onClose))
    {
        setUsingNativeTitleBar (true);
        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        centreWithSize (getWidth(), getHeight());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        // Deferred: the owner deletes this window.
        juce::MessageManager::callAsync ([close = onClose_] { close(); });
    }

private:
    std::function<void()> onClose_;
};
} // namespace

InstrumentService::InstrumentService (diagnostics::RotatingLog& log, AudioService& audio,
                                      settings::SoundSettings& settings, std::function<void()> saveSettings)
    : log_ (log), audio_ (audio), settings_ (settings), saveSettings_ (std::move (saveSettings))
{
    formats_.addFormat (new juce::VST3PluginFormat()); // the manager takes ownership
    if (auto xml = juce::XmlDocument::parse (knownListFile()))
        known_.recreateFromXml (*xml);
    main_.choice.kind = "builtIn";

    scanner_.onProgress = [this] (const juce::String& file, int index, int total) {
        scanProgress_ = (TRANS ("Scanning") + " " + juce::String (index) + " / " + juce::String (total) + ": "
                         + juce::File (file).getFileName()).toStdString();
        if (onChange)
            onChange();
    };
    scanner_.onResult = [this] (PluginScanner::Result& result) {
        const auto name = juce::File (result.file).getFileName();
        if (result.problem != ScanProblem::none && result.problem != ScanProblem::noInstrument)
        {
            known_.addToBlacklist (result.file);
            problems_.push_back (describe (result.problem, name).toStdString());
            log_.write (LogLevel::warning, "sound", "Plug-in skipped",
                        { { "file", name.toStdString() }, { "reason", describe (result.problem, name).toStdString() } });
        }
        if (result.problem == ScanProblem::noInstrument)
            problems_.push_back (describe (result.problem, name).toStdString());
        for (auto* type : result.types)
        {
            known_.addType (*type);
            if (type->isInstrument)
            {
                ++added_;
                log_.write (LogLevel::info, "sound", "Instrument found",
                            { { "plugin", type->name.toStdString() }, { "vendor", type->manufacturerName.toStdString() } });
            }
        }
    };
    scanner_.onFinished = [this] {
        scanProgress_.clear();
        saveKnownList();
        std::vector<juce::PluginDescription> list;
        instruments (list);
        log_.write (LogLevel::info, "sound", "Scan finished", { { "instruments", std::to_string (list.size()) } });
        changed ((TRANS ("Scan finished. VST3 instruments available:") + " " + juce::String (static_cast<int> (list.size()))).toStdString());
    };
}

InstrumentService::~InstrumentService()
{
    scanner_.cancel();
    closeEditor();
}

// --- lists and lookups --------------------------------------------------------

void InstrumentService::instruments (std::vector<juce::PluginDescription>& out) const
{
    for (const auto& d : known_.getTypes())
        if (d.isInstrument)
            out.push_back (d);
}

const juce::PluginDescription* InstrumentService::findPlugin (const std::string& id) const
{
    static thread_local juce::PluginDescription found;
    for (const auto& d : known_.getTypes())
        if (d.isInstrument && d.createIdentifierString().toStdString() == id)
        {
            found = d;
            return &found;
        }
    return nullptr;
}

int InstrumentService::soundIdOf (const settings::SoundSettings& choice) const
{
    if (choice.kind == "main")
        return 0;
    if (choice.kind == "soundFont")
        return ui::SoundView::soundFont;
    if (choice.kind == "plugin")
    {
        std::vector<juce::PluginDescription> list;
        instruments (list);
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].createIdentifierString().toStdString() == choice.pluginId)
                return ui::SoundView::firstPlugin + static_cast<int> (i);
    }
    return ui::SoundView::builtIn;
}

settings::SoundSettings InstrumentService::choiceFor (int soundId) const
{
    settings::SoundSettings choice;
    if (soundId == 0)
        choice.kind = "main";
    else if (soundId == ui::SoundView::soundFont)
    {
        choice.kind = "soundFont";
        choice.soundFontPath = soundFontPath_;
    }
    else if (soundId >= ui::SoundView::firstPlugin)
    {
        std::vector<juce::PluginDescription> list;
        instruments (list);
        const auto index = static_cast<std::size_t> (soundId - ui::SoundView::firstPlugin);
        if (index < list.size())
        {
            choice.kind = "plugin";
            choice.pluginId = list[index].createIdentifierString().toStdString();
            choice.pluginName = list[index].name.toStdString();
        }
    }
    return choice;
}

ui::SoundView InstrumentService::view() const
{
    ui::SoundView v;
    std::vector<juce::PluginDescription> list;
    instruments (list);
    for (const auto& d : list)
        v.plugins.push_back ((d.name + (d.manufacturerName.isNotEmpty() ? " (" + d.manufacturerName + ")" : juce::String()))
                                 .toStdString());
    v.soundFontName = soundFontName_;
    v.selected = soundIdOf (main_.choice);
    v.canEdit = main_.ownsHandle;
    v.scanning = scanner_.isScanning();
    v.scanProgress = scanProgress_;
    v.status = status_;
    juce::VST3PluginFormat vst3;
    for (const auto& folder : juce::StringArray::fromTokens (vst3.getDefaultLocationsToSearch().toString(), ";", ""))
        v.folders.push_back (folder.toStdString());
    v.standardFolderCount = v.folders.size();
    for (const auto& folder : settings_.pluginFolders)
        v.folders.push_back (folder);
    v.problems = problems_;
    if (! main_.problem.empty())
        v.problems.push_back (main_.problem);
    for (const auto& slot : parts_)
        if (! slot.problem.empty())
            v.problems.push_back (slot.problem);
    return v;
}

void InstrumentService::changed (std::string status)
{
    status_ = std::move (status);
    if (onChange)
        onChange();
}

// --- instances ----------------------------------------------------------------

int InstrumentService::soundFontHandle (const std::filesystem::path& path, std::string& error)
{
    const auto key = pathToUtf8 (path);
    if (const auto it = soundFonts_.find (key); it != soundFonts_.end())
        return it->second;
    log_.write (LogLevel::info, "sound", "Loading SoundFont", { { "file", fileNameForLog (path) } });
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto font = audio::SoundFontRenderer::load (path);
    if (! font)
    {
        error = font.error();
        log_.write (LogLevel::error, "sound", "SoundFont failed to load", { { "error", error } });
        return -1;
    }
    auto renderer = std::move (font).value();
    soundFontName_ = renderer->name();
    soundFontPath_ = key;
    log_.write (LogLevel::info, "sound", "SoundFont loaded",
                { { "presets", std::to_string (renderer->presetCount()) },
                  { "loadMs", std::to_string (static_cast<long long> (juce::Time::getMillisecondCounterHiRes() - started)) } });
    const int handle = audio_.addInstrument (std::move (renderer));
    soundFonts_[key] = handle;
    return handle;
}

juce::MemoryBlock InstrumentService::readStateFile (const std::string& pluginId) const
{
    if (auto bytes = readBinaryFile (stateFile (pluginId), 64u * 1024u * 1024u); bytes && ! bytes.value().empty())
    {
        const auto& data = bytes.value();
        const auto newline = std::find (data.begin(), data.end(), static_cast<std::uint8_t> ('\n'));
        if (newline != data.end() && std::string (data.begin(), newline) == pluginId)
        {
            const auto offset = static_cast<std::size_t> (newline - data.begin()) + 1;
            return juce::MemoryBlock (data.data() + offset, data.size() - offset);
        }
    }
    return {};
}

bool InstrumentService::realise (Slot& slot, const juce::MemoryBlock* state, bool drums)
{
    slot.problem.clear();
    slot.handle = AudioService::builtInHandle;
    slot.ownsHandle = false;
    const auto& choice = slot.choice;
    if (choice.kind == "builtIn" || choice.kind == "main")
        return true;

    if (choice.kind == "soundFont")
    {
        std::string error;
        const int handle = choice.soundFontPath.empty() ? -1 : soundFontHandle (pathFromUtf8 (choice.soundFontPath), error);
        if (handle < 0)
        {
            const auto name = juce::File (text (choice.soundFontPath)).getFileName().toStdString();
            slot.problem = (TRANS ("SoundFont not available:") + " " + text (name.empty() ? std::string ("?") : name)
                            + (error.empty() ? juce::String() : " (" + text (error) + ")") + ". "
                            + TRANS ("Choose the file again in Settings > Sounds."))
                               .toStdString();
            return false;
        }
        slot.handle = handle;
        return true;
    }

    // VST3: a separate instance for this slot.
    const auto* description = findPlugin (choice.pluginId);
    if (description == nullptr)
    {
        slot.problem = (TRANS ("Instrument not found:") + " \"" + text (choice.pluginName) + "\". "
                        + TRANS ("It may have been uninstalled or moved. Scan again or choose another sound; the choice is kept until you change it."))
                           .toStdString();
        log_.write (LogLevel::warning, "sound", "Instrument not found", { { "plugin", choice.pluginName } });
        return false;
    }
    if (! juce::File (description->fileOrIdentifier).exists())
    {
        slot.problem = (TRANS ("The instrument file is missing:") + " " + description->fileOrIdentifier + ". "
                        + TRANS ("Reinstall it or scan again."))
                           .toStdString();
        return false;
    }
    const auto [rate, block] = audio_.renderFormat();
    juce::String error;
    log_.write (LogLevel::info, "sound", "Loading plug-in",
                { { "plugin", description->name.toStdString() }, { "vendor", description->manufacturerName.toStdString() },
                  { "version", description->version.toStdString() } });
    auto instance = formats_.createPluginInstance (*description, rate, block, error);
    if (instance == nullptr)
    {
        log_.write (LogLevel::error, "sound", "Plug-in failed to load", { { "error", error.toStdString() } });
        slot.problem = (TRANS ("Could not load") + " " + description->name + ": " + error).toStdString();
        return false;
    }
    if (state != nullptr && state->getSize() > 0)
    {
        instance->setStateInformation (state->getData(), static_cast<int> (state->getSize()));
        log_.write (LogLevel::info, "sound", "Instrument settings restored", { { "bytes", std::to_string (state->getSize()) } });
    }
    auto renderer = std::make_unique<audio::PluginInstrumentRenderer> (std::move (instance));
    renderer->setSendDrums (drums);
    slot.handle = audio_.addInstrument (std::move (renderer));
    slot.ownsHandle = true;
    return true;
}

void InstrumentService::release (Slot& slot)
{
    if (slot.ownsHandle)
    {
        if ((&slot == &main_ && editorPart_ == -1)
            || (&slot != &main_ && editorPart_ == static_cast<int> (&slot - parts_.data())))
            closeEditor();
        const int handle = slot.handle;
        slot.handle = AudioService::builtInHandle;
        slot.ownsHandle = false;
        applyRouting(); // nothing routes to it any more
        audio_.removeInstrument (handle);
    }
}

void InstrumentService::releaseUnusedSoundFonts()
{
    for (auto it = soundFonts_.begin(); it != soundFonts_.end();)
    {
        const int handle = it->second;
        const bool used = main_.handle == handle
                          || std::any_of (parts_.begin(), parts_.end(), [handle] (const Slot& s) { return s.handle == handle; });
        if (! used && it->first != soundFontPath_)
        {
            audio_.removeInstrument (handle);
            it = soundFonts_.erase (it);
        }
        else
            ++it;
    }
}

void InstrumentService::applyRouting()
{
    std::array<int, 16> channels;
    channels.fill (-1);
    for (std::size_t i = 0; i < parts_.size() && i < channels_.size(); ++i)
        if (parts_[i].choice.kind != "main" && channels_[i] >= 0 && channels_[i] < 16)
            channels[static_cast<std::size_t> (channels_[i])] = parts_[i].handle;
    audio_.setRouting (main_.handle, channels);
}

// --- main sound ---------------------------------------------------------------

void InstrumentService::restore()
{
    log_.write (LogLevel::info, "sound", "Restoring instrument", { { "kind", settings_.kind } });
    main_.choice = settings_;
    main_.choice.pluginFolders.clear();
    if (main_.choice.kind == "soundFont")
        soundFontPath_ = main_.choice.soundFontPath;
    const auto state = main_.choice.kind == "plugin" ? readStateFile (main_.choice.pluginId) : juce::MemoryBlock();
    realise (main_, &state, false);
    applyRouting();
    changed (main_.problem.empty() ? std::string() : main_.problem);
}

void InstrumentService::saveState()
{
    if (! main_.ownsHandle)
        return;
    auto* renderer = dynamic_cast<audio::PluginInstrumentRenderer*> (audio_.instrument (main_.handle));
    if (renderer == nullptr)
        return;
    juce::MemoryBlock state;
    renderer->plugin().getStateInformation (state);
    const auto id = main_.choice.pluginId + "\n";
    std::vector<std::uint8_t> bytes (id.begin(), id.end());
    const auto* data = static_cast<const std::uint8_t*> (state.getData());
    bytes.insert (bytes.end(), data, data + state.getSize());
    if (const auto written = writeFileAtomically (stateFile (main_.choice.pluginId), bytes); ! written)
        log_.write (LogLevel::error, "sound", "Could not save the instrument's settings", { { "error", written.error() } });
    else
        log_.write (LogLevel::info, "sound", "Instrument settings saved", { { "bytes", std::to_string (state.getSize()) } });
}

void InstrumentService::chooseMain (int soundId)
{
    auto choice = choiceFor (soundId);
    if (choice.kind == "main")
        return;
    saveState();
    release (main_);
    main_.choice = choice;
    const auto state = choice.kind == "plugin" ? readStateFile (choice.pluginId) : juce::MemoryBlock();
    const bool ok = realise (main_, &state, false);
    applyRouting();
    releaseUnusedSoundFonts();
    settings_.kind = choice.kind;
    settings_.pluginId = choice.pluginId;
    settings_.pluginName = choice.pluginName;
    if (choice.kind == "soundFont")
        settings_.soundFontPath = choice.soundFontPath;
    saveSettings_();
    log_.write (LogLevel::info, "sound", "Main sound chosen", { { "kind", choice.kind }, { "plugin", choice.pluginName } });
    changed (ok ? (choice.kind == "plugin" ? "VST3: " + choice.pluginName
                   : choice.kind == "soundFont" ? "SoundFont: " + soundFontName_
                                                : TRANS ("Built-in sound").toStdString())
                : main_.problem);
}

void InstrumentService::chooseSoundFont (const std::filesystem::path& path)
{
    std::string error;
    if (soundFontHandle (path, error) < 0)
        return changed ((TRANS ("Could not load the SoundFont:") + " " + text (error)).toStdString());
    soundFontPath_ = pathToUtf8 (path);
    chooseMain (ui::SoundView::soundFont);
}

void InstrumentService::showEditor (int part)
{
    Slot* slot = part < 0 ? &main_ : static_cast<std::size_t> (part) < parts_.size() ? &parts_[static_cast<std::size_t> (part)] : nullptr;
    if (slot == nullptr || ! slot->ownsHandle)
        return;
    auto* renderer = dynamic_cast<audio::PluginInstrumentRenderer*> (audio_.instrument (slot->handle));
    if (renderer == nullptr)
        return;
    if (editorWindow_ && editorPart_ == part)
    {
        editorWindow_->toFront (true);
        return;
    }
    closeEditor();
    auto* editor = renderer->plugin().createEditorIfNeeded();
    if (editor == nullptr)
        return changed (TRANS ("This instrument has no window.").toStdString());
    log_.write (LogLevel::info, "sound", "Plug-in window opened", { { "track", part < 0 ? "main" : std::to_string (part + 1) } });
    editorPart_ = part;
    editorWindow_ = std::make_unique<PluginWindow> (editor, renderer->plugin().getName(), [this] { closeEditor(); });
}

void InstrumentService::closeEditor()
{
    if (editorWindow_)
    {
        log_.write (LogLevel::info, "sound", "Plug-in window closed");
        editorWindow_.reset();
        if (editorPart_ >= 0 && onTrackSoundsChanged)
            onTrackSoundsChanged(); // the track's plug-in state may have changed
    }
    editorPart_ = -2;
}

// --- scanning -------------------------------------------------------------------

juce::StringArray InstrumentService::scanFolders() const
{
    juce::VST3PluginFormat vst3;
    auto folders = juce::StringArray::fromTokens (vst3.getDefaultLocationsToSearch().toString(), ";", "");
    for (const auto& folder : settings_.pluginFolders)
        folders.add (text (folder));
    folders.removeEmptyStrings();
    folders.removeDuplicates (true);
    return folders;
}

void InstrumentService::startScan()
{
    if (scanner_.isScanning())
        return;
    juce::FileSearchPath path;
    for (const auto& folder : scanFolders())
        path.add (juce::File (folder));
    juce::VST3PluginFormat vst3;
    const auto files = vst3.searchPathsForPlugins (path, true, true);
    juce::StringArray todo;
    for (const auto& file : files)
        if (! known_.getBlacklistedFiles().contains (file) && ! known_.isListingUpToDate (file, vst3))
            todo.add (file);
    problems_.clear();
    added_ = 0;
    log_.write (LogLevel::info, "sound", "Scanning for VST3 instruments",
                { { "folders", path.toString().toStdString() }, { "files", std::to_string (files.size()) },
                  { "new", std::to_string (todo.size()) } });
    if (todo.isEmpty())
        return changed ((TRANS ("No new VST3 files found. Instruments available:") + " "
                         + juce::String (static_cast<int> (std::count_if (known_.getTypes().begin(), known_.getTypes().end(),
                                                                         [] (const auto& d) { return d.isInstrument; }))))
                            .toStdString());
    scanner_.scan (todo);
    changed (TRANS ("Scanning for VST3 instruments...").toStdString());
}

void InstrumentService::rescanAll()
{
    if (scanner_.isScanning())
        return;
    log_.write (LogLevel::info, "sound", "Full rescan requested: skipped and known plug-ins are checked again");
    known_.clearBlacklistedFiles();
    known_.clear();
    saveKnownList();
    startScan();
}

void InstrumentService::addPluginFile (const juce::File& file)
{
    log_.write (LogLevel::info, "sound", "Adding plug-in file", { { "file", file.getFileName().toStdString() } });
    if (isUnsupportedPluginFile (file))
        return changed (describe (ScanProblem::notVst3, file.getFileName()).toStdString());
    if (! file.hasFileExtension ("vst3"))
        return changed ((file.getFileName() + ": " + TRANS ("not a VST3 instrument (.vst3).")).toStdString());
    known_.removeFromBlacklist (file.getFullPathName()); // the user asks explicitly: try again
    problems_.clear();
    scanner_.scan ({ file.getFullPathName() });
    changed ((TRANS ("Checking") + " " + file.getFileName() + "...").toStdString());
}

void InstrumentService::addScanFolder (const juce::File& folder)
{
    const auto path = folder.getFullPathName().toStdString();
    if (std::find (settings_.pluginFolders.begin(), settings_.pluginFolders.end(), path) == settings_.pluginFolders.end())
    {
        settings_.pluginFolders.push_back (path);
        saveSettings_();
        log_.write (LogLevel::info, "sound", "Scan folder added", { { "folder", folder.getFileName().toStdString() } });
    }
    startScan();
}

void InstrumentService::removeScanFolder (std::size_t index)
{
    juce::VST3PluginFormat vst3;
    const auto standard = static_cast<std::size_t> (
        juce::StringArray::fromTokens (vst3.getDefaultLocationsToSearch().toString(), ";", "").size());
    if (index < standard || index - standard >= settings_.pluginFolders.size())
        return;
    settings_.pluginFolders.erase (settings_.pluginFolders.begin() + static_cast<std::ptrdiff_t> (index - standard));
    saveSettings_();
    changed ({});
}

void InstrumentService::saveKnownList()
{
    if (auto xml = known_.createXml())
        xml->writeTo (knownListFile());
}

// --- tracks -------------------------------------------------------------------------

void InstrumentService::setSong (std::vector<int> channels, std::vector<TrackSound> sounds)
{
    closeEditor();
    for (auto& slot : parts_)
        release (slot);
    parts_.assign (channels.size(), {});
    channels_ = std::move (channels);
    for (std::size_t i = 0; i < parts_.size(); ++i)
    {
        auto& slot = parts_[i];
        slot.choice.kind = "main";
        if (i < sounds.size() && ! sounds[i].choice.kind.empty())
            slot.choice = sounds[i].choice;
        if (slot.choice.kind != "main")
            realise (slot, i < sounds.size() ? &sounds[i].state : nullptr, channels_[i] == drumChannel);
    }
    applyRouting();
    releaseUnusedSoundFonts();
    if (onChange)
        onChange();
}

void InstrumentService::choosePart (std::size_t part, int soundId)
{
    if (part >= parts_.size())
        return;
    auto& slot = parts_[part];
    release (slot);
    slot.choice = choiceFor (soundId);
    realise (slot, nullptr, channels_[part] == drumChannel);
    applyRouting();
    releaseUnusedSoundFonts();
    log_.write (LogLevel::info, "sound", "Track sound chosen",
                { { "track", std::to_string (part + 1) }, { "kind", slot.choice.kind }, { "plugin", slot.choice.pluginName } });
    if (onTrackSoundsChanged)
        onTrackSoundsChanged();
    changed (slot.problem);
}

std::vector<int> InstrumentService::partSoundIds() const
{
    std::vector<int> ids;
    for (const auto& slot : parts_)
        ids.push_back (soundIdOf (slot.choice));
    return ids;
}

std::vector<InstrumentService::TrackSound> InstrumentService::trackSounds() const
{
    std::vector<TrackSound> sounds;
    for (const auto& slot : parts_)
    {
        TrackSound sound;
        sound.choice = slot.choice;
        if (slot.ownsHandle)
            if (auto* renderer = dynamic_cast<audio::PluginInstrumentRenderer*> (audio_.instrument (slot.handle)))
                renderer->plugin().getStateInformation (sound.state);
        sounds.push_back (std::move (sound));
    }
    return sounds;
}

} // namespace pianoassist::app
