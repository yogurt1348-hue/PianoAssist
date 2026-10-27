#include "app/InstrumentService.h"

#include "app/AppPaths.h"
#include "audio/PluginInstrumentRenderer.h"
#include "audio/SoundFontRenderer.h"
#include "core/BinaryFile.h"
#include "core/Utf8Path.h"

namespace pianoassist::app
{
namespace
{
using diagnostics::LogLevel;

juce::File toFile (const std::filesystem::path& path)
{
    return juce::File (juce::String::fromUTF8 (pathToUtf8 (path).c_str()));
}

juce::File knownListFile() { return toFile (userDataDirectory() / "plugins.xml"); }
juce::File deadMansPedal() { return toFile (userDataDirectory() / "plugin-scan-in-progress.txt"); }
std::filesystem::path stateFile() { return userDataDirectory() / "instrument-state.bin"; }

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

    // A scan that crashed last time left this file; its entry is now blacklisted.
    if (deadMansPedal().existsAsFile())
    {
        juce::StringArray crashed;
        deadMansPedal().readLines (crashed);
        crashed.removeEmptyStrings();
        for (const auto& file : crashed)
        {
            known_.addToBlacklist (file);
            log_.write (LogLevel::warning, "sound", "A plug-in crashed the last scan and is skipped",
                        { { "file", juce::File (file).getFileName().toStdString() } });
        }
        deadMansPedal().deleteFile();
        saveKnownList();
    }
}

InstrumentService::~InstrumentService()
{
    stopTimer();
    closeEditor();
}

void InstrumentService::instruments (std::vector<juce::PluginDescription>& out) const
{
    for (const auto& d : known_.getTypes())
        if (d.isInstrument)
            out.push_back (d);
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
    v.selected = selected_;
    v.canEdit = selected_ >= ui::SoundView::firstPlugin && audio_.instrument() != nullptr;
    v.scanning = scanner_ != nullptr;
    v.status = status_;
    return v;
}

void InstrumentService::changed (std::string status)
{
    status_ = std::move (status);
    if (onChange)
        onChange();
}

void InstrumentService::restore()
{
    log_.write (LogLevel::info, "sound", "Restoring instrument", { { "kind", settings_.kind } });
    if (settings_.kind == "soundFont" && ! settings_.soundFontPath.empty())
        chooseSoundFont (pathFromUtf8 (settings_.soundFontPath));
    else if (settings_.kind == "plugin" && ! settings_.pluginId.empty())
    {
        std::vector<juce::PluginDescription> list;
        instruments (list);
        for (std::size_t i = 0; i < list.size(); ++i)
            if (list[i].createIdentifierString().toStdString() == settings_.pluginId)
                return choosePlugin (i);
        log_.write (LogLevel::warning, "sound", "Remembered plug-in not found", { { "plugin", settings_.pluginName } });
        changed ("Instrument \"" + settings_.pluginName + "\" was not found; using the built-in synth.");
    }
}

void InstrumentService::saveState()
{
    auto* renderer = dynamic_cast<audio::PluginInstrumentRenderer*> (audio_.instrument());
    if (renderer == nullptr)
        return;
    juce::MemoryBlock state;
    renderer->plugin().getStateInformation (state);
    const auto id = settings_.pluginId + "\n";
    std::vector<std::uint8_t> bytes (id.begin(), id.end());
    const auto* data = static_cast<const std::uint8_t*> (state.getData());
    bytes.insert (bytes.end(), data, data + state.getSize());
    if (const auto written = writeFileAtomically (stateFile(), bytes); ! written)
        log_.write (LogLevel::error, "sound", "Could not save the instrument's settings", { { "error", written.error() } });
    else
        log_.write (LogLevel::info, "sound", "Instrument settings saved", { { "bytes", std::to_string (state.getSize()) } });
}

void InstrumentService::chooseBuiltIn()
{
    saveState();
    closeEditor();
    audio_.setInstrument (nullptr);
    selected_ = ui::SoundView::builtIn;
    settings_.kind = "builtIn";
    saveSettings_();
    log_.write (LogLevel::info, "sound", "Using the built-in synth");
    changed ("Built-in synth");
}

void InstrumentService::chooseSoundFont (const std::filesystem::path& path)
{
    log_.write (LogLevel::info, "sound", "Loading SoundFont", { { "file", fileNameForLog (path) } });
    const auto started = juce::Time::getMillisecondCounterHiRes();
    auto font = audio::SoundFontRenderer::load (path);
    if (! font)
    {
        log_.write (LogLevel::error, "sound", "SoundFont failed to load", { { "error", font.error() } });
        changed ("Could not load the SoundFont: " + font.error());
        return;
    }
    saveState();
    closeEditor();
    auto renderer = std::move (font).value();
    soundFontName_ = renderer->name();
    log_.write (LogLevel::info, "sound", "SoundFont loaded",
                { { "presets", std::to_string (renderer->presetCount()) },
                  { "loadMs", std::to_string (static_cast<long long> (juce::Time::getMillisecondCounterHiRes() - started)) } });
    audio_.setInstrument (std::move (renderer));
    selected_ = ui::SoundView::soundFont;
    settings_.kind = "soundFont";
    settings_.soundFontPath = pathToUtf8 (path);
    saveSettings_();
    changed ("SoundFont: " + soundFontName_);
}

bool InstrumentService::loadPlugin (const juce::PluginDescription& description)
{
    const auto [rate, block] = audio_.renderFormat();
    juce::String error;
    log_.write (LogLevel::info, "sound", "Loading plug-in",
                { { "plugin", description.name.toStdString() }, { "vendor", description.manufacturerName.toStdString() },
                  { "version", description.version.toStdString() } });
    auto instance = formats_.createPluginInstance (description, rate, block, error);
    if (instance == nullptr)
    {
        log_.write (LogLevel::error, "sound", "Plug-in failed to load", { { "error", error.toStdString() } });
        changed ("Could not load " + description.name.toStdString() + ": " + error.toStdString());
        return false;
    }

    const auto id = description.createIdentifierString().toStdString();
    if (auto bytes = readBinaryFile (stateFile(), 64u * 1024u * 1024u); bytes && ! bytes.value().empty())
    {
        const auto& data = bytes.value();
        const auto newline = std::find (data.begin(), data.end(), static_cast<std::uint8_t> ('\n'));
        if (newline != data.end() && std::string (data.begin(), newline) == id)
        {
            const auto offset = static_cast<std::size_t> (newline - data.begin()) + 1;
            instance->setStateInformation (data.data() + offset, static_cast<int> (data.size() - offset));
            log_.write (LogLevel::info, "sound", "Instrument settings restored");
        }
    }

    saveState();
    closeEditor();
    audio_.setInstrument (std::make_unique<audio::PluginInstrumentRenderer> (std::move (instance)));
    settings_.kind = "plugin";
    settings_.pluginId = id;
    settings_.pluginName = description.name.toStdString();
    saveSettings_();
    return true;
}

void InstrumentService::choosePlugin (std::size_t index)
{
    std::vector<juce::PluginDescription> list;
    instruments (list);
    if (index >= list.size())
        return;
    if (loadPlugin (list[index]))
    {
        selected_ = ui::SoundView::firstPlugin + static_cast<int> (index);
        changed ("VST3: " + list[index].name.toStdString());
    }
}

void InstrumentService::addPluginFile (const juce::File& file)
{
    log_.write (LogLevel::info, "sound", "Adding plug-in file", { { "file", file.getFileName().toStdString() } });
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* format : formats_.getFormats())
        known_.scanAndAddFile (file.getFullPathName(), true, found, *format);
    saveKnownList();
    const auto instruments = std::count_if (found.begin(), found.end(), [] (auto* d) { return d->isInstrument; });
    log_.write (LogLevel::info, "sound", "Plug-in file scanned",
                { { "types", std::to_string (found.size()) }, { "instruments", std::to_string (instruments) } });
    changed (instruments > 0 ? "Added " + std::to_string (instruments) + " instrument(s) from " + file.getFileName().toStdString()
                             : file.getFileName().toStdString() + " has no VST3 instrument (effects are not used).");
}

void InstrumentService::startScan()
{
    if (scanner_)
        return;
    auto* vst3 = formats_.getFormat (0);
    const auto path = vst3->getDefaultLocationsToSearch();
    log_.write (LogLevel::info, "sound", "Scanning for VST3 instruments", { { "folders", path.toString().toStdString() } });
    scanner_ = std::make_unique<juce::PluginDirectoryScanner> (known_, *vst3, path, true, deadMansPedal());
    startTimer (20);
    changed ("Scanning for VST3 instruments...");
}

void InstrumentService::timerCallback()
{
    if (! scanner_)
    {
        stopTimer();
        return;
    }
    juce::String name;
    const auto next = scanner_->getNextPluginFileThatWillBeScanned();
    if (scanner_->scanNextFile (true, name))
    {
        if (onChange)
        {
            status_ = "Scanning " + juce::File (next).getFileName().toStdString() + "...";
            onChange();
        }
        return;
    }
    for (const auto& failed : scanner_->getFailedFiles())
        log_.write (LogLevel::warning, "sound", "Plug-in could not be scanned",
                    { { "file", juce::File (failed).getFileName().toStdString() } });
    scanner_.reset();
    stopTimer();
    saveKnownList();
    std::vector<juce::PluginDescription> list;
    instruments (list);
    log_.write (LogLevel::info, "sound", "Scan finished", { { "instruments", std::to_string (list.size()) } });
    changed ("Found " + std::to_string (list.size()) + " VST3 instrument(s).");
}

void InstrumentService::showEditor()
{
    auto* renderer = dynamic_cast<audio::PluginInstrumentRenderer*> (audio_.instrument());
    if (renderer == nullptr)
        return;
    if (editorWindow_)
    {
        editorWindow_->toFront (true);
        return;
    }
    auto* editor = renderer->plugin().createEditorIfNeeded();
    if (editor == nullptr)
    {
        changed ("This instrument has no window.");
        return;
    }
    log_.write (LogLevel::info, "sound", "Plug-in window opened");
    editorWindow_ = std::make_unique<PluginWindow> (editor, renderer->plugin().getName(), [this] { closeEditor(); });
}

void InstrumentService::closeEditor()
{
    if (editorWindow_)
    {
        log_.write (LogLevel::info, "sound", "Plug-in window closed");
        editorWindow_.reset();
    }
}

void InstrumentService::saveKnownList()
{
    if (auto xml = known_.createXml())
        xml->writeTo (knownListFile());
}

} // namespace pianoassist::app
