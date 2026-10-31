// The levels of the sound and the live audio input: what the VU meters of the Audio FX tab read, and the queue that
// brings a sound card's input into a layer.
#include "AudioCapture.h"
#include "AudioEffects.h"
#include "AudioMeter.h"
#include "AudioStream.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

static void require(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
static std::vector<float> ramp(int frames, float value)
{
    return std::vector<float>(size_t(frames), value);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        // --- AudioMeter: peaks per channel, falling when the signal stops
        {
            AudioMeter meter;
            std::array<float, 2 * 256> block{};
            for (int i = 0; i < 256; ++i) {
                block[2 * i] = .5f;
                block[2 * i + 1] = i == 10 ? -.25f : 0.f;
            }
            meter.update(block.data(), 256, 48000);
            require(std::abs(meter.peak(0) - .5f) < 1e-6 && std::abs(meter.peak(1) - .25f) < 1e-6, "left and right peaks are separate");
            require(std::abs(meter.peak() - .5f) < 1e-6, "the larger of the two");
            const float held = meter.peak(0);
            std::array<float, 2 * 256> quiet{};
            for (int i = 0; i < 100; ++i)
                meter.update(quiet.data(), 256, 48000);
            require(meter.peak(0) < held * .2f && meter.peak(0) > 0, "the peak falls but does not jump to zero");
            std::array<float, 2> bad{{NAN, INFINITY}};
            meter.update(bad.data(), 1, 48000);
            require(std::isfinite(meter.peak(0)) && std::isfinite(meter.peak(1)), "a non-finite sample never reaches the meter");
            meter.silence(3 * 48000, 48000);
            require(meter.peak() < 1e-3f, "a few silent seconds empty it");
            require(AudioMeter::toDb(1.f) == 0.f && AudioMeter::toDb(0.f) == -60.f && std::abs(AudioMeter::toDb(.5f) + 6.02f) < .01f,
                    "dBFS conversion, floored");
        }

        // --- AudioCapture: the queue between the device's thread and the layer's callback (no device needed)
        {
            AudioCapture capture;
            std::array<float, 2 * 64> out{};
            capture.read(out.data(), 64);
            for (float x : out)
                require(x == 0.f, "an empty input is silence");
            const auto first = ramp(48, .5f);
            capture.feed(first.data(), 48);
            require(capture.queued() == 48, "frames wait to be read");
            out.fill(0);
            capture.read(out.data(), 64);
            require(out[0] == .5f && out[1] == .5f && out[2 * 47] == .5f && out[2 * 48] == 0.f, "mono comes out on both sides, then silence");
            require(capture.queued() == 0, "what was read is gone");
            // Late reader: the oldest is dropped, the newest kept, the delay stays short
            std::vector<float> long_(20000);
            for (size_t i = 0; i < long_.size(); ++i)
                long_[i] = float(i) / 20000.f;
            capture.feed(long_.data(), int(long_.size()));
            out.fill(0);
            capture.read(out.data(), 64);
            require(out[0] > .8f, "a late reader skips to the recent samples");
            require(capture.queued() < 8192, "the queue is bounded");
            // A device that delivers non-finite samples
            const std::array<float, 4> bad{{NAN, INFINITY, -INFINITY, .1f}};
            capture.feed(bad.data(), 4);
            out.fill(0);
            capture.read(out.data(), 4);
            for (int i = 0; i < 8; ++i)
                require(std::isfinite(out[i]), "non-finite input samples are silenced");
            // Overflowing the ring is counted, not crashed
            std::vector<float> huge(100000, .1f);
            capture.feed(huge.data(), int(huge.size()));
            require(capture.overruns() > 0, "an overrun is counted");
            require(isAudioInputUri(audioInputUri("Built-in Microphone")) && audioInputDevice(audioInputUri("A:B")) == "A:B" &&
                        !isAudioInputUri("/a/file.wav"),
                    "input URIs");
        }

        // --- A layer's meters: the source's, each FX's, the output's (after the fader)
        {
            QTemporaryDir dir;
            const auto recipe = dir.filePath("noise.nyxaudio");
            {
                QFile f(recipe);
                require(f.open(QIODevice::WriteOnly), "recipe fixture");
                f.write("{\"version\":1,\"type\":\"white\",\"gain\":0.5,\"attack\":0.001,\"decay\":0.1,\"sustain\":1,\"release\":0.1,\"gate\":1}");
            }
            QString error;
            AudioStream stream;
            require(stream.open(recipe, 48000, &error), qPrintable(error));
            auto chain = std::make_shared<AudioEffectChain>(48000);
            require(chain->replace(QJsonArray{QJsonObject{{"kind", "lowpass"}}, QJsonObject{{"kind", "gate"}}}, &error), qPrintable(error));
            stream.setEffects(chain);
            Timeline timeline;
            stream.setTransport(0, true, 1, timeline, 1, .5f);
            std::array<float, 2 * 512> audio{};
            for (int i = 0; i < 200; ++i) {
                audio.fill(0);
                stream.mix(audio.data(), 512, 0);
            }
            const auto effects = chain->effects();
            require(stream.sourceMeter().peak() > .01f, "the source meter sees the generator");
            require(effects.size() == 2 && effects[0]->meter().peak() > 0.f, "an FX has a meter of its own");
            require(stream.outputMeter().peak() > 0.f && stream.outputMeter().peak() < stream.sourceMeter().peak() + 1e-3f,
                    "the output is after the fader (0.5)");
            // The master switch: off, the chain does not run and its meters fall; the signal goes through
            chain->setEnabled(false);
            require(!chain->enabled(), "master switch");
            for (int i = 0; i < 400; ++i) {
                audio.fill(0);
                stream.mix(audio.data(), 512, 0);
            }
            require(effects[0]->meter().peak() < 1e-3f, "a chain that is off lets its meters fall");
            require(stream.outputMeter().peak() > 0.f, "the sound still comes out");
            chain->setEnabled(true);
        }
        std::cout << "Audio levels: meters, input queue, per-FX meters, master switch passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
