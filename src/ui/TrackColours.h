#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pianoassist::ui
{

/** Track colours: 0xAARRGGBB. Automatic colours come from the instrument
    family (bass teal, strings lilac, flutes light blue, drums amber, piano
    soft green...), and a second or third track of the same family gets a
    darker or lighter shade, so two Piccolos stay apart. Plain integers so
    the rules are testable without a GUI. */
struct TrackColourKey
{
    int program = -1;   ///< General MIDI program, 0-based; -1 when the file sets none
    bool drums = false; ///< channel 10
};

namespace trackcolours
{
inline constexpr std::uint32_t piano = 0xff7fcf95;      // soft green
inline constexpr std::uint32_t chromatic = 0xffb5d77a;  // lime
inline constexpr std::uint32_t organ = 0xffff9e6b;      // orange
inline constexpr std::uint32_t guitar = 0xffd8ac7a;     // tan
inline constexpr std::uint32_t bass = 0xff3fbfb4;       // teal
inline constexpr std::uint32_t strings = 0xffb79ce0;    // lilac
inline constexpr std::uint32_t brass = 0xffeccb57;      // yellow
inline constexpr std::uint32_t reed = 0xffffb199;       // peach
inline constexpr std::uint32_t pipe = 0xff74c4f5;       // light blue
inline constexpr std::uint32_t pipeDark = 0xff3e86d6;   // darker blue for a second flute
inline constexpr std::uint32_t lead = 0xfff38db6;       // pink
inline constexpr std::uint32_t pad = 0xff9aa6e6;        // periwinkle
inline constexpr std::uint32_t effects = 0xffef7aa8;    // rose
inline constexpr std::uint32_t ethnic = 0xffe6a3d6;     // orchid
inline constexpr std::uint32_t drums = 0xfff5b041;      // amber
inline constexpr std::uint32_t other = 0xffaab7c4;      // grey blue

/** Swatches for the colour picker, the family colours first. */
inline constexpr std::array<std::uint32_t, 18> swatches {
    piano, bass, strings, pipe, pipeDark, lead, drums, organ, guitar,
    brass, reed, pad, effects, ethnic, chromatic, other, 0xffe57373, 0xff4db6e8,
};

inline std::uint32_t familyColour (const TrackColourKey& key)
{
    if (key.drums)
        return drums;
    const int p = key.program < 0 ? 0 : key.program; // no program: General MIDI plays a piano
    switch (p / 8)
    {
        case 0:  return piano;
        case 1:  return chromatic;
        case 2:  return organ;
        case 3:  return guitar;
        case 4:  return bass;
        case 5:  return strings;
        case 6:  return strings; // ensembles: strings, choirs, synth strings
        case 7:  return brass;
        case 8:  return reed;
        case 9:  return pipe;
        case 10: return lead;
        case 11: return pad;
        case 12: return effects;
        case 13: return ethnic;
        case 14: return drums; // percussive
        default: return other;
    }
}

inline std::uint32_t scaled (std::uint32_t argb, double factor)
{
    const auto channel = [&] (int shift) {
        const double v = static_cast<double> ((argb >> shift) & 0xffu);
        const double out = factor < 1.0 ? v * factor : v + (255.0 - v) * (factor - 1.0);
        return static_cast<std::uint32_t> (std::clamp (out, 0.0, 255.0) + 0.5) << shift;
    };
    return (argb & 0xff000000u) | channel (16) | channel (8) | channel (0);
}

/** The n-th track of one family (0-based): base, darker, lighter, darker still. */
inline std::uint32_t shade (std::uint32_t base, int n)
{
    if (n <= 0)
        return base;
    if (base == pipe && n == 1)
        return pipeDark;
    switch (n % 4)
    {
        case 1:  return scaled (base, 0.68);
        case 2:  return scaled (base, 1.35);
        case 3:  return scaled (base, 0.5);
        default: return scaled (base, 1.6);
    }
}
} // namespace trackcolours

/** One automatic colour per track, in track order. */
inline std::vector<std::uint32_t> autoTrackColours (const std::vector<TrackColourKey>& keys)
{
    std::vector<std::uint32_t> colours;
    std::vector<std::uint32_t> used;
    for (const auto& key : keys)
    {
        const auto base = trackcolours::familyColour (key);
        const auto n = static_cast<int> (std::count (used.begin(), used.end(), base));
        used.push_back (base);
        colours.push_back (trackcolours::shade (base, n));
    }
    return colours;
}

/** "#RRGGBB" (opaque) to 0xffRRGGBB; also accepts "RRGGBB". */
inline std::optional<std::uint32_t> parseHexColour (std::string text)
{
    text.erase (std::remove_if (text.begin(), text.end(), [] (unsigned char c) { return std::isspace (c) != 0; }), text.end());
    if (! text.empty() && text.front() == '#')
        text.erase (text.begin());
    if (text.size() != 6)
        return std::nullopt;
    std::uint32_t value = 0;
    for (const char c : text)
    {
        const auto d = std::tolower (static_cast<unsigned char> (c));
        value <<= 4;
        if (d >= '0' && d <= '9')
            value |= static_cast<std::uint32_t> (d - '0');
        else if (d >= 'a' && d <= 'f')
            value |= static_cast<std::uint32_t> (d - 'a' + 10);
        else
            return std::nullopt;
    }
    return 0xff000000u | value;
}

inline std::string hexColour (std::uint32_t argb)
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "#";
    for (int shift = 20; shift >= 0; shift -= 4)
        text += digits[(argb >> shift) & 0xfu];
    return text;
}

} // namespace pianoassist::ui
