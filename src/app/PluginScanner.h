#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace pianoassist::app
{

/** Command-line switch for the scan helper: the app runs itself as
    "PianoAssist.exe --scan-plugin <file.vst3> <result.xml>" so that a plug-in
    that crashes while loading takes down only that helper process. */
inline constexpr const char* scanPluginSwitch = "--scan-plugin";

/** Runs inside the helper process: loads the file, writes the plug-in
    descriptions as XML and returns the process exit code. */
int runPluginScanHelper (const juce::StringArray& arguments);

/** Why a file did not yield an instrument. */
enum class ScanProblem
{
    none,
    notVst3,       ///< .dll (VST2), .clap, .component...: not supported
    crashed,       ///< the helper process crashed while loading it
    timedOut,      ///< loading took longer than the limit
    failedToLoad,  ///< loaded nothing: damaged, 32-bit, or a missing dependency
    noInstrument,  ///< loaded, but only effects inside
};

juce::String describe (ScanProblem problem, const juce::String& fileName);

/** Scans plug-in files one at a time, each in a helper process. Results
    arrive on the message thread through the callbacks. */
class PluginScanner final : private juce::Timer
{
public:
    struct Result
    {
        juce::String file;
        ScanProblem problem = ScanProblem::none;
        juce::OwnedArray<juce::PluginDescription> types;
    };

    std::function<void (const juce::String& file, int index, int total)> onProgress;
    std::function<void (Result&)> onResult;
    std::function<void()> onFinished;

    ~PluginScanner() override;

    void scan (juce::StringArray files);
    bool isScanning() const noexcept { return ! queue_.isEmpty() || child_ != nullptr; }
    void cancel();

    static constexpr int timeoutMs = 90'000;

private:
    void timerCallback() override;
    void startNext();
    void finishCurrent (bool timedOut);

    juce::StringArray queue_;
    int total_ = 0;
    int done_ = 0;
    juce::String current_;
    juce::File resultFile_;
    std::unique_ptr<juce::ChildProcess> child_;
    juce::uint32 startedMs_ = 0;
};

/** True when the file looks like a plug-in format PianoAssist does not host. */
bool isUnsupportedPluginFile (const juce::File& file);

} // namespace pianoassist::app
