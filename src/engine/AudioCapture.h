#pragma once
// A live audio input as a layer's sound: a sound card's input or a microphone, read by the layer's AudioStream like a
// file's decoded samples (so the layer has its volume, its FX chain and its meters). The device is addressed by
//   input:audio:<device name>      (empty name: the system's default input)
// stored in the project, and dragged from the Media Bin (Audio > Inputs) as /NyxMap/Inputs/<the URI, percent-encoded>,
// like the video inputs. The device delivers its first channel (or a mix down), 48 kHz: it comes out on both sides.

#include <QString>
#include <QStringList>
#include <atomic>
#include <memory>
#include <vector>

namespace tcaudio {
class Device;
}

inline const QString kAudioInputPrefix = QStringLiteral("input:audio:");
inline bool isAudioInputUri(const QString &s) { return s.startsWith(kAudioInputPrefix); }
inline QString audioInputUri(const QString &device) { return kAudioInputPrefix + device; }
inline QString audioInputDevice(const QString &uri) { return uri.mid(kAudioInputPrefix.size()); }

class AudioCapture
{
public:
    AudioCapture();
    ~AudioCapture(); // the device stopped: its callback has returned

    // The input devices of the system (names), the default one first
    static QStringList devices();

    bool open(const QString &device, int rate, QString *err);
    void close();
    bool isOpen() const { return m_open.load(); }
    QString deviceName() const { return m_name; }

    // Audio callback of the layer: adds `frames` stereo samples of what came in to `out`, silence where the device
    // has not delivered yet. Never waits. When the callback is late the oldest samples are dropped, not the newest:
    // the delay stays short.
    void read(float *out, int frames);
    // What the device delivers (its own thread); tests call it directly
    void feed(const float *mono, int frames);
    // Frames waiting (diagnostics, tests)
    size_t queued() const { return m_head.load() - m_tail.load(); }
    unsigned long long overruns() const { return m_overruns.load(); }
    unsigned long long underruns() const { return m_underruns.load(); }

private:
    static constexpr size_t kRing = 1 << 15; // 0.68 s at 48 kHz: a power of two
    static constexpr size_t kMaxQueued = 4096; // above this the reader skips ahead (85 ms)
    std::unique_ptr<tcaudio::Device> m_device;
    QString m_name;
    std::atomic<bool> m_open{false};
    std::vector<float> m_ring;                 // mono samples
    std::atomic<size_t> m_head{0}, m_tail{0}; // written (device thread), read (layer's callback)
    std::atomic<unsigned long long> m_overruns{0}, m_underruns{0};
};
