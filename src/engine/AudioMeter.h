#pragma once
// Stereo peak meter with decay: one writer (an audio callback), any number of readers (the interface), no lock.
// Every point of an audio path that is worth watching (a layer's source, each of its FX, its output) owns one.

#include <algorithm>
#include <atomic>
#include <cmath>

class AudioMeter
{
public:
    static constexpr float kDecaySeconds = 0.3f; // time constant of the fall

    // Audio thread: the peaks of `frames` stereo interleaved samples, falling from the previous ones
    void update(const float *stereo, int frames, int rate)
    {
        if (!stereo || frames <= 0 || rate <= 0) return;
        const float fall = std::exp(-float(frames) / (float(rate) * kDecaySeconds));
        float peak[2] = {m_peak[0].load(std::memory_order_relaxed) * fall, m_peak[1].load(std::memory_order_relaxed) * fall};
        for (int i = 0; i < frames; ++i)
            for (int c = 0; c < 2; ++c) {
                const float x = std::abs(stereo[2 * i + c]);
                if (std::isfinite(x)) peak[c] = std::max(peak[c], x);
            }
        for (int c = 0; c < 2; ++c) m_peak[c].store(peak[c], std::memory_order_relaxed);
    }
    // Audio thread: a silent block (a bypassed or stopped path): the meter keeps falling
    void silence(int frames, int rate)
    {
        if (frames <= 0 || rate <= 0) return;
        const float fall = std::exp(-float(frames) / (float(rate) * kDecaySeconds));
        for (auto &p : m_peak) p.store(p.load(std::memory_order_relaxed) * fall, std::memory_order_relaxed);
    }
    void reset()
    {
        for (auto &p : m_peak) p.store(0, std::memory_order_relaxed);
    }
    float peak(int channel) const { return m_peak[channel & 1].load(std::memory_order_relaxed); } // linear, 0..1+
    float peak() const { return std::max(peak(0), peak(1)); }
    // dBFS of a linear level, floored at `floorDb` (-inf would not draw)
    static float toDb(float linear, float floorDb = -60.f)
    {
        return linear > 1e-6f ? std::max(floorDb, 20.f * std::log10(linear)) : floorDb;
    }

private:
    std::atomic<float> m_peak[2] = {0.f, 0.f};
};
