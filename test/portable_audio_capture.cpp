// The portable audio test links no sound system: an input cannot be opened there (the queue itself is AudioCapture.cpp).
#include "AudioCapture.h"
#include "TimecodeIo.h"

struct tcaudio::Device::Impl {};

AudioCapture::AudioCapture() : m_ring(kRing, 0.f) {}
AudioCapture::~AudioCapture() = default;
QStringList AudioCapture::devices() { return {}; }
bool AudioCapture::open(const QString &, int, QString *err)
{
    if (err) *err = QStringLiteral("No sound system in this build");
    return false;
}
void AudioCapture::close()
{
    m_open = false;
    m_head = m_tail = 0;
}
namespace tcaudio {
Device::Device() = default;
Device::~Device() = default;
} // namespace tcaudio
