#include "AudioGenerator.h"
#include "AudioStream.h"
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <thread>

static thread_local bool watchAllocations = false;
static thread_local size_t allocations = 0;
void *operator new(std::size_t n)
{
    if (watchAllocations)
        ++allocations;
    if (void *p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void operator delete(void *p) noexcept { std::free(p); }
void operator delete(void *p, std::size_t) noexcept { std::free(p); }
static void require(bool ok, const char *message)
{
    if (!ok)
        throw std::runtime_error(message);
}
static double energy(const std::array<float, 960> &out)
{
    double e = 0;
    for (float f : out) {
        require(std::isfinite(f), "finite audio");
        e += f * f;
    }
    return e;
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    try {
        QTemporaryDir dir;
        require(dir.isValid(), "temporary directory");
        const auto path = dir.filePath("test.nyxaudio");
        auto write = [&](const QJsonObject &json) {
            QFile f(path);
            require(f.open(QIODevice::WriteOnly), "write recipe");
            f.write(QJsonDocument(json).toJson());
        };
        auto load = [&](const QJsonObject &json) {
            write(json);
            QString error;
            auto g = AudioGenerator::load(path, &error);
            if (!g)
                throw std::runtime_error(error.toStdString());
            return g;
        };
        QJsonObject fm{{"version", 1}, {"type", "fm"}, {"frequency", 440}, {"gain", .2}};
        auto synth = load(fm);
        std::array<float, 960> out{};
        watchAllocations = true;
        for (int block = 0; block < 100; ++block) {
            out.fill(0);
            synth->mix(out.data(), 480, 48000, true, 1, 1);
        }
        watchAllocations = false;
        require(allocations == 0, "FM callback must not allocate");
        require(energy(out) > 0, "FM runs indefinitely without a rendered clip");
        const auto original = out;
        out.fill(1);
        require(synth->mix(out.data(), 480, 48000, true, 1, 1) <= .201f, "source meter excludes other mixed streams");
        synth->setValue(AudioGenerator::Frequency, 1000);
        out.fill(0);
        synth->mix(out.data(), 480, 48000, true, 1, 1);
        require(out != original, "frequency edits change live audio");
        for (int k = 0; k < 30; ++k) {
            out.fill(0);
            synth->mix(out.data(), 480, 48000, false, 1, 1);
        }
        require(energy(out) == 0, "pause ramps to silence");
        out.fill(0);
        synth->mix(out.data(), 480, 48000, true, 1, 1);
        require(energy(out) > 0, "resume");
        synth->setValue(AudioGenerator::Gain, 0);
        for (int k = 0; k < 100; ++k) {
            out.fill(0);
            synth->mix(out.data(), 480, 48000, true, 1, 1);
        }
        require(energy(out) < 1e-12, "live gain reaches silence");
        for (const QString &kind : {QStringLiteral("white"), QStringLiteral("pink")}) {
            auto noise = load({{"version", 1}, {"type", kind}, {"attack", .005}, {"release", .03}});
            double sum = 0, lag = 0, previous = 0;
            watchAllocations = true;
            for (int block = 0; block < 100; ++block) {
                out.fill(0);
                noise->mix(out.data(), 480, 48000, true, 1, 1);
                for (int i = 0; i < 480; ++i) {
                    const double x = out[2 * i];
                    sum += x * x;
                    lag += x * previous;
                    previous = x;
                }
            }
            watchAllocations = false;
            require(sum > 0 && allocations == 0, "Faust noise runs without callback allocation");
            require(kind == "pink" ? lag / sum > .5 : std::abs(lag / sum) < .1,
                    "pink is correlated, white decorrelated");
            noise->setValue(AudioGenerator::Gate, 0);
            out.fill(0);
            noise->mix(out.data(), 480, 48000, true, 1, 1);
            require(energy(out) > 0, "ADSR releases instead of hard cutting");
            for (int k = 0; k < 10; ++k) {
                out.fill(0);
                noise->mix(out.data(), 480, 48000, true, 1, 1);
            }
            require(energy(out) < 1e-12, "ADSR reaches silence after release");
            noise->setValue(AudioGenerator::Gate, 1);
            out.fill(0);
            noise->mix(out.data(), 480, 48000, true, 1, 1);
            require(energy(out) > 0, "gate retriggers envelope");
        }
        auto grain = load({{"version", 1}, {"type", "granular"}, {"density", 100}, {"grainMs", 500}});
        watchAllocations = true;
        for (int k = 0; k < 200; ++k) {
            out.fill(0);
            grain->mix(out.data(), 480, 48000, true, 8, 1);
        }
        watchAllocations = false;
        require(allocations == 0 && energy(out) > 0, "bounded granular callback without allocations");
        // A real sample, loaded once; grain synthesis itself remains in the callback.
        const auto sample = dir.filePath("sample.wav");
        {
            QFile f(sample);
            require(f.open(QIODevice::WriteOnly), "sample");
            QDataStream w(&f);
            w.setByteOrder(QDataStream::LittleEndian);
            w.writeRawData("RIFF", 4);
            w << quint32(36 + 9600);
            w.writeRawData("WAVEfmt ", 8);
            w << quint32(16) << quint16(1) << quint16(1) << quint32(48000) << quint32(96000) << quint16(2)
              << quint16(16);
            w.writeRawData("data", 4);
            w << quint32(9600);
            for (int i = 0; i < 4800; ++i)
                w << qint16(10000 * std::sin(i * .1));
        }
        grain = load({{"version", 1}, {"type", "granular"}, {"source", "sample.wav"}});
        for (int k = 0; k < 20; ++k) {
            out.fill(0);
            grain->mix(out.data(), 480, 48000, true, 1, 1);
        }
        require(energy(out) > 0, "sample grains");
        auto visual = load({{"version", 1}, {"type", "video"}});
        out.fill(0);
        visual->mix(out.data(), 480, 48000, true, 1, 1);
        require(energy(out) == 0, "black image silent");
        visual->setColor(1, 1, 1);
        for (int k = 0; k < 20; ++k) {
            out.fill(0);
            visual->mix(out.data(), 480, 48000, true, 1, 1);
        }
        require(energy(out) > 0, "current bright image drives sound immediately");
        visual->setColor(0, 0, 0);
        for (int k = 0; k < 100; ++k) {
            out.fill(0);
            visual->mix(out.data(), 480, 48000, true, 1, 1);
        }
        require(energy(out) < 1e-12, "changing images affect sound without reloading");
        // Writer/reader exercise: controls are atomics; immutable samples and DSP state are never touched by UI.
        std::thread writer([&] {
            for (int i = 0; i < 10000; ++i)
                visual->setValue(AudioGenerator::Frequency, 20 + i % 7000);
        });
        for (int k = 0; k < 100; ++k) {
            out.fill(0);
            visual->mix(out.data(), 480, 48000, true, 1, 1);
            require(std::isfinite(energy(out)), "concurrent controls");
        }
        writer.join();
        write(fm);
        AudioStream stream;
        QString error;
        require(stream.open(path, 48000, &error), "stream opens realtime synth");
        require(stream.duration() == 0 && stream.generator(), "unbounded realtime transport");
        Timeline timeline;
        stream.setTransport(0, true, 1, timeline, 1, 1);
        out.fill(0);
        stream.mix(out.data(), 480, 0);
        require(energy(out) > 0, "realtime mixer integration");
        stream.close();
        require(!stream.generator(), "DSP released on close");
        AudioStream::Info info;
        require(AudioStream::probe(path, &info, &error) && info.duration == 0, "generator metadata");
        for (const auto &bad :
             {QJsonObject{{"version", 2}, {"type", "fm"}}, QJsonObject{{"version", 1}, {"type", "bad"}},
              QJsonObject{{"version", 1}, {"type", "fm"}, {"gain", "bad"}},
              QJsonObject{{"version", 1}, {"type", "video"}, {"source", "clip.mp4"}}}) {
            write(bad);
            error.clear();
            require(!AudioGenerator::load(path, &error) && !error.isEmpty(), "invalid recipe rejected");
        }
#ifdef NYXMAP_FAUST_COMPILER
        const auto program = dir.filePath("custom.dsp");
        {
            QFile f(program);
            require(f.open(QIODevice::WriteOnly), "write Faust program");
            f.write("process = hslider(\"Left\",0.2,0,1,0.01), hslider(\"Right\",0.4,0,1,0.01);");
        }
        auto custom = AudioGenerator::load(program, &error);
        require(bool(custom), error.toStdString().c_str());
        require(custom->parameterCount() == 2, "Faust widgets discovered");
        watchAllocations = true;
        for (int k = 0; k < 20; ++k) {
            out.fill(0);
            custom->mix(out.data(), 480, 48000, true, 1, 1);
        }
        watchAllocations = false;
        require(allocations == 0 && std::abs(out[958] - .2) < .001 && std::abs(out[959] - .4) < .001,
                "Faust stereo compute without allocations");
        custom->setValue(0, .8);
        out.fill(0);
        custom->mix(out.data(), 480, 48000, true, 1, 1);
        require(std::abs(out[958] - .8) < .001, "arbitrary Faust control edits are live");
        {
            QFile f(program);
            f.open(QIODevice::WriteOnly);
            f.write("process = broken(; ");
        }
        require(!AudioGenerator::load(program, &error) && !error.isEmpty(), "Faust compiler errors reported");
#endif
        std::cout
            << "Realtime audio: FM, granular, live control, visual input, no callback allocations, transport passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
