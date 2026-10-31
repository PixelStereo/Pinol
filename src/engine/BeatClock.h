#pragma once
// The show's musical clock: a tempo (BPM), a bar length, a running state and a position in beats. It is what makes a
// sequence play in time: a step's waits can be counted in beats, and a step can be quantized — its GO delayed to the
// next beat or bar. It runs on its own (Internal), is set by a tap, or follows a tempo measured elsewhere (External:
// Ableton Link through a small helper program, a DAW, a controller: OSC /clock/sync). Thread-safe: the interface, the cue
// clock and the OSC thread all read and write it.
//
// The position is a function of time (a steady clock), not a counter that ticks: it is exact whenever it is asked, and
// nothing is late when the interface is busy.

#include <QJsonObject>
#include <chrono>
#include <cstdint>
#include <mutex>

class BeatClock
{
public:
    enum class Source { Internal, External };
    static constexpr double kMinBpm = 20, kMaxBpm = 400;

    BeatClock() = default;

    double bpm() const;
    void setBpm(double bpm); // kept within kMinBpm..kMaxBpm; the position goes on from where it is
    int beatsPerBar() const;
    void setBeatsPerBar(int n); // 1..16
    bool running() const;
    void start(); // from beat 0, the first beat of a bar, now
    void stop();  // the position is kept
    void resume(); // from where it stopped
    // A tap: the tempo of the last taps (the intervals of the last two seconds; a longer pause starts a new count)
    void tap() { tapAt(nowNs()); }
    void tapAt(int64_t ns); // a tap at a given time (steady clock): the tests
    // The position in beats (0 at the first beat of the bar 1), now or at `ns` (steady clock)
    double beat() const { return beatAt(nowNs()); }
    double beatAt(int64_t ns) const;
    double secondsPerBeat() const { return 60.0 / bpm(); }
    int beatInBar() const;             // 0..beatsPerBar-1
    double phase() const;              // 0..1 within the beat
    // Seconds from now until the next multiple of `grid` beats (1: beat, beatsPerBar: bar); 0 when `grid` <= 0, when the
    // clock does not run (a stopped clock never reaches it) or when now is on it (within a millisecond)
    double secondsToNext(double grid) const { return secondsToNextAt(grid, nowNs()); }
    double secondsToNextAt(double grid, int64_t ns) const;
    // A tempo and a position measured elsewhere, at this moment: the clock takes them (and its running state). While they
    // keep coming (within two seconds) the source is External
    void sync(double beat, double bpm, bool playing);
    Source source() const;

    QJsonObject toJson() const; // bpm, beats per bar (not the running state: a project opens stopped)
    void fromJson(const QJsonObject &o);

    static int64_t nowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

private:
    double beatAtLocked(int64_t ns) const;
    void rebase(int64_t ns); // the origin moved to now, at the current position (before a tempo or state change)
    mutable std::mutex m_mutex;
    double m_bpm = 120;
    int m_beatsPerBar = 4;
    bool m_running = false;
    int64_t m_originNs = 0;   // when the origin position was exact
    double m_originBeat = 0;
    int64_t m_externalNs = 0; // the last sync()
    int64_t m_lastTapNs = 0;
    double m_tapIntervals[4] = {0, 0, 0, 0};
    int m_taps = 0;
};
