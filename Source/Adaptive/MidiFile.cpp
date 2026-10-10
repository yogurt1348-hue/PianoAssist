#include "MidiFile.h"

#include <algorithm>
#include <array>
#include <deque>
#include <fstream>
#include <iterator>

namespace cadenza::adaptive
{
namespace
{
// Sanity limits so a corrupt header can't make us allocate or loop forever.
constexpr int kMaxTracks = 1024;
constexpr std::int64_t kMaxTick = std::int64_t { 1 } << 40;

class Reader
{
public:
    explicit Reader(const std::vector<std::uint8_t>& bytes) : m_bytes(bytes) {}

    bool ok() const noexcept { return m_ok; }
    std::size_t pos() const noexcept { return m_pos; }
    std::size_t size() const noexcept { return m_bytes.size(); }
    std::size_t remaining() const noexcept { return m_pos < m_bytes.size() ? m_bytes.size() - m_pos : 0; }
    void seek(std::size_t p) noexcept { m_pos = std::min(p, m_bytes.size()); }

    std::uint8_t u8() noexcept
    {
        if (m_pos >= m_bytes.size()) { m_ok = false; return 0; }
        return m_bytes[m_pos++];
    }
    std::uint16_t u16() noexcept { const auto a = u8(); const auto b = u8(); return static_cast<std::uint16_t>((a << 8) | b); }
    std::uint32_t u32() noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v = (v << 8) | u8();
        return v;
    }
    // Variable-length quantity (max 4 bytes per the spec).
    std::uint32_t vlq() noexcept
    {
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) {
            const auto b = u8();
            v = (v << 7) | (b & 0x7F);
            if ((b & 0x80) == 0) return v;
        }
        m_ok = false;
        return v;
    }
    bool tag(const char* t) noexcept
    {
        if (remaining() < 4) { m_ok = false; return false; }
        for (int i = 0; i < 4; ++i)
            if (m_bytes[m_pos + static_cast<std::size_t>(i)] != static_cast<std::uint8_t>(t[i])) return false;
        m_pos += 4;
        return true;
    }
    std::string text(std::uint32_t len)
    {
        const auto n = std::min<std::size_t>(len, remaining());
        std::string s(reinterpret_cast<const char*>(m_bytes.data() + m_pos), n);
        m_pos += n;
        if (n < len) m_ok = false;
        return s;
    }
    void skip(std::uint32_t len) noexcept
    {
        if (len > remaining()) { m_pos = m_bytes.size(); m_ok = false; return; }
        m_pos += len;
    }

private:
    const std::vector<std::uint8_t>& m_bytes;
    std::size_t m_pos = 0;
    bool m_ok = true;
};

int dataBytesFor(std::uint8_t status) noexcept
{
    switch (status & 0xF0) {
        case 0xC0: case 0xD0: return 1;
        default:              return 2;
    }
}

void pairNotes(MidiTrackData& track)
{
    track.notes.clear();
    // FIFO of open note indices per (channel, pitch).
    std::array<std::deque<std::size_t>, 16 * 128> open;
    for (const auto& e : track.events) {
        if (e.kind != MidiEventKind::NoteOn && e.kind != MidiEventKind::NoteOff) continue;
        auto& q = open[static_cast<std::size_t>(e.channel) * 128 + e.data1];
        if (e.kind == MidiEventKind::NoteOn) {
            q.push_back(track.notes.size());
            track.notes.push_back({ e.tick, e.tick, e.channel, e.data1, e.data2 });
        } else if (!q.empty()) {
            track.notes[q.front()].offTick = e.tick;
            q.pop_front();
        }
    }
    // Unterminated notes ring to the end of the track (at least one tick).
    for (auto& q : open)
        for (auto idx : q)
            track.notes[idx].offTick = std::max(track.endTick, track.notes[idx].onTick + 1);

    std::stable_sort(track.notes.begin(), track.notes.end(), [](const MidiNote& a, const MidiNote& b) {
        if (a.onTick != b.onTick) return a.onTick < b.onTick;
        return a.pitch < b.pitch;
    });
}

bool parseTrack(Reader& r, std::size_t end, MidiTrackData& track, MidiFileData& file, int trackIndex)
{
    std::int64_t tick = 0;
    std::uint8_t running = 0;
    bool sawEnd = false;

    while (r.ok() && r.pos() < end) {
        tick += r.vlq();
        if (!r.ok()) break;
        if (tick > kMaxTick) {
            file.warnings.push_back("track " + std::to_string(trackIndex) + ": absurd tick value; rest of track skipped");
            r.seek(end);
            break;
        }

        std::uint8_t status = r.u8();
        if (status < 0x80) {
            if (running == 0) {
                file.warnings.push_back("track " + std::to_string(trackIndex)
                                        + ": data byte without running status; rest of track skipped");
                r.seek(end);
                break;
            }
            r.seek(r.pos() - 1);
            status = running;
        }

        if (status >= 0x80 && status <= 0xEF) {
            running = status;
            const std::uint8_t ch = status & 0x0F;
            const std::uint8_t d1 = r.u8() & 0x7F;
            const std::uint8_t d2 = dataBytesFor(status) == 2 ? static_cast<std::uint8_t>(r.u8() & 0x7F) : 0;
            MidiChannelEvent e;
            e.tick = tick;
            e.channel = ch;
            e.data1 = d1;
            e.data2 = d2;
            switch (status & 0xF0) {
                case 0x80: e.kind = MidiEventKind::NoteOff; e.data2 = 0; break;
                case 0x90: e.kind = d2 == 0 ? MidiEventKind::NoteOff : MidiEventKind::NoteOn; break;
                case 0xA0: e.kind = MidiEventKind::PolyPressure; break;
                case 0xB0: e.kind = MidiEventKind::ControlChange; break;
                case 0xC0: e.kind = MidiEventKind::ProgramChange; break;
                case 0xD0: e.kind = MidiEventKind::ChannelPressure; break;
                case 0xE0:
                    e.kind = MidiEventKind::PitchBend;
                    e.bend = (static_cast<int>(d2) << 7) | static_cast<int>(d1);
                    break;
                default: break;
            }
            if (r.ok()) track.events.push_back(e);
        } else if (status == 0xFF) {
            running = 0;   // meta events cancel running status (common reader behaviour)
            const std::uint8_t type = r.u8();
            const std::uint32_t len = r.vlq();
            if (type == 0x2F) {
                r.skip(len);
                sawEnd = true;
                track.endTick = tick;
                break;
            }
            if (type == 0x03) {
                auto name = r.text(len);
                if (track.name.empty()) track.name = std::move(name);
            } else if (type == 0x51 && len == 3) {
                const std::uint32_t a = r.u8(), b = r.u8(), c = r.u8();
                const std::uint32_t us = (a << 16) | (b << 8) | c;
                if (us > 0) file.tempos.push_back({ tick, us });
                else file.warnings.push_back("ignored zero tempo meta event");
            } else if (type == 0x58 && len == 4) {
                const int num = r.u8();
                const int denPow = r.u8();
                r.u8(); r.u8();
                if (num > 0 && denPow < 8) file.timeSignatures.push_back({ tick, num, 1 << denPow });
                else file.warnings.push_back("ignored invalid time signature");
            } else {
                r.skip(len);
            }
        } else if (status == 0xF0 || status == 0xF7) {
            running = 0;
            r.skip(r.vlq());
        } else {
            file.warnings.push_back("track " + std::to_string(trackIndex)
                                    + ": unsupported status byte; rest of track skipped");
            r.seek(end);
            break;
        }
    }

    if (!r.ok())
        file.warnings.push_back("track " + std::to_string(trackIndex) + ": truncated");
    if (!sawEnd) {
        track.endTick = tick;
        if (r.ok()) file.warnings.push_back("track " + std::to_string(trackIndex) + ": missing End-of-Track");
    }
    for (const auto& e : track.events) track.endTick = std::max(track.endTick, e.tick);
    pairNotes(track);
    return true;
}

// --- writer helpers ---
void putVlq(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    std::uint8_t buf[5];
    int n = 0;
    buf[n++] = static_cast<std::uint8_t>(v & 0x7F);
    while ((v >>= 7) != 0) buf[n++] = static_cast<std::uint8_t>((v & 0x7F) | 0x80);
    while (n > 0) out.push_back(buf[--n]);
}
void put32(std::vector<std::uint8_t>& out, std::uint32_t v)
{
    for (int s = 24; s >= 0; s -= 8) out.push_back(static_cast<std::uint8_t>((v >> s) & 0xFF));
}
void put16(std::vector<std::uint8_t>& out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

int eventOrder(MidiEventKind k) noexcept
{
    // On equal ticks: offs first (so a re-struck note isn't cut), then controls, then ons.
    switch (k) {
        case MidiEventKind::NoteOff: return 0;
        case MidiEventKind::NoteOn:  return 2;
        default:                     return 1;
    }
}
}

std::uint16_t MidiTrackData::channelMask() const noexcept
{
    std::uint16_t mask = 0;
    for (const auto& n : notes) mask = static_cast<std::uint16_t>(mask | (1u << n.channel));
    return mask;
}

bool MidiTrackData::usesChannel(int channel) const noexcept
{
    return channel >= 0 && channel < 16 && (channelMask() & (1u << channel)) != 0;
}

std::optional<int> MidiTrackData::firstProgram(int channel) const
{
    for (const auto& e : events)
        if (e.kind == MidiEventKind::ProgramChange && e.channel == channel)
            return static_cast<int>(e.data1);
    return std::nullopt;
}

std::int64_t MidiFileData::lengthTicks() const noexcept
{
    std::int64_t len = 0;
    for (const auto& t : tracks) len = std::max(len, t.endTick);
    return len;
}

std::size_t MidiFileData::noteCount() const noexcept
{
    std::size_t n = 0;
    for (const auto& t : tracks) n += t.notes.size();
    return n;
}

MidiFileParseResult parseMidiFile(const std::vector<std::uint8_t>& bytes)
{
    MidiFileParseResult result;
    Reader r(bytes);

    // Tolerate a RIFF/RMID wrapper by searching for the MThd tag in the first 64 bytes.
    std::size_t start = bytes.size();
    const std::size_t searchEnd = std::min<std::size_t>(bytes.size(), 64);
    for (std::size_t i = 0; i + 4 <= searchEnd; ++i) {
        if (bytes[i] == 'M' && bytes[i + 1] == 'T' && bytes[i + 2] == 'h' && bytes[i + 3] == 'd') { start = i; break; }
    }
    if (bytes.empty()) { result.error = "empty file"; return result; }
    if (start >= bytes.size() || bytes.size() - start < 14) {
        result.error = "not a Standard MIDI File (no MThd header)";
        return result;
    }
    r.seek(start);
    if (!r.tag("MThd")) { result.error = "not a Standard MIDI File (no MThd header)"; return result; }
    const std::uint32_t headerLen = r.u32();
    if (headerLen < 6) { result.error = "invalid MThd length"; return result; }
    const int format = r.u16();
    const int declaredTracks = r.u16();
    const std::uint16_t division = r.u16();
    r.skip(headerLen - 6);
    if (!r.ok()) { result.error = "truncated MThd header"; return result; }
    if (format > 2) { result.error = "unknown SMF format " + std::to_string(format); return result; }
    if (format == 2) { result.error = "SMF format 2 (independent sequences) is not supported"; return result; }
    if ((division & 0x8000) != 0) { result.error = "SMPTE time division is not supported"; return result; }
    if (division == 0) { result.error = "invalid time division (0 PPQ)"; return result; }
    if (declaredTracks > kMaxTracks) { result.error = "too many tracks"; return result; }

    auto& data = result.data;
    data.format = format;
    data.ppq = division;

    int t = 0;
    while (r.remaining() >= 8 && t < kMaxTracks) {
        if (r.tag("MTrk")) {
            std::uint32_t len = r.u32();
            if (len > r.remaining()) {
                data.warnings.push_back("track " + std::to_string(t) + ": chunk length exceeds file size");
                len = static_cast<std::uint32_t>(r.remaining());
            }
            const std::size_t end = r.pos() + len;
            MidiTrackData track;
            parseTrack(r, end, track, data, t);
            r.seek(end);
            data.tracks.push_back(std::move(track));
            ++t;
        } else {
            if (!r.ok()) break;
            r.seek(r.pos() + 4);            // unknown chunk: skip tag + body
            const std::uint32_t len = r.u32();
            r.skip(len);
            if (!r.ok()) break;
        }
    }

    if (data.tracks.empty()) { result.error = "no MTrk chunks"; return result; }
    if (static_cast<int>(data.tracks.size()) != declaredTracks)
        data.warnings.push_back("header declares " + std::to_string(declaredTracks) + " tracks, found "
                                + std::to_string(data.tracks.size()));

    std::stable_sort(data.tempos.begin(), data.tempos.end(),
                     [](const TempoEvent& a, const TempoEvent& b) { return a.tick < b.tick; });
    std::stable_sort(data.timeSignatures.begin(), data.timeSignatures.end(),
                     [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) { return a.tick < b.tick; });
    result.ok = true;
    return result;
}

MidiFileParseResult parseMidiFileFromPath(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        MidiFileParseResult r;
        r.error = "cannot open file: " + path;
        return r;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return parseMidiFile(bytes);
}

void sortTrackEvents(MidiTrackData& track)
{
    std::stable_sort(track.events.begin(), track.events.end(), [](const MidiChannelEvent& a, const MidiChannelEvent& b) {
        if (a.tick != b.tick) return a.tick < b.tick;
        return eventOrder(a.kind) < eventOrder(b.kind);
    });
}

void finalizeTrack(MidiTrackData& track)
{
    sortTrackEvents(track);
    for (const auto& e : track.events) track.endTick = std::max(track.endTick, e.tick);
    pairNotes(track);
}

void addNote(MidiTrackData& track, std::int64_t onTick, std::int64_t lengthTicks,
             int pitch, int velocity, int channel)
{
    MidiChannelEvent on;
    on.tick = onTick;
    on.kind = MidiEventKind::NoteOn;
    on.channel = static_cast<std::uint8_t>(channel & 0x0F);
    on.data1 = static_cast<std::uint8_t>(pitch & 0x7F);
    on.data2 = static_cast<std::uint8_t>(std::clamp(velocity, 1, 127));
    MidiChannelEvent off = on;
    off.tick = onTick + std::max<std::int64_t>(1, lengthTicks);
    off.kind = MidiEventKind::NoteOff;
    off.data2 = 0;
    track.events.push_back(on);
    track.events.push_back(off);
}

std::vector<std::uint8_t> writeMidiFile(const MidiFileData& data)
{
    std::vector<std::uint8_t> out;
    out.insert(out.end(), { 'M', 'T', 'h', 'd' });
    put32(out, 6);
    const bool format0 = data.format == 0 && data.tracks.size() == 1;
    put16(out, format0 ? 0 : 1);
    put16(out, static_cast<std::uint16_t>(std::max<std::size_t>(1, data.tracks.size())));
    put16(out, static_cast<std::uint16_t>(std::clamp(data.ppq, 1, 0x7FFF)));

    const auto writeTrack = [&](const MidiTrackData* track, bool withConductor) {
        struct Item { std::int64_t tick; int order; std::vector<std::uint8_t> bytes; };
        std::vector<Item> items;
        if (track != nullptr && !track->name.empty()) {
            std::vector<std::uint8_t> b { 0xFF, 0x03 };
            putVlq(b, static_cast<std::uint32_t>(track->name.size()));
            b.insert(b.end(), track->name.begin(), track->name.end());
            items.push_back({ 0, -2, std::move(b) });
        }
        if (withConductor) {
            for (const auto& ts : data.timeSignatures) {
                int pow = 0;
                while ((1 << pow) < ts.denominator && pow < 7) ++pow;
                items.push_back({ ts.tick, -1, { 0xFF, 0x58, 0x04, static_cast<std::uint8_t>(ts.numerator),
                                                 static_cast<std::uint8_t>(pow), 24, 8 } });
            }
            for (const auto& te : data.tempos)
                items.push_back({ te.tick, -1, { 0xFF, 0x51, 0x03,
                                                 static_cast<std::uint8_t>((te.microsPerQuarter >> 16) & 0xFF),
                                                 static_cast<std::uint8_t>((te.microsPerQuarter >> 8) & 0xFF),
                                                 static_cast<std::uint8_t>(te.microsPerQuarter & 0xFF) } });
        }
        std::int64_t endTick = 0;
        if (track != nullptr) {
            endTick = track->endTick;
            for (const auto& e : track->events) {
                std::vector<std::uint8_t> b;
                const auto ch = static_cast<std::uint8_t>(e.channel & 0x0F);
                switch (e.kind) {
                    case MidiEventKind::NoteOff:         b = { static_cast<std::uint8_t>(0x80 | ch), e.data1, 0 }; break;
                    case MidiEventKind::NoteOn:          b = { static_cast<std::uint8_t>(0x90 | ch), e.data1, e.data2 }; break;
                    case MidiEventKind::PolyPressure:    b = { static_cast<std::uint8_t>(0xA0 | ch), e.data1, e.data2 }; break;
                    case MidiEventKind::ControlChange:   b = { static_cast<std::uint8_t>(0xB0 | ch), e.data1, e.data2 }; break;
                    case MidiEventKind::ProgramChange:   b = { static_cast<std::uint8_t>(0xC0 | ch), e.data1 }; break;
                    case MidiEventKind::ChannelPressure: b = { static_cast<std::uint8_t>(0xD0 | ch), e.data1 }; break;
                    case MidiEventKind::PitchBend:
                        b = { static_cast<std::uint8_t>(0xE0 | ch), static_cast<std::uint8_t>(e.bend & 0x7F),
                              static_cast<std::uint8_t>((e.bend >> 7) & 0x7F) };
                        break;
                }
                items.push_back({ e.tick, eventOrder(e.kind), std::move(b) });
            }
        }
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
            if (a.tick != b.tick) return a.tick < b.tick;
            return a.order < b.order;
        });

        std::vector<std::uint8_t> body;
        std::int64_t last = 0;
        for (const auto& it : items) {
            putVlq(body, static_cast<std::uint32_t>(std::max<std::int64_t>(0, it.tick - last)));
            last = std::max(last, it.tick);
            body.insert(body.end(), it.bytes.begin(), it.bytes.end());
        }
        putVlq(body, static_cast<std::uint32_t>(std::max<std::int64_t>(0, endTick - last)));
        body.insert(body.end(), { 0xFF, 0x2F, 0x00 });

        out.insert(out.end(), { 'M', 'T', 'r', 'k' });
        put32(out, static_cast<std::uint32_t>(body.size()));
        out.insert(out.end(), body.begin(), body.end());
    };

    if (data.tracks.empty()) {
        writeTrack(nullptr, true);
    } else {
        for (std::size_t i = 0; i < data.tracks.size(); ++i)
            writeTrack(&data.tracks[i], i == 0);
    }
    return out;
}
}
