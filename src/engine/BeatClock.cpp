#include "BeatClock.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr int64_t kExternalTimeout = 2'000'000'000; // ns
}

double BeatClock::beatAtLocked(int64_t ns) const
{
    if (!m_running) return m_originBeat;
    return m_originBeat + double(ns - m_originNs) * 1e-9 * m_bpm / 60.0;
}

double BeatClock::beatAt(int64_t ns) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return beatAtLocked(ns);
}

void BeatClock::rebase(int64_t ns)
{
    m_originBeat = beatAtLocked(ns);
    m_originNs = ns;
}

double BeatClock::bpm() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_bpm;
}

void BeatClock::setBpm(double bpm)
{
    if (!std::isfinite(bpm)) return;
    std::lock_guard<std::mutex> lk(m_mutex);
    rebase(nowNs());
    m_bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
}

int BeatClock::beatsPerBar() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_beatsPerBar;
}

void BeatClock::setBeatsPerBar(int n)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_beatsPerBar = std::clamp(n, 1, 16);
}

bool BeatClock::running() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_running;
}

void BeatClock::start()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_originNs = nowNs();
    m_originBeat = 0;
    m_running = true;
}

void BeatClock::stop()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    rebase(nowNs());
    m_running = false;
}

void BeatClock::resume()
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_running) return;
    m_originNs = nowNs();
    m_running = true;
}

void BeatClock::tapAt(int64_t now)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const double gap = double(now - m_lastTapNs) * 1e-9;
    m_lastTapNs = now;
    if (m_taps == 0 || gap > 2.0 || gap < 0.15) { // a first tap, or a pause: a new count (or a bounce: ignored)
        if (gap >= 0.15 || m_taps == 0) m_taps = 1;
        return;
    }
    // The intervals of the last taps, most recent first
    for (int i = 3; i > 0; --i) m_tapIntervals[i] = m_tapIntervals[i - 1];
    m_tapIntervals[0] = gap;
    m_taps = std::min(m_taps + 1, 5);
    double sum = 0;
    const int n = m_taps - 1;
    for (int i = 0; i < n; ++i) sum += m_tapIntervals[i];
    rebase(now);
    m_bpm = std::clamp(60.0 * n / sum, kMinBpm, kMaxBpm);
}

int BeatClock::beatInBar() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const double b = std::max(0.0, beatAtLocked(nowNs()));
    return int(std::floor(b)) % m_beatsPerBar;
}

double BeatClock::phase() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    const double b = beatAtLocked(nowNs());
    return b - std::floor(b);
}

double BeatClock::secondsToNextAt(double grid, int64_t ns) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!(grid > 0) || !m_running) return 0;
    const double b = beatAtLocked(ns);
    const double next = std::ceil(b / grid - 1e-9) * grid; // on the line within a hair: now
    const double beats = next - b;
    const double seconds = beats * 60.0 / m_bpm;
    return seconds < 1e-3 ? 0.0 : seconds;
}

void BeatClock::sync(double beat, double bpm, bool playing)
{
    if (!std::isfinite(beat) || !std::isfinite(bpm)) return;
    const int64_t now = nowNs();
    std::lock_guard<std::mutex> lk(m_mutex);
    m_originNs = now;
    m_originBeat = beat;
    m_bpm = std::clamp(bpm, kMinBpm, kMaxBpm);
    m_running = playing;
    m_externalNs = now;
}

BeatClock::Source BeatClock::source() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_externalNs && nowNs() - m_externalNs < kExternalTimeout ? Source::External : Source::Internal;
}

QJsonObject BeatClock::toJson() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return QJsonObject{{"bpm", m_bpm}, {"beats_per_bar", m_beatsPerBar}};
}

void BeatClock::fromJson(const QJsonObject &o)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_bpm = std::clamp(o.value("bpm").toDouble(120), kMinBpm, kMaxBpm);
    m_beatsPerBar = std::clamp(o.value("beats_per_bar").toInt(4), 1, 16);
    m_running = false;
    m_originBeat = 0;
    m_externalNs = 0;
    m_taps = 0;
}
