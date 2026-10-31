#pragma once
#include <array>
#include <atomic>

// A value one producer publishes and readers copy, never waiting for one another (see publish())
template <class T> class Published {
public:
    T read() const {
        // A pin protects the whole snapshot. The writer can never recycle a pinned slot. Acquisition is bounded for
        // render-thread readers too: extreme contention returns a cleared value.
        for (int attempt = 0; attempt < 3; ++attempt) {
            auto &slot = m_slots[m_published.load(std::memory_order_acquire)];
            const unsigned state = slot.users.fetch_add(1, std::memory_order_acquire);
            if (!(state & writing)) {
                T result = slot.value;
                slot.users.fetch_sub(1, std::memory_order_release);
                return result;
            }
            slot.users.fetch_sub(1, std::memory_order_release);
        }
        return {};
    }
    // Exactly one producer (process/reset, as for the FFT ring itself).
    // Multiple readers may pin the same slot. A reader arriving after writer
    // ownership only touches the atomic counter, never the snapshot. Subtract
    // the writer bit on release instead of storing zero: failed reader pins
    // may still be in flight and must retain their counts until they withdraw.
    void publish(const T &value) {
        const unsigned current = m_published.load(std::memory_order_relaxed);
        for (unsigned offset = 1; offset < m_slots.size(); ++offset) {
            const unsigned next = (current + offset) % m_slots.size();
            auto &slot = m_slots[next];
            unsigned empty = 0;
            if (!slot.users.compare_exchange_strong(empty, writing,
                    std::memory_order_acquire, std::memory_order_relaxed)) continue;
            slot.value = value;
            slot.users.fetch_sub(writing, std::memory_order_release);
            m_published.store(next, std::memory_order_release);
            return;
        }
        // Slow readers may retain both inactive snapshots. Keep the previous
        // complete publication; the audio callback must never wait for them.
    }
private:
    static constexpr unsigned writing = 1u << 31;
    struct SnapshotSlot {
        T value;
        mutable std::atomic<unsigned> users{0};
    };
    static_assert(std::atomic<unsigned>::is_always_lock_free,
                  "Audio publication requires lock-free counters");
    std::array<SnapshotSlot, 3> m_slots{};
    std::atomic<unsigned> m_published{0};
};
