#pragma once
// Hit detection (onsets): the start of a sound event, a kick or a snare, as a clean trigger with a velocity.
//
// Two halves, so that each interaction can listen to its own frequency region without the audio thread knowing it:
//  * OnsetAnalysis, in the audio thread: a short spectrum (1024 samples, a hop of 256: 5 ms) of the sound, its power in 24
//    logarithmic bands, compressed to dB-like units; per band the *flux* of each hop, the rise since the one before
//    (half-wave rectified: a fall is not an attack). It publishes the flux of the last hops, no more.
//  * OnsetDetector, in the interaction (one per interaction): sums the flux of the bands of its region, learns its own
//    mean and spread over about a second and a half, and picks the peaks that stand out (an adaptive threshold set by its
//    sensitivity), no closer than a minimum gap, with a velocity relative to the hardest hits it has heard.
// The result is a pulse of a few milliseconds' hold at the hit's velocity, 0 otherwise: the trigger that an Envelope filter
// (Interactions.h) turns into a flash, a pulse, an impact.
//
// Spectral flux is the classical onset function (Masri 1996, Dixon 2006, Bello et al. 2005). This is a small
// implementation of it, not a binding: no dependency.

#include "Published.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <complex>
#include <cstdint>

namespace onset {
inline constexpr int kWindow = 1024, kHop = 256, kBins = kWindow / 2 + 1, kBands = 24, kHistory = 32;
inline constexpr float kLowHz = 30, kHighHz = 18000;
// The edges of band b (0..kBands), logarithmic
inline float bandEdge(int b) { return kLowHz * std::pow(kHighHz / kLowHz, float(b) / kBands); }
// The overlap of [lo, hi] Hz with each band, 0..1 (the part of the band the region covers, on the log scale)
inline std::array<float, kBands> selection(float lo, float hi)
{
    std::array<float, kBands> w{};
    if (!std::isfinite(lo) || !std::isfinite(hi) || hi <= lo) return w;
    for (int b = 0; b < kBands; ++b) {
        const float a = bandEdge(b), z = bandEdge(b + 1);
        const float overlap = std::min(hi, z) - std::max(lo, a);
        if (overlap <= 0) continue;
        // On the log scale: the share of the band's width
        w[size_t(b)] = std::clamp(std::log(std::min(hi, z) / std::max(lo, a)) / std::log(z / a), 0.f, 1.f);
    }
    return w;
}
} // namespace onset

// The flux of the last hops, per band. flux[seq % kHistory] is hop `seq`; `newest` is the last one made (0: none)
struct OnsetFlux {
    uint64_t newest = 0;
    int rate = 48000;
    std::array<std::array<float, onset::kBands>, onset::kHistory> flux{};
    double hopSeconds() const { return rate > 0 ? double(onset::kHop) / rate : 0.0; }
};

class OnsetAnalysis
{
public:
    OnsetAnalysis()
    {
        constexpr double pi = 3.14159265358979323846;
        for (int n = 0; n < onset::kWindow; ++n) m_window[size_t(n)] = float(.5 - .5 * std::cos(2 * pi * n / onset::kWindow));
        for (int n = 0; n < onset::kWindow / 2; ++n) m_twiddle[size_t(n)] = std::polar(1.f, float(-2 * pi * n / onset::kWindow));
    }
    OnsetFlux read() const { return m_published.read(); }
    void reset()
    {
        for (auto &c : m_ring) c.fill(0);
        m_write = m_filled = m_hop = 0;
        m_previous.fill(0);
        m_havePrevious = false;
        m_seq = 0;
        m_history = OnsetFlux{};
        m_history.rate = m_rate;
        m_published.publish(m_history);
    }
    // Audio thread: no allocation, no lock
    void process(const float *stereo, int frames, int rate)
    {
        if (!stereo || frames <= 0 || rate <= 0) return;
        if (m_rate != rate) {
            m_rate = rate;
            reset();
        }
        for (int i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                const float x = stereo[2 * i + c];
                m_ring[size_t(c)][size_t(m_write)] = std::isfinite(x) ? x : 0.f;
            }
            m_write = (m_write + 1) % onset::kWindow;
            m_filled = std::min(m_filled + 1, onset::kWindow);
            if (++m_hop >= onset::kHop && m_filled == onset::kWindow) {
                m_hop = 0;
                hop();
            }
        }
    }

private:
    void fft(std::array<std::complex<float>, onset::kWindow> &x) const
    {
        constexpr int n = onset::kWindow;
        for (int i = 1, j = 0; i < n; ++i) {
            int bit = n >> 1;
            for (; j & bit; bit >>= 1) j ^= bit;
            j ^= bit;
            if (i < j) std::swap(x[size_t(i)], x[size_t(j)]);
        }
        for (int length = 2; length <= n; length <<= 1)
            for (int base = 0; base < n; base += length)
                for (int j = 0; j < length / 2; ++j) {
                    const auto a = x[size_t(base + j)], b = x[size_t(base + j + length / 2)] * m_twiddle[size_t(j * n / length)];
                    x[size_t(base + j)] = a + b;
                    x[size_t(base + j + length / 2)] = a - b;
                }
    }
    void hop()
    {
        std::array<float, onset::kBins> power{};
        for (int c = 0; c < 2; ++c) {
            for (int i = 0; i < onset::kWindow; ++i)
                m_fft[size_t(i)] = {m_ring[size_t(c)][size_t((m_write + i) % onset::kWindow)] * m_window[size_t(i)], 0.f};
            fft(m_fft);
            for (int k = 1; k < onset::kBins; ++k) power[size_t(k)] += std::norm(m_fft[size_t(k)]) / (onset::kWindow * onset::kWindow * .375f);
        }
        // The band's amplitude (RMS of the two channels), the bins weighted by their share of the band (a narrow low band
        // gets the fractional share of a bin: no jump as the sound moves between two)
        const float step = float(m_rate) / onset::kWindow;
        std::array<float, onset::kBands> level{}, flux{};
        for (int b = 0; b < onset::kBands; ++b) {
            const float lo = onset::bandEdge(b), hi = onset::bandEdge(b + 1);
            const int first = std::clamp(int(std::floor(lo / step)) - 1, 1, onset::kBins - 1);
            const int last = std::clamp(int(std::ceil(hi / step)) + 1, 1, onset::kBins - 1);
            double sum = 0, weights = 0;
            for (int k = first; k <= last; ++k) {
                const float left = (float(k) - .5f) * step, right = (float(k) + .5f) * step;
                const float w = std::clamp((std::min(hi, right) - std::max(lo, left)) / step, 0.f, 1.f);
                sum += power[size_t(k)] * w;
                weights += w;
            }
            // Mean power of what the band covers, as an amplitude, in a log scale that turns -60 dBFS into ~0.7 and
            // full scale into ~6.9: a hit is a few units, the noise of a quiet room a tenth
            const float amplitude = std::sqrt(float(weights > 0 ? sum / weights : 0.0) / 2.f);
            level[size_t(b)] = std::log1p(1000.f * amplitude);
        }
        for (int b = 0; b < onset::kBands; ++b) flux[size_t(b)] = m_havePrevious ? std::max(0.f, level[size_t(b)] - m_previous[size_t(b)]) : 0.f;
        m_previous = level;
        m_havePrevious = true;
        ++m_seq;
        m_history.newest = m_seq;
        m_history.rate = m_rate;
        m_history.flux[size_t(m_seq % onset::kHistory)] = flux;
        m_published.publish(m_history);
    }
    std::array<float, onset::kWindow> m_window{};
    std::array<std::array<float, onset::kWindow>, 2> m_ring{};
    std::array<std::complex<float>, onset::kWindow> m_fft{};
    std::array<std::complex<float>, onset::kWindow / 2> m_twiddle{};
    std::array<float, onset::kBands> m_previous{};
    bool m_havePrevious = false;
    int m_rate = 48000, m_write = 0, m_filled = 0, m_hop = 0;
    uint64_t m_seq = 0;
    OnsetFlux m_history;
    Published<OnsetFlux> m_published;
};

// In the interaction (one per interaction, and per XY axis): from the flux of the bands of a region to a trigger
class OnsetDetector
{
public:
    // The pulse, now: the velocity (0..1) for `hold` seconds after a hit, 0 otherwise. `sensitivity` 0..1: how far above
    // its usual a rise has to stand out (1 hears the faintest hits, 0 only the hard ones); `gap`: the shortest time between
    // two hits, seconds; `dt`: the time since the last call
    double step(const OnsetFlux &flux, double lowHz, double highHz, double sensitivity, double gap, double dt)
    {
        if (flux.newest == 0 || flux.rate <= 0) return m_out = 0;
        if (lowHz != m_low || highHz != m_high) {
            m_low = lowHz;
            m_high = highHz;
            m_weights = onset::selection(float(lowHz), float(highHz));
            m_weightSum = 0;
            for (float w : m_weights) m_weightSum += w;
        }
        if (flux.newest < m_seen) m_seen = 0, m_started = false; // an analysis restarted
        if (!m_started) { // starts at the newest hop: what was before the interaction was listening is not its business
            m_started = true;
            m_seen = flux.newest;
            m_hops = 0;
            return m_out = 0;
        }
        const double hopSeconds = flux.hopSeconds();
        const uint64_t oldest = flux.newest >= onset::kHistory ? flux.newest - onset::kHistory + 1 : 1;
        for (uint64_t s = std::max(m_seen + 1, oldest); s <= flux.newest; ++s) {
            double f = 0;
            const auto &row = flux.flux[size_t(s % onset::kHistory)];
            for (int b = 0; b < onset::kBands; ++b) f += double(m_weights[size_t(b)]) * row[size_t(b)];
            hop(f, hopSeconds, std::clamp(sensitivity, 0.0, 1.0), std::max(0.04, gap));
        }
        m_seen = flux.newest;
        m_hold -= dt;
        if (m_hold <= 0) m_out = 0;
        return m_out;
    }
    void reset() { *this = OnsetDetector(); }

private:
    static constexpr double kHoldSeconds = 0.04, kStatsSeconds = 1.5, kPeakSeconds = 6.0, kWarmupSeconds = 0.4;
    void hop(double f, double hopSeconds, double sensitivity, double gap)
    {
        ++m_hops;
        m_since += hopSeconds;
        const double k = 3.4 - 2.6 * sensitivity; // how many spreads above the mean
        const double floor = 0.15 * m_weightSum;  // the smallest rise that can be a hit: a fraction of a log unit a band
        const double threshold = m_mean + k * std::sqrt(std::max(0.0, m_var)) + floor;
        // The peak is the hop before this one: a rise that the next hop does not beat
        if (m_hops > kWarmupSeconds / hopSeconds && m_f1 > m_thr1 && m_f1 >= m_f2 && m_f1 > f && m_since >= gap) {
            m_peakRef = std::max(m_peakRef, m_f1);
            // The log scale turns a ratio of amplitudes into a difference: the velocity is the amplitude of this hit relative to
            // the hardest it has heard (per band, so that a wide region and a narrow one compare alike)
            m_out = std::clamp(std::exp((m_f1 - m_peakRef) / std::max(m_weightSum, 1.0)), 0.1, 1.0);
            m_hold = kHoldSeconds;
            m_since = 0;
        }
        // What it learns: its own mean and spread, not pulled up by the hits themselves (clipped at the threshold)
        const double a = 1.0 - std::exp(-hopSeconds / kStatsSeconds);
        const double learned = std::min(f, threshold);
        const double d = learned - m_mean;
        m_mean += a * d;
        m_var += a * (d * d - m_var);
        m_peakRef *= std::exp(-hopSeconds / kPeakSeconds);
        m_f2 = m_f1;
        m_f1 = f;
        m_thr1 = threshold;
    }
    double m_low = -1, m_high = -1;
    std::array<float, onset::kBands> m_weights{};
    double m_weightSum = 0;
    bool m_started = false;
    uint64_t m_seen = 0, m_hops = 0;
    double m_mean = 0, m_var = 0, m_peakRef = 0, m_since = 1e9, m_hold = 0, m_out = 0;
    double m_f1 = 0, m_f2 = 0, m_thr1 = 1e9;
};
