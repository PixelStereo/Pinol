// A live audio input's device (miniaudio, through the timecode's device class): opened for a layer's sound.
#include "AudioCapture.h"
#include "AudioOutput.h"
#include "TimecodeIo.h"

AudioCapture::AudioCapture() : m_ring(kRing, 0.f) {}

AudioCapture::~AudioCapture() { close(); }

QStringList AudioCapture::devices() { return AudioOutput::captureDeviceNames(); }

bool AudioCapture::open(const QString &device, int rate, QString *err)
{
    close();
    if (rate != tcaudio::kRate) { // the device is opened at the mixer's rate only
        if (err) *err = QStringLiteral("Unsupported audio input sample rate");
        return false;
    }
    auto next = std::make_unique<tcaudio::Device>();
    if (!next->openCapture(device, [this](const float *mono, int frames) { feed(mono, frames); }, err)) return false;
    m_name = next->name();
    m_device = std::move(next);
    m_open = true;
    return true;
}

void AudioCapture::close()
{
    m_open = false;
    m_device.reset(); // its callback has returned
    m_head = m_tail = 0;
}
