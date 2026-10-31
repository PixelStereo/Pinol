#include "AudioGenerator.h"
#include "FaustInterpreter.h"
#include "ParameterKey.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>

#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "generated/NyxFaustSynth.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}
namespace {
constexpr int sampleRate = 48000;
struct Decoder {
    AVFormatContext *format = nullptr;
    AVCodecContext *codec = nullptr;
    AVFrame *frame = av_frame_alloc();
    AVPacket *packet = av_packet_alloc();
    SwrContext *resample = nullptr;
    int stream = -1;
    ~Decoder()
    {
        swr_free(&resample);
        av_packet_free(&packet);
        av_frame_free(&frame);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }
    bool open(const QString &path, AVMediaType type)
    {
        if (!frame || !packet ||
            avformat_open_input(&format, QFile::encodeName(path).constData(), nullptr, nullptr) < 0 ||
            avformat_find_stream_info(format, nullptr) < 0)
            return false;
        stream = av_find_best_stream(format, type, -1, -1, nullptr, 0);
        if (stream < 0)
            return false;
        auto *par = format->streams[stream]->codecpar;
        auto *decoder = avcodec_find_decoder(par->codec_id);
        codec = avcodec_alloc_context3(decoder);
        return codec && avcodec_parameters_to_context(codec, par) >= 0 && avcodec_open2(codec, decoder, nullptr) >= 0;
    }
    template <class F> bool read(F consume)
    {
        bool done = false;
        auto drain = [&] {
            int result;
            while ((result = avcodec_receive_frame(codec, frame)) >= 0) {
                done = !consume(frame);
                av_frame_unref(frame);
                if (done)
                    return true;
            }
            return result == AVERROR(EAGAIN) || result == AVERROR_EOF;
        };
        int result;
        while (!done && (result = av_read_frame(format, packet)) >= 0) {
            if (packet->stream_index == stream) {
                const int sent = avcodec_send_packet(codec, packet);
                if (sent < 0 || !drain()) {
                    av_packet_unref(packet);
                    return false;
                }
            }
            av_packet_unref(packet);
        }
        if (done)
            return true;
        if (result != AVERROR_EOF)
            return false;
        return avcodec_send_packet(codec, nullptr) >= 0 && drain();
    }
};
} // namespace
const std::array<AudioGenerator::Parameter, AudioGenerator::Count> AudioGenerator::parameters = {
    {{"frequency", "Frequency (Hz)", 20, 8000, 220},
     {"ratio", "FM ratio", .01, 16, 2},
     {"index", "FM depth", 0, 20, 2},
     {"gain", "Synth gain", 0, 1, .2},
     {"grainMs", "Grain size (ms)", 5, 500, 80},
     {"density", "Grains / second", 1, 100, 15},
     {"pitch", "Grain pitch", .125, 8, 1},
     {"position", "Sample position", 0, 1, .5},
     {"attack", "Attack (s)", .001, 10, .01},
     {"decay", "Decay (s)", .001, 10, .1},
     {"sustain", "Sustain", 0, 1, .8},
     {"release", "Release (s)", .001, 10, .1},
     {"gate", "Envelope gate", 0, 1, 1}}};
struct AudioGenerator::Backend : UI {
    std::unique_ptr<dsp> processor;
    AudioGenerator *owner = nullptr;
    std::array<float *, 128> zones{};
    int externalGate = -1;
    bool custom = false, unsupported = false;

    float *kind = nullptr, *speed = nullptr, *hasSample = nullptr;
    int rate = 48000;
    Backend() = default;
    void collect(const char *label, float *zone, float initial, float low, float high)
    {
        if (!custom) {
            add(label, zone);
            return;
        }
        if (owner->m_parameters.size() >= 128 || !std::isfinite(initial) || !std::isfinite(low) ||
            !std::isfinite(high) || low > high) {
            unsupported = true;
            return;
        }
        const int i = int(owner->m_parameters.size());
        QStringList taken;
        for (int j = 0; j < i; ++j) taken << QString::fromUtf8(owner->m_parameters[size_t(j)].key);
        owner->m_names.push_back(faustControlKey(QString::fromUtf8(label), taken, i).toStdString());
        const char *key = owner->m_names.back().c_str();
        owner->m_names.push_back(label);
        owner->m_parameters.push_back({key, owner->m_names.back().c_str(), low, high, initial});
        owner->m_values[i] = initial;
        zones[i] = zone;
        if (QString::fromUtf8(label) == "gate")
            externalGate = i;
    }
    void add(const char *label, float *zone)
    {
        for (int i = 0; i < Count; ++i)
            if (QString::fromUtf8(label) == QLatin1String(parameters[i].key))
                zones[i] = zone;
        if (QString::fromUtf8(label) == "_kind")
            kind = zone;
        if (QString::fromUtf8(label) == "_speed")
            speed = zone;
        if (QString::fromUtf8(label) == "_hasSample")
            hasSample = zone;
    }
    void openTabBox(const char *) override {}
    void openHorizontalBox(const char *) override {}
    void openVerticalBox(const char *) override {}
    void closeBox() override {}
    void addButton(const char *l, float *z) override { collect(l, z, 0, 0, 1); }
    void addCheckButton(const char *l, float *z) override { collect(l, z, 0, 0, 1); }
    void addVerticalSlider(const char *l, float *z, float initial, float low, float high, float) override
    {
        collect(l, z, initial, low, high);
    }
    void addHorizontalSlider(const char *l, float *z, float initial, float low, float high, float) override
    {
        collect(l, z, initial, low, high);
    }
    void addNumEntry(const char *l, float *z, float initial, float low, float high, float) override
    {
        collect(l, z, initial, low, high);
    }
    void addHorizontalBargraph(const char *, float *, float, float) override {}
    void addVerticalBargraph(const char *, float *, float, float) override {}
    void addSoundfile(const char *, const char *, Soundfile **) override { unsupported = true; }
};
AudioGenerator::~AudioGenerator() = default;
AudioGenerator::AudioGenerator()
{
    for (auto &v : m_values)
        v = 0;
    m_parameters.assign(parameters.begin(), parameters.end());
    for (int i = 0; i < Count; ++i) {
        m_values[i] = parameters[i].initial;
    }
}
bool AudioGenerator::isRecipe(const QString &path)
{
    return QFileInfo(path).suffix().compare("nyxaudio", Qt::CaseInsensitive) == 0 ||
           QFileInfo(path).suffix().compare("dsp", Qt::CaseInsensitive) == 0;
}
bool AudioGenerator::sonification() const
{
    return m_kind == 2 || (external() && m_backend && m_backend->processor->getNumInputs() == 4);
}
void AudioGenerator::setValue(int i, double v)
{
    if (i >= 0 && i < parameterCount() && std::isfinite(v))
        m_values[i].store(std::clamp(v, parameter(i).low, parameter(i).high), std::memory_order_relaxed);
}
void AudioGenerator::setColor(float r, float g, float b)
{
    const float c[] = {r, g, b};
    for (int i = 0; i < 3; ++i)
        m_color[i].store(std::isfinite(c[i]) ? std::clamp(c[i], 0.f, 1.f) : 0.f, std::memory_order_relaxed);
}
std::unique_ptr<AudioGenerator> AudioGenerator::load(const QString &path, QString *error, int rate)
{
    auto fail = [&](const QString &why) -> std::unique_ptr<AudioGenerator> {
        if (error)
            *error = "Audio generator: " + why;
        return {};
    };
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
        return fail("cannot read recipe (maximum 64 KiB)");
    if (QFileInfo(path).suffix().compare("dsp", Qt::CaseInsensitive) == 0) {
#ifdef NYXMAP_FAUST_COMPILER
        auto synth = std::unique_ptr<AudioGenerator>(new AudioGenerator);
        synth->m_kind = 5;
        synth->m_parameters.clear();
        auto &backend = synth->m_backend;
        backend = std::make_unique<Backend>();
        backend->owner = synth.get();
        backend->custom = true;
        backend->rate = rate;
        QString error;
        backend->processor =
            compileFaustProgram(QString::fromUtf8(file.readAll()), QFileInfo(path).absolutePath(), &error);
        if (!backend->processor)
            return fail(error);
        const int inputs = backend->processor->getNumInputs(), outputs = backend->processor->getNumOutputs();
        if ((inputs != 0 && inputs != 4) || (outputs != 1 && outputs != 2))
            return fail("Faust generators require 0 or 4 inputs and 1 or 2 outputs");
        backend->processor->init(rate);
        backend->processor->buildUserInterface(backend.get());
        if (backend->unsupported)
            return fail("unsupported Faust controls (maximum 128; soundfile widgets unsupported)");
        return synth;
#else
        return fail("this build has no libfaust compiler; built-in generators remain available");
#endif
    }
    QJsonParseError parse;
    const auto doc = QJsonDocument::fromJson(file.readAll(), &parse);
    if (parse.error != QJsonParseError::NoError || !doc.isObject())
        return fail("expected JSON object");
    const auto recipe = doc.object();
    if (recipe.value("version").toInt() != 1)
        return fail("unsupported version");
    const auto type = recipe.value("type").toString();
    if (type != "fm" && type != "granular" && type != "video" && type != "white" && type != "pink")
        return fail("unknown synthesis type");
    auto synth = std::unique_ptr<AudioGenerator>(new AudioGenerator);
    synth->m_kind = type == "granular" ? 1 : type == "video" ? 2 : type == "white" ? 3 : type == "pink" ? 4 : 0;
    for (int i = 0; i < Count; ++i) {
        const auto &p = parameters[i];
        const auto v = recipe.value(QLatin1String(p.key));
        const double n = v.isUndefined() ? p.initial : v.toDouble(NAN);
        if (!std::isfinite(n) || n < p.low || n > p.high)
            return fail(QString("invalid parameter: ") + p.key);
        synth->m_values[i] = n;
    }
    // A sonification reads the live composition, never pre-analyzes a video file.
    if (type == "video" && recipe.contains("source"))
        return fail("video uses the live composition; remove source and load the video on a visual layer");
    if (type == "granular" && recipe.contains("source")) {
        const auto relative = recipe.value("source").toString();
        const auto source = QFileInfo(path).dir().absoluteFilePath(relative);
        if (relative.isEmpty() || !QFileInfo(source).isFile())
            return fail("source must be an existing local audio file");
        Decoder decoder;
        if (!decoder.open(source, AVMEDIA_TYPE_AUDIO))
            return fail("cannot decode grain source");
        AVChannelLayout mono = AV_CHANNEL_LAYOUT_MONO;
        if (swr_alloc_set_opts2(&decoder.resample, &mono, AV_SAMPLE_FMT_FLT, sampleRate, &decoder.codec->ch_layout,
                                decoder.codec->sample_fmt, decoder.codec->sample_rate, 0, nullptr) < 0 ||
            swr_init(decoder.resample) < 0)
            return fail("cannot resample source");
        bool converted = true;
        const bool ok = decoder.read([&](AVFrame *f) {
            const int capacity = swr_get_out_samples(decoder.resample, f->nb_samples);
            if (capacity <= 0 || capacity > sampleRate * 60) {
                converted = false;
                return false;
            }
            std::vector<float> buffer(size_t(capacity), 0);
            uint8_t *dst[] = {reinterpret_cast<uint8_t *>(buffer.data())};
            const int n = swr_convert(decoder.resample, dst, capacity, const_cast<const uint8_t **>(f->extended_data),
                                      f->nb_samples);
            if (n < 0) {
                converted = false;
                return false;
            }
            const size_t count = std::min(size_t(n), size_t(sampleRate * 60) - synth->m_sample.size());
            synth->m_sample.insert(synth->m_sample.end(), buffer.begin(), buffer.begin() + count);
            return synth->m_sample.size() < size_t(sampleRate * 60);
        });
        if (!ok || !converted || synth->m_sample.empty())
            return fail("cannot read grain samples");
        for (float &s : synth->m_sample)
            s = std::isfinite(s) ? std::clamp(s, -1.f, 1.f) : 0.f;
    }
    if (rate < 8000 || rate > 192000)
        return fail("unsupported sample rate");
    synth->m_backend = std::make_unique<Backend>();
    synth->m_backend->rate = rate;
    synth->m_backend->processor = std::make_unique<NyxFaustSynth>();
    synth->m_backend->processor->buildUserInterface(synth->m_backend.get());
    synth->m_backend->processor->init(rate);
    *synth->m_backend->kind = float(synth->m_kind);
    *synth->m_backend->hasSample = synth->m_sample.empty() ? 0.f : 1.f;
    return synth;
}
float AudioGenerator::mix(float *out, int frames, int rate, bool playing, double speed, float gain)
{
    if (!out || frames <= 0 || !m_backend || rate != m_backend->rate)
        return 0;
    float peak = 0;
    auto &b = *m_backend;
    speed = std::isfinite(speed) ? std::clamp(std::abs(speed), 0., 8.) : 0;
    const double target = (external() && !playing) ? 0 : std::isfinite(gain) ? std::clamp(double(gain), 0., 2.) : 0;
    for (int i = 0; i < parameterCount(); ++i)
        if (b.zones[i])
            *b.zones[i] = float(value(i));
    if (!external()) {
        *b.zones[Gate] = playing && speed > 0 ? float(value(Gate)) : 0.f;
        *b.speed = float(speed);
    } else if (b.externalGate >= 0 && (!playing || speed == 0))
        *b.zones[b.externalGate] = 0;

    const double position = value(Position) * std::max(0., double(m_sample.size()) - 1);
    const float rgb[] = {m_color[0].load(), m_color[1].load(), m_color[2].load()};
    // Fixed stack buffers, prepared before compute: no locks, heap work, IO or
    // compilation in the callback. Faust owns oscillators, grains, noise and ADSR.
    constexpr int block = 64;
    float source[block], red[block], green[block], blue[block], audio[block], right[block];
    float *inputs[] = {source, red, green, blue};
    float *outputs[] = {audio, right};
    for (int offset = 0; offset < frames; offset += block) {
        const int n = std::min(block, frames - offset);
        for (int i = 0; i < n; ++i) {
            source[i] = 0;
            if (!m_sample.empty()) {
                const double at = std::fmod(m_readPosition + position, double(m_sample.size()));
                const size_t p = size_t(at), q = (p + 1) % m_sample.size();
                source[i] = float(m_sample[p] + (m_sample[q] - m_sample[p]) * (at - p));
                m_readPosition =
                    std::fmod(m_readPosition + (playing ? speed : 0) * sampleRate / rate, double(m_sample.size()));
            }
            red[i] = rgb[0];
            green[i] = rgb[1];
            blue[i] = rgb[2];
        }
        b.processor->compute(n, inputs, outputs);
        for (int i = 0; i < n; ++i) {
            m_level += std::clamp(target - m_level, -1. / (.008 * rate), 1. / (.008 * rate));
            const float s = std::isfinite(audio[i]) ? float(std::clamp(audio[i] * m_level, -1., 1.)) : 0.f;
            out[2 * (offset + i)] += s;
            const float r = b.processor->getNumOutputs() == 2
                                ? (std::isfinite(right[i]) ? float(std::clamp(right[i] * m_level, -1., 1.)) : 0.f)
                                : s;
            out[2 * (offset + i) + 1] += r;
            peak = std::max(peak, std::max(std::abs(s), std::abs(r)));
        }
    }
    return peak;
}
