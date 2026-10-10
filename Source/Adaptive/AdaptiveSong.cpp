#include "AdaptiveSong.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <map>

namespace cadenza::adaptive
{
namespace
{
constexpr int kDrumChannel = 9;   // 0-based GM drum channel (MIDI channel 10)

std::string lower(std::string s)
{
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

struct BeatMapContext { double scale; };
double mapByBeats(double tick, const void* ctx)
{
    return tick * static_cast<const BeatMapContext*>(ctx)->scale;
}

struct SecondsMapContext { const TempoMap* from; const TempoMap* to; };
double mapBySeconds(double tick, const void* ctx)
{
    const auto* c = static_cast<const SecondsMapContext*>(ctx);
    return c->to->secondsToTicks(c->from->ticksToSeconds(tick));
}
}

std::vector<TrackInfo> describeTracks(const MidiFileData& file)
{
    std::vector<TrackInfo> out;
    for (int i = 0; i < static_cast<int>(file.tracks.size()); ++i) {
        const auto& t = file.tracks[static_cast<std::size_t>(i)];
        TrackInfo info;
        info.index = i;
        info.name = t.name;
        info.noteCount = static_cast<int>(t.notes.size());
        info.channelMask = t.channelMask();
        info.drums = (info.channelMask & (1u << kDrumChannel)) != 0;
        if (!t.notes.empty()) {
            info.firstTick = t.notes.front().onTick;
            std::map<std::int64_t, int> onsets;
            int counts[16] = {};
            for (const auto& n : t.notes) {
                info.lastTick = std::max(info.lastTick, n.offTick);
                info.lowestNote = std::min<int>(info.lowestNote, n.pitch);
                info.highestNote = std::max<int>(info.highestNote, n.pitch);
                ++onsets[n.onTick];
                ++counts[n.channel];
            }
            info.polyphony = static_cast<double>(t.notes.size()) / static_cast<double>(onsets.size());
            const int mainChannel = static_cast<int>(std::max_element(counts, counts + 16) - counts);
            info.program = t.firstProgram(mainChannel);
        }
        out.push_back(info);
    }
    return out;
}

std::string describeTrack(const TrackInfo& info)
{
    std::string s = std::to_string(info.index + 1) + ": " + (info.name.empty() ? std::string("(unnamed)") : info.name);
    std::string chans;
    for (int c = 0; c < 16; ++c)
        if (info.channelMask & (1u << c)) chans += (chans.empty() ? "" : ",") + std::to_string(c + 1);
    s += " (ch " + (chans.empty() ? std::string("-") : chans) + ", " + std::to_string(info.noteCount) + " notes";
    if (info.drums) s += ", drums";
    s += ")";
    return s;
}

int suggestReferenceTrack(const MidiFileData& file)
{
    const auto infos = describeTracks(file);
    int best = -1;
    double bestScore = -1.0;
    for (const auto& t : infos) {
        if (t.noteCount == 0) continue;
        double score = std::log1p(static_cast<double>(t.noteCount));
        const auto name = lower(t.name);
        static const char* keys[] = { "piano", "keys", "keyboard", "pno", "klavier", "фортепиано", "пианино" };
        for (const char* k : keys)
            if (name.find(k) != std::string::npos) score += 10.0;
        if (t.program && *t.program >= 0 && *t.program <= 7) score += 5.0;   // GM acoustic/electric pianos
        if (t.polyphony > 1.3) score += 2.0;                                 // keyboard-like texture
        if (t.drums && t.channelMask == (1u << kDrumChannel)) score = -1.0; // pure drum track
        if (score > bestScore) { bestScore = score; best = t.index; }
    }
    return best;
}

std::vector<int> suggestBackingTracks(const MidiFileData& file, const std::vector<int>& referenceTracks)
{
    std::vector<int> out;
    for (int i = 0; i < static_cast<int>(file.tracks.size()); ++i) {
        if (std::find(referenceTracks.begin(), referenceTracks.end(), i) != referenceTracks.end()) continue;
        const auto& t = file.tracks[static_cast<std::size_t>(i)];
        bool hasChannelEvents = false;
        for (const auto& e : t.events)
            if (e.kind == MidiEventKind::NoteOn) { hasChannelEvents = true; break; }
        if (hasChannelEvents) out.push_back(i);
    }
    return out;
}

std::array<std::uint8_t, 16> assignOutputChannels(std::uint16_t used, std::uint16_t reserved)
{
    std::array<std::uint8_t, 16> map = BackingSequence::identityChannels();
    std::uint16_t taken = reserved;
    // Channels that keep their own number first.
    for (int c = 0; c < 16; ++c) {
        if (!(used & (1u << c))) continue;
        if (!(reserved & (1u << c))) taken = static_cast<std::uint16_t>(taken | (1u << c));
    }
    for (int c = 0; c < 16; ++c) {
        if (!(used & (1u << c)) || !(reserved & (1u << c))) continue;
        int target = -1;
        for (int o = 15; o >= 0; --o) {   // fill from the top (Yamaha-style parts live high)
            if (o == kDrumChannel && c != kDrumChannel) continue;
            if (taken & (1u << o)) continue;
            target = o;
            break;
        }
        map[static_cast<std::size_t>(c)] = target >= 0 ? static_cast<std::uint8_t>(target + 1) : 0;
        if (target >= 0) taken = static_cast<std::uint16_t>(taken | (1u << target));
    }
    return map;
}

AdaptiveSongBuildResult buildAdaptiveSong(const MidiFileData& refFile, const MidiFileData* backingFile,
                                          const AdaptiveSongOptions& options)
{
    AdaptiveSongBuildResult result;
    auto& song = result.song;
    const MidiFileData& backFile = backingFile != nullptr ? *backingFile : refFile;
    song.twoFiles = backingFile != nullptr;

    // ---- reference ----
    if (options.referenceTracks.empty()) { result.error = "no reference track selected"; return result; }
    ReferenceBuildOptions refOpts = options.reference;
    refOpts.tracks = options.referenceTracks;
    refOpts.channelFilter = options.referenceChannel;
    std::string error;
    auto reference = std::make_shared<ReferencePerformance>(ReferencePerformance::build(refFile, refOpts, &error));
    if (reference->empty()) {
        result.error = error.empty() ? "reference part is empty" : error;
        return result;
    }

    // ---- backing selection (never the reference part itself) ----
    std::vector<TrackSelection> backing;
    for (auto sel : options.backingTracks) {
        if (sel.track < 0 || sel.track >= static_cast<int>(backFile.tracks.size())) {
            song.warnings.push_back("backing track " + std::to_string(sel.track + 1) + " does not exist; ignored");
            continue;
        }
        if (!song.twoFiles
            && std::find(options.referenceTracks.begin(), options.referenceTracks.end(), sel.track) != options.referenceTracks.end()) {
            if (options.referenceChannel < 0) {
                song.warnings.push_back("track " + std::to_string(sel.track + 1)
                                        + " is the reference part; it is not played as backing");
                continue;
            }
            sel.channelMask = static_cast<std::uint16_t>(sel.channelMask & ~(1u << options.referenceChannel));
        }
        backing.push_back(sel);
    }
    if (backing.empty())
        song.warnings.push_back("no backing tracks selected: only the performer will be heard");

    std::uint16_t usedSource = 0;
    for (const auto& sel : backing) {
        const auto& t = backFile.tracks[static_cast<std::size_t>(sel.track)];
        for (const auto& e : t.events) usedSource = static_cast<std::uint16_t>(usedSource | ((1u << e.channel) & sel.channelMask));
    }
    song.channelMap = assignOutputChannels(usedSource, options.reservedOutputChannels);
    for (int c = 0; c < 16; ++c) {
        if (!(usedSource & (1u << c))) continue;
        const int out = song.channelMap[static_cast<std::size_t>(c)];
        if (out == 0) song.warnings.push_back("no free output channel for backing channel " + std::to_string(c + 1) + "; it is muted");
        else if (out != c + 1)
            song.warnings.push_back("backing channel " + std::to_string(c + 1) + " plays on channel " + std::to_string(out)
                                    + " (channel " + std::to_string(c + 1) + " is used by the live keyboard)");
    }

    // ---- timeline mapping ----
    const TempoMap backMap = TempoMap::fromMidiFile(backFile);
    BeatMapContext beatCtx { static_cast<double>(refFile.ppq) / static_cast<double>(backFile.ppq) };
    SecondsMapContext secCtx { &backMap, &reference->tempoMap() };
    BackingSequence::TickMapper mapper = nullptr;
    const void* ctx = nullptr;
    if (song.twoFiles) {
        if (options.alignment == TimelineAlignment::BySeconds) { mapper = &mapBySeconds; ctx = &secCtx; }
        else if (refFile.ppq != backFile.ppq) { mapper = &mapByBeats; ctx = &beatCtx; }

        if (refFile.ppq != backFile.ppq)
            song.warnings.push_back("PPQ differs (reference " + std::to_string(refFile.ppq) + ", backing "
                                    + std::to_string(backFile.ppq) + "): backing rescaled to the reference timeline");

        const double refEndTick = static_cast<double>(std::max<std::int64_t>(reference->endTick(), refFile.lengthTicks()));
        double diff = 0.0;
        const bool sameTempo = reference->tempoMap().equivalentTo(backMap, refEndTick, options.tempoMismatchToleranceSec, &diff);
        if (!sameTempo) {
            song.timelineMismatch = true;
            song.warnings.push_back(std::string("tempo maps of the two files differ (up to ")
                                    + std::to_string(static_cast<int>(diff * 1000.0 + 0.5)) + " ms); aligned "
                                    + (options.alignment == TimelineAlignment::ByBeats ? "by beats" : "by seconds")
                                    + " - check that the files belong together");
        }
        const double refBeats = refEndTick / refFile.ppq;
        const double backBeats = static_cast<double>(backFile.lengthTicks()) / backFile.ppq;
        if (std::abs(refBeats - backBeats) > options.lengthMismatchToleranceBeats)
            song.warnings.push_back("lengths differ: reference " + std::to_string(static_cast<int>(refBeats))
                                    + " beats, backing " + std::to_string(static_cast<int>(backBeats)) + " beats");
    }

    auto seq = std::make_shared<BackingSequence>(BackingSequence::build(backFile, backing, song.channelMap, mapper, ctx));
    song.reference = reference;
    song.backing = seq;
    song.startNominalSec = reference->group(0).nominalSec;
    const double endTick = std::max(static_cast<double>(reference->endTick()), seq->endTick());
    song.endNominalSec = reference->tempoMap().ticksToSeconds(endTick);
    result.ok = true;
    return result;
}
}
