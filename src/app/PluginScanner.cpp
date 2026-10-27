#include "app/PluginScanner.h"

namespace pianoassist::app
{

bool isUnsupportedPluginFile (const juce::File& file)
{
    return file.hasFileExtension ("dll;clap;component;so;aaxplugin;vst");
}

juce::String describe (ScanProblem problem, const juce::String& fileName)
{
    switch (problem)
    {
        case ScanProblem::none:
            return {};
        case ScanProblem::notVst3:
            return fileName + ": " + TRANS ("this format is not supported. PianoAssist uses 64-bit VST3 instruments (.vst3); "
                                            "VST2 (.dll), CLAP and AU plug-ins cannot be loaded.");
        case ScanProblem::crashed:
            return fileName + ": " + TRANS ("the plug-in crashed while loading and is skipped. Reinstall or update it, then scan again.");
        case ScanProblem::timedOut:
            return fileName + ": " + TRANS ("the plug-in did not finish loading in time and is skipped for now.");
        case ScanProblem::failedToLoad:
            return fileName + ": " + TRANS ("could not be loaded. It may be 32-bit, damaged, or missing a part its installer adds.");
        case ScanProblem::noInstrument:
            return fileName + ": " + TRANS ("contains only effects, no instrument.");
    }
    return {};
}

int runPluginScanHelper (const juce::StringArray& arguments)
{
    const auto index = arguments.indexOf (scanPluginSwitch);
    if (index < 0 || index + 2 >= arguments.size())
        return 2;
    const auto file = arguments[index + 1].unquoted();
    const juce::File out (arguments[index + 2].unquoted());

    juce::VST3PluginFormat format;
    juce::OwnedArray<juce::PluginDescription> types;
    format.findAllTypesForFile (types, file);

    juce::XmlElement root ("PLUGINS");
    for (auto* type : types)
        root.addChildElement (type->createXml().release());
    if (! root.writeTo (out))
        return 3;
    return types.isEmpty() ? 1 : 0;
}

PluginScanner::~PluginScanner()
{
    cancel();
}

void PluginScanner::scan (juce::StringArray files)
{
    for (const auto& f : files)
        queue_.addIfNotAlreadyThere (f);
    total_ = done_ + queue_.size() + (child_ != nullptr ? 1 : 0);
    if (child_ == nullptr)
        startNext();
}

void PluginScanner::cancel()
{
    stopTimer();
    queue_.clear();
    if (child_ != nullptr)
        child_->kill();
    child_.reset();
    resultFile_.deleteFile();
}

void PluginScanner::startNext()
{
    while (! queue_.isEmpty())
    {
        current_ = queue_[0];
        queue_.remove (0);
        const juce::File file (current_);
        if (isUnsupportedPluginFile (file))
        {
            Result result;
            result.file = current_;
            result.problem = ScanProblem::notVst3;
            ++done_;
            if (onResult)
                onResult (result);
            continue;
        }

        if (onProgress)
            onProgress (current_, done_ + 1, total_);
        resultFile_ = juce::File::createTempFile (".xml");
        child_ = std::make_unique<juce::ChildProcess>();
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        if (! child_->start (juce::StringArray { exe, scanPluginSwitch, current_, resultFile_.getFullPathName() }, 0))
        {
            child_.reset();
            Result result;
            result.file = current_;
            result.problem = ScanProblem::failedToLoad;
            ++done_;
            if (onResult)
                onResult (result);
            continue;
        }
        startedMs_ = juce::Time::getMillisecondCounter();
        startTimer (100);
        return;
    }
    stopTimer();
    total_ = done_ = 0;
    if (onFinished)
        onFinished();
}

void PluginScanner::timerCallback()
{
    if (child_ == nullptr)
        return startNext();
    if (child_->isRunning())
    {
        if (juce::Time::getMillisecondCounter() - startedMs_ > static_cast<juce::uint32> (timeoutMs))
        {
            child_->kill();
            finishCurrent (true);
        }
        return;
    }
    finishCurrent (false);
}

void PluginScanner::finishCurrent (bool timedOut)
{
    Result result;
    result.file = current_;
    const auto exitCode = timedOut ? 0u : child_->getExitCode();
    child_.reset();
    stopTimer();

    if (auto xml = juce::XmlDocument::parse (resultFile_))
        for (auto* e : xml->getChildIterator())
        {
            auto description = std::make_unique<juce::PluginDescription>();
            if (description->loadFromXml (*e))
                result.types.add (description.release());
        }
    resultFile_.deleteFile();

    if (timedOut)
        result.problem = ScanProblem::timedOut;
    else if (exitCode > 3) // not one of ours: the helper died
        result.problem = ScanProblem::crashed;
    else if (result.types.isEmpty())
        result.problem = ScanProblem::failedToLoad;
    else if (std::none_of (result.types.begin(), result.types.end(), [] (auto* d) { return d->isInstrument; }))
        result.problem = ScanProblem::noInstrument;

    ++done_;
    if (onResult)
        onResult (result);
    startNext();
}

} // namespace pianoassist::app
