#include "AudioEffects.h"
#include "AudioStream.h"
#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

static thread_local bool watch = false;
static thread_local size_t allocations = 0;
void *operator new(std::size_t n)
{
    if (watch)
        ++allocations;
    if (auto *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
static void require(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
static void set(AudioEffect &fx, const QString &name, double value)
{
    const auto &params = fx.parameters();
    for (size_t i = 0; i < params.size(); ++i)
        if (params[i].label == name) {
            fx.setValue(int(i), value);
            return;
        }
    throw std::runtime_error("parameter not found");
}
static double tone(const QString &kind, double frequency, double cutoff)
{
    QString error;
    auto fx = AudioEffect::load({{"kind", kind}, {"wet", 1}}, 48000, &error);
    require(bool(fx), qPrintable(error));
    set(*fx, "cutoff", cutoff);
    double sum = 0;
    for (int block = 0; block < 100; ++block) {
        std::array<float, 256> audio{};
        for (int i = 0; i < 128; ++i)
            audio[i * 2] = audio[i * 2 + 1] =
                float(.2 * std::sin(2 * 3.141592653589793 * frequency * (block * 128 + i) / 48000));
        fx->process(audio.data(), 128);
        if (block > 50)
            for (float x : audio)
                sum += x * x;
    }
    return sum;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        QString error;
        for (const auto &preset : AudioEffect::presets) {
            auto fx = AudioEffect::load({{"kind", preset.key}}, 48000, &error);
            require(bool(fx), qPrintable(error));
            watch = true;
            for (int block = 0; block < 100; ++block) {
                std::array<float, 256> audio{};
                audio[0] = block == 0 ? .5f : 0.f;
                fx->process(audio.data(), 128);
                for (float value : audio)
                    require(std::isfinite(value) && std::abs(value) <= 16, "bounded, finite FX output");
            }
            watch = false;
            require(allocations == 0, "FX callback must not allocate");
        }
        require(tone("lowpass", 200, 800) > tone("lowpass", 8000, 800) * 100, "low-pass rejects high frequencies");
        require(tone("highpass", 8000, 2000) > tone("highpass", 100, 2000) * 20, "high-pass rejects low frequencies");
        auto width = AudioEffect::load({{"kind", "width"}}, 48000, &error);
        require(bool(width), "width effect");
        set(*width, "width", 0);
        std::array<float, 256> audio{};
        audio[0] = 1;
        audio[1] = -1;
        width->process(audio.data(), 128);
        require(std::abs(audio[0]) + std::abs(audio[1]) < .01, "width collapses side-only signal");
        auto dry = AudioEffect::load({{"kind", "distortion"}, {"wet", 0}}, 48000, &error);
        require(bool(dry), "dry effect");
        audio.fill(.123f);
        const auto input = audio;
        dry->process(audio.data(), 128);
        require(audio == input, "zero wet is exact dry passthrough");
        dry->setWet(1);
        for (int i = 0; i < 50; ++i) {
            audio.fill(.8f);
            dry->process(audio.data(), 128);
        }
        dry->setEnabled(false);
        for (int i = 0; i < 100; ++i) {
            audio.fill(.123f);
            dry->process(audio.data(), 128);
        }
        require(std::abs(audio[0] - .123f) < 1e-6, "bypass returns to dry");
        auto delay = AudioEffect::load({{"kind", "delay"}, {"wet", 1}}, 48000, &error);
        require(bool(delay), "delay");
        set(*delay, "time_ms", 20);
        double tail = 0;
        for (int i = 0; i < 20; ++i) {
            audio.fill(0);
            delay->process(audio.data(), 128);
        }
        for (int block = 0; block < 100; ++block) {
            audio.fill(0);
            if (block == 0)
                audio[0] = 1;
            delay->process(audio.data(), 128);
            if (block > 10)
                for (float x : audio)
                    tail += x * x;
        }
        require(tail > 0, "delay tail continues after input stops");
        AudioEffectChain chain;
        require(chain.replace(
                    {QJsonObject{{"id", "a"}, {"kind", "lowpass"}}, QJsonObject{{"id", "b"}, {"kind", "distortion"}}},
                    &error),
                "create chain");
        const auto saved = chain.toJson();
        auto ordered = chain.effects();
        require(chain.replace({saved[1], saved[0]}, &error), "reorder chain");
        require(chain.effects()[1].get() == ordered[0].get(), "reordering retains DSP state");
        require(!chain.replace({QJsonObject{{"id", "x"}, {"kind", "not-an-effect"}}}, &error), "reject unknown effect");
        require(chain.toJson()[0].toObject().value("id") == "b", "failed edit preserves current chain");
        require(!chain.replace({saved[0], saved[0]}, &error), "reject duplicate ids");
        require(chain.replace({QJsonObject{{"id", "missing"},
                                           {"kind", "faust"},
                                           {"path", "missing.dsp"},
                                           {"values", QJsonObject{{"p0", .7}}}}},
                              &error, true),
                "preserve unavailable effect on project restore");
        require(!chain.effects()[0]->error().isEmpty() &&
                    chain.toJson()[0].toObject().value("values").toObject().value("p0").toDouble() == .7,
                "broken FX keep their controls");
        audio.fill(.2f);
        chain.process(audio.data(), 128, 48000);
        require(audio[0] == .2f, "unavailable FX pass audio through");
        require(chain.replace(saved, &error), "restore chain");
        std::atomic<bool> done{false};
        std::atomic<bool> finite{true};
        std::thread reader([&] {
            watch = true;
            while (!done) {
                std::array<float, 256> buffer{};
                buffer[0] = .1;
                chain.process(buffer.data(), 128, 48000);
                for (float v : buffer)
                    if (!std::isfinite(v))
                        finite = false;
            }
            watch = false;
            if (allocations != 0)
                finite = false;
        });
        for (int i = 0; i < 50; ++i)
            require(chain.replace(i % 2 ? QJsonArray{saved[0], saved[1]} : QJsonArray{saved[1], saved[0]}, &error),
                    "live chain edits");
        done = true;
        reader.join();
        require(finite, "concurrent publication keeps DSP alive without callback allocations");
        QTemporaryDir dir;
        const auto preset = dir.filePath("synth.nyxaudio");
        {
            QFile f(preset);
            require(f.open(QIODevice::WriteOnly), "synth fixture");
            f.write("{\"version\":1,\"type\":\"fm\"}");
        }
        auto fx = std::make_shared<AudioEffectChain>();
        require(fx->replace({QJsonObject{{"kind", "delay"}, {"wet", 1}}}, &error), "stream chain");
        AudioStream stream;
        require(stream.open(preset, 48000, &error), "synth stream");
        stream.setEffects(fx);
        Timeline timeline;
        stream.setTransport(0, true, 1, timeline, 1, 1);
        for (int i = 0; i < 100; ++i) {
            audio.fill(0);
            stream.mix(audio.data(), 128, 0);
        }
        stream.setTransport(0, true, 1, timeline, 1, 0);
        double muted = 0;
        for (int i = 0; i < 100; ++i) {
            audio.fill(0);
            stream.mix(audio.data(), 128, 0);
            if (i > 20)
                for (float v : audio)
                    muted += v * v;
        }
        require(muted == 0, "layer mute suppresses effect tails");
#ifdef NYXMAP_FAUST_COMPILER
        const auto program = dir.filePath("fx.dsp");
        {
            QFile f(program);
            f.open(QIODevice::WriteOnly);
            f.write("g=hslider(\"gain\",0.5,0,2,.01);process=*(g),*(g);");
        }
        auto custom = AudioEffect::load({{"kind", "faust"}, {"path", program}}, 48000, &error);
        require(bool(custom), qPrintable(error));
        audio.fill(.4f);
        custom->process(audio.data(), 128);
        require(std::abs(audio[0] - .2f) < 1e-5, "custom Faust stereo FX");
#endif
        std::cout << "Audio FX: 13 presets, spectral checks, dry/wet, bypass, tails, publication, stream mute passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
