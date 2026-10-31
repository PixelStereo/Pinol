// The queue between a capture device's thread and a layer's audio callback (the device itself: AudioCaptureDevice.cpp).
#include "AudioCapture.h"

#include <algorithm>
#include <cmath>

void AudioCapture::feed(const float *mono, int frames)
{
    if (!mono || frames <= 0) return;
    size_t head = m_head.load(std::memory_order_relaxed);
    for (int i = 0; i < frames; ++i) m_ring[(head + size_t(i)) & (kRing - 1)] = std::isfinite(mono[i]) ? mono[i] : 0.f;
    head += size_t(frames);
    // Only the reader moves the tail: when the writer is a full ring ahead, the reader will skip to the newest samples
    if (head - m_tail.load(std::memory_order_acquire) > kRing) m_overruns.fetch_add(1, std::memory_order_relaxed);
    m_head.store(head, std::memory_order_release);
}

void AudioCapture::read(float *out, int frames)
{
    if (!out || frames <= 0) return;
    const size_t head = m_head.load(std::memory_order_acquire);
    size_t tail = m_tail.load(std::memory_order_relaxed);
    if (head - tail > kMaxQueued) tail = head - std::min(size_t(frames) + 256, kMaxQueued); // late: drop the oldest
    const size_t available = head > tail ? head - tail : 0;
    const int n = int(std::min(available, size_t(frames)));
    for (int i = 0; i < n; ++i) {
        const float x = m_ring[(tail + size_t(i)) & (kRing - 1)];
        out[2 * i] += x;
        out[2 * i + 1] += x;
    }
    if (n < frames && m_open.load()) m_underruns.fetch_add(1, std::memory_order_relaxed);
    m_tail.store(tail + size_t(n), std::memory_order_release);
}
