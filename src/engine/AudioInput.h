#pragma once
// A live audio input (a sound card's input, a microphone), analysed like the program mix (AudioAnalysis): what the
// Audio interactions that listen to it measure. Its device's first channel (or a mix down), 48 kHz.

#include "AudioAnalysis.h"
#include <QString>
#include <atomic>
#include <memory>
#include <mutex>

namespace tcaudio {
class Device;
}

class AudioInput
{
public:
    AudioInput();
    ~AudioInput(); // stopped

    // Opens the input named `device` (empty: the system's default) in place of the one open. Returns false with err
    // when it cannot be opened (nothing is listened to then).
    bool start(const QString &device, QString *err);
    void stop();
    bool isRunning() const { return m_running.load(); }
    QString deviceName() const; // the device actually open (empty when stopped)

    AudioAnalysis::Spectrum spectrum() const { return m_analysis.spectrum(); }
    AudioAnalysis::Wave wave() const { return m_analysis.wave(); }
    AudioAnalysis::Values analysis() const { return m_analysis.values(); }
    OnsetFlux onsetFlux() const { return m_analysis.onsetFlux(); }
    float peak() const { return m_peak.load(); } // 0..1+, with decay

    // What the device delivers (its own thread); tests call it directly
    void feed(const float *mono, int frames);

private:
    std::unique_ptr<tcaudio::Device> m_device;
    mutable std::mutex m_mutex; // m_device and m_name (not the audio thread)
    QString m_name;
    std::atomic<bool> m_running{false};
    AudioAnalysis m_analysis;
    std::atomic<float> m_peak{0.0f};
    float m_stereo[2 * 512]; // the analysis takes stereo: the input on both sides (audio thread)
};
