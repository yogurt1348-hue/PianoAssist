#include "core/NoteName.h"
#include "core/SpscQueue.h"
#include "midi_io/DeviceSelection.h"
#include "midi_io/InputMonitor.h"
#include "midi_io/MidiNormalizer.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace pianoassist;
using namespace pianoassist::midi_io;

namespace
{
std::uint64_t nextSequence = 0;

InputEvent raw (std::vector<std::uint8_t> bytes, std::int64_t micros)
{
    auto event = normalizeMidiMessage (bytes.data(), bytes.size(), micros, nextSequence++);
    REQUIRE (event.has_value());
    return *event;
}

InputEvent on (std::uint8_t pitch, std::int64_t micros, std::uint8_t velocity = 80)
{
    return raw ({ 0x90, pitch, velocity }, micros);
}

InputEvent off (std::uint8_t pitch, std::int64_t micros)
{
    return raw ({ 0x80, pitch, 0x40 }, micros);
}
} // namespace

TEST_CASE ("Raw MIDI messages normalize to input events")
{
    SECTION ("note-on")
    {
        const auto e = raw ({ 0x93, 60, 100 }, 5);
        CHECK (e.type == InputEventType::noteOn);
        CHECK (e.channel == 3);
        CHECK (e.data1 == 60);
        CHECK (e.data2 == 100);
        CHECK (e.timestampMicros == 5);
    }
    SECTION ("zero-velocity note-on is a note-off")
    {
        CHECK (raw ({ 0x90, 60, 0 }, 0).type == InputEventType::noteOff);
    }
    SECTION ("note-off")
    {
        CHECK (raw ({ 0x80, 60, 64 }, 0).type == InputEventType::noteOff);
    }
    SECTION ("sustain pedal")
    {
        const auto e = raw ({ 0xB0, 64, 127 }, 0);
        CHECK (e.type == InputEventType::sustain);
        CHECK (e.data2 == 127);
    }
    SECTION ("other controllers and system messages are counted, not dropped")
    {
        CHECK (raw ({ 0xB0, 7, 100 }, 0).type == InputEventType::other);
        CHECK (raw ({ 0xFE }, 0).type == InputEventType::other); // active sensing
        CHECK (raw ({ 0xC0, 5 }, 0).type == InputEventType::other);
    }
    SECTION ("truncated or empty messages are rejected")
    {
        const std::uint8_t truncated[] = { 0x90, 60 };
        CHECK_FALSE (normalizeMidiMessage (truncated, 2, 0, 0).has_value());
        CHECK_FALSE (normalizeMidiMessage (nullptr, 0, 0, 0).has_value());
    }
}

TEST_CASE ("Monitor tracks a chord, sustain and the last note")
{
    InputMonitor monitor;
    monitor.apply (on (60, 1000));
    monitor.apply (on (64, 1010));
    monitor.apply (on (67, 1020, 110));
    monitor.apply (raw ({ 0xB0, 64, 127 }, 1030));

    CHECK (monitor.heldPitches() == std::vector<std::uint8_t> { 60, 64, 67 });
    CHECK (monitor.sustainDown());
    REQUIRE (monitor.lastNote().has_value());
    CHECK (monitor.lastNote()->pitch == 67);
    CHECK (monitor.lastNote()->velocity == 110);

    monitor.apply (raw ({ 0x90, 64, 0 }, 2000));
    monitor.apply (raw ({ 0xB0, 64, 0 }, 2010));
    CHECK (monitor.heldPitches() == std::vector<std::uint8_t> { 60, 67 });
    CHECK_FALSE (monitor.sustainDown());
}

TEST_CASE ("Device loss during a held chord leaves nothing stuck")
{
    InputMonitor monitor;
    monitor.apply (on (48, 0));
    monitor.apply (on (52, 0));
    monitor.apply (raw ({ 0xB0, 64, 127 }, 0));

    const auto released = monitor.releaseAll();
    CHECK (released == std::vector<std::uint8_t> { 48, 52 });
    CHECK (monitor.heldPitches().empty());
    CHECK_FALSE (monitor.sustainDown());

    // The note-offs that arrive after reconnecting are harmless orphans.
    monitor.apply (off (48, 10));
    CHECK (monitor.counters().orphanNoteOffs == 1);
    CHECK (monitor.heldPitches().empty());
}

TEST_CASE ("Duplicate, missing and out-of-order events are counted")
{
    InputMonitor monitor;
    monitor.apply (on (60, 100));
    monitor.apply (on (60, 200)); // duplicate note-on, missing off in between
    monitor.apply (off (61, 300)); // off without on
    monitor.apply (on (62, 150)); // older timestamp than the previous event

    const auto& c = monitor.counters();
    CHECK (c.noteOns == 3);
    CHECK (c.duplicateNoteOns == 1);
    CHECK (c.orphanNoteOffs == 1);
    CHECK (c.outOfOrderEvents == 1);
    CHECK (monitor.isHeld (60));
    CHECK (monitor.isHeld (62));
}

TEST_CASE ("Equal timestamps are ordered by sequence number")
{
    InputMonitor monitor;
    monitor.apply (InputEvent { 500, 1, InputEventType::noteOn, 0, 60, 90 });
    monitor.apply (InputEvent { 500, 2, InputEventType::noteOn, 0, 64, 90 });
    CHECK (monitor.counters().outOfOrderEvents == 0);
    monitor.apply (InputEvent { 500, 1, InputEventType::noteOff, 0, 60, 0 });
    CHECK (monitor.counters().outOfOrderEvents == 1);
}

TEST_CASE ("Event rate counts the last second")
{
    InputMonitor monitor;
    for (int i = 0; i < 20; ++i)
        monitor.apply (on (static_cast<std::uint8_t> (60 + i % 12), i * 100'000));
    // Events at 1.0 s .. 1.9 s fall inside (0.9 s, 1.9 s]: 10 events.
    CHECK (monitor.eventsPerSecond (1'900'000) == Catch::Approx (10.0));
    CHECK (monitor.eventsPerSecond (10'000'000) == 0.0);
}

TEST_CASE ("Device list changes are detected")
{
    const MidiDeviceInfo casio { "\\\\?\\usb#vid_07cf&pid_6803#1", "CASIO USB-MIDI" };
    const MidiDeviceInfo loop { "loopMIDI-1", "loopMIDI Port" };

    const auto change = diffDevices ({ loop }, { loop, casio });
    REQUIRE (change.added.size() == 1);
    CHECK (change.added.front() == casio);
    CHECK (change.removed.empty());

    CHECK (diffDevices ({ casio, loop }, { loop }).removed == std::vector { casio });
    CHECK (diffDevices ({ casio }, { casio }).empty());
}

TEST_CASE ("Automatic input selection")
{
    const MidiDeviceInfo casio { "id-casio", "CASIO USB-MIDI" };
    const MidiDeviceInfo casioOtherPort { "id-casio-port2", "CASIO USB-MIDI" };
    const MidiDeviceInfo loop { "id-loop", "loopMIDI Port" };

    SECTION ("nothing remembered picks the first device")
    {
        const auto choice = chooseInputDevice ({ loop, casio }, "", "");
        REQUIRE (choice.has_value());
        CHECK (choice->device == loop);
        CHECK (choice->reason == SelectionReason::onlyChoice);
    }
    SECTION ("remembered identifier wins")
    {
        const auto choice = chooseInputDevice ({ loop, casio }, "id-casio", "CASIO USB-MIDI");
        REQUIRE (choice.has_value());
        CHECK (choice->device == casio);
        CHECK (choice->reason == SelectionReason::rememberedIdentifier);
    }
    SECTION ("same name on another USB port is accepted")
    {
        const auto choice = chooseInputDevice ({ loop, casioOtherPort }, "id-casio", "CASIO USB-MIDI");
        REQUIRE (choice.has_value());
        CHECK (choice->device == casioOtherPort);
        CHECK (choice->reason == SelectionReason::rememberedName);
    }
    SECTION ("a missing remembered device is not silently replaced")
    {
        CHECK_FALSE (chooseInputDevice ({ loop }, "id-casio", "CASIO USB-MIDI").has_value());
    }
    SECTION ("no devices")
    {
        CHECK_FALSE (chooseInputDevice ({}, "", "").has_value());
    }
}

TEST_CASE ("Note names use MIDI 60 = C4")
{
    CHECK (noteName (60) == "C4");
    CHECK (noteName (61) == "C#4");
    CHECK (noteName (21) == "A0");
    CHECK (solfegeName (60) == "до");
    CHECK (solfegeName (67) == "соль");
    CHECK (solfegeName (70) == "ля#");
    CHECK (noteName (108) == "C8");
    CHECK (noteName (0) == "C-1");
    CHECK (noteName (128) == "?");
}

TEST_CASE ("SPSC queue keeps order and counts drops when full")
{
    SpscQueue<int, 4> queue;
    for (int i = 0; i < 4; ++i)
        CHECK (queue.push (i));
    CHECK_FALSE (queue.push (99));
    CHECK (queue.takeDroppedCount() == 1);
    CHECK (queue.takeDroppedCount() == 0);
    for (int i = 0; i < 4; ++i)
        CHECK (queue.pop() == std::optional<int> (i));
    CHECK_FALSE (queue.pop().has_value());
}

TEST_CASE ("SPSC queue transfers every item across threads in order")
{
    static SpscQueue<std::uint32_t, 1024> queue;
    constexpr std::uint32_t total = 200'000;

    std::thread producer ([] {
        for (std::uint32_t i = 0; i < total;)
            if (queue.push (i))
                ++i;
    });

    std::uint32_t expected = 0;
    bool ordered = true;
    while (expected < total)
        if (const auto item = queue.pop())
            ordered = ordered && (*item == expected++);

    producer.join();
    CHECK (ordered);
    CHECK_FALSE (queue.pop().has_value());
}
