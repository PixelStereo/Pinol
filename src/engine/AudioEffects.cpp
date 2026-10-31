#include "AudioEffects.h"
#include "FaustInterpreter.h"
#include "ParameterKey.h"
#include "faust/dsp/dsp.h"
#include "faust/gui/UI.h"
#include "faust/gui/meta.h"
#include "generated/NyxFxBandPass.h"
#include "generated/NyxFxChorus.h"
#include "generated/NyxFxCompressor.h"
#include "generated/NyxFxDelay.h"
#include "generated/NyxFxDistortion.h"
#include "generated/NyxFxEqualizer.h"
#include "generated/NyxFxFlanger.h"
#include "generated/NyxFxHighPass.h"
#include "generated/NyxFxLowPass.h"
#include "generated/NyxFxNoiseGate.h"
#include "generated/NyxFxReverb.h"
#include "generated/NyxFxStereoWidth.h"
#include "generated/NyxFxTremolo.h"
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>
#include <thread>

const std::array<AudioEffect::Preset, 13> AudioEffect::presets = {{{"lowpass", "Low-pass filter", 1},
                                                                   {"highpass", "High-pass filter", 1},
                                                                   {"bandpass", "Band-pass filter", 1},
                                                                   {"eq", "3-band EQ", 1},
                                                                   {"delay", "Delay", .35},
                                                                   {"reverb", "Freeverb", .35},
                                                                   {"distortion", "Distortion", 1},
                                                                   {"compressor", "Compressor", 1},
                                                                   {"gate", "Noise gate", 1},
                                                                   {"chorus", "Chorus", .5},
                                                                   {"flanger", "Flanger", .5},
                                                                   {"tremolo", "Tremolo", 1},
                                                                   {"width", "Stereo width", 1}}};
struct AudioEffect::Backend : UI {
    AudioEffect &owner;
    std::unique_ptr<dsp> processor;
    std::array<float *, 128> zones{};
    bool unsupported = false;
    explicit Backend(AudioEffect &effect) : owner(effect) {}
    void add(const char *label, float *zone, float initial, float low, float high)
    {
        const size_t i = owner.m_parameters.size();
        if (i >= zones.size() || !std::isfinite(initial) || !std::isfinite(low) || !std::isfinite(high) || low > high) {
            unsupported = true;
            return;
        }
        for (size_t j = 0; j < i; ++j)
            if (zones[j] == zone)
                return;
        QStringList taken;
        for (const auto &p : owner.m_parameters) taken << p.key;
        owner.m_parameters.push_back({faustControlKey(QString::fromUtf8(label), taken, int(i)), QString::fromUtf8(label), low, high, initial});
        owner.m_values[i] = std::clamp(double(initial), double(low), double(high));
        zones[i] = zone;
    }
    void openTabBox(const char *) override {}
    void openHorizontalBox(const char *) override {}
    void openVerticalBox(const char *) override {}
    void closeBox() override {}
    void addButton(const char *l, float *z) override { add(l, z, 0, 0, 1); }
    void addCheckButton(const char *l, float *z) override { add(l, z, 0, 0, 1); }
    void addVerticalSlider(const char *l, float *z, float v, float lo, float hi, float) override
    {
        add(l, z, v, lo, hi);
    }
    void addHorizontalSlider(const char *l, float *z, float v, float lo, float hi, float) override
    {
        add(l, z, v, lo, hi);
    }
    void addNumEntry(const char *l, float *z, float v, float lo, float hi, float) override { add(l, z, v, lo, hi); }
    void addHorizontalBargraph(const char *, float *, float, float) override {}
    void addVerticalBargraph(const char *, float *, float, float) override {}
    void addSoundfile(const char *, const char *, Soundfile **) override { unsupported = true; }
};
AudioEffect::AudioEffect()
{
    for (auto &v : m_values)
        v = 0;
}
AudioEffect::~AudioEffect() = default;
QString AudioEffect::name() const
{
    for (const auto &p : presets)
        if (m_kind == QLatin1String(p.key))
            return QString::fromUtf8(p.label);
    return m_kind == "faust" ? QFileInfo(m_path).completeBaseName() : m_kind;
}
void AudioEffect::setValue(int index, double v)
{
    if (index >= 0 && size_t(index) < m_parameters.size() && std::isfinite(v))
        m_values[size_t(index)].store(std::clamp(v, m_parameters[size_t(index)].low, m_parameters[size_t(index)].high),
                                      std::memory_order_relaxed);
}
void AudioEffect::setWet(double v)
{
    if (std::isfinite(v))
        m_wet.store(std::clamp(v, 0., 1.), std::memory_order_relaxed);
}
void AudioEffect::restoreControls(const QJsonObject &state)
{
    setEnabled(state.value("enabled").toBool(true));
    setWet(state.value("wet").toDouble(wet()));
    const auto values = state.value("values").toObject();
    for (size_t i = 0; i < m_parameters.size(); ++i)
        if (values.contains(m_parameters[i].key))
            setValue(int(i), values.value(m_parameters[i].key).toDouble(value(int(i))));
}
QJsonObject AudioEffect::toJson() const
{
    QJsonObject state = m_saved;
    state["id"] = m_id;
    state["kind"] = m_kind;
    if (!m_path.isEmpty())
        state["path"] = m_path;
    state["enabled"] = enabled();
    state["wet"] = wet();
    auto values = state.value("values").toObject();
    for (size_t i = 0; i < m_parameters.size(); ++i)
        values[m_parameters[i].key] = value(int(i));
    state["values"] = values;
    return state;
}
std::shared_ptr<AudioEffect> AudioEffect::load(const QJsonObject &state, int rate, QString *error, bool keepUnavailable)
{
    auto effect = std::shared_ptr<AudioEffect>(new AudioEffect);
    effect->m_saved = state;
    effect->m_rate = rate;
    effect->m_id = state.value("id").toString();
    if (effect->m_id.isEmpty())
        effect->m_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    auto fail = [&](const QString &reason) -> std::shared_ptr<AudioEffect> {
        if (error)
            *error = reason;
        if (!keepUnavailable)
            return {};
        effect->m_error = reason;
        effect->m_backend.reset();
        effect->m_parameters.clear();
        effect->restoreControls(state);
        return effect;
    };
    if (rate < 8000 || rate > 192000)
        return fail("Unsupported audio FX sample rate");
    effect->m_kind = state.value("kind").toString();
    effect->m_path = state.value("path").toString();
    auto &backend = effect->m_backend;
    backend = std::make_unique<Backend>(*effect);
    const auto &kind = effect->m_kind;
    for (const auto &preset : presets)
        if (kind == QLatin1String(preset.key))
            effect->m_wet = preset.wet;
    if (kind == "lowpass")
        backend->processor = std::make_unique<NyxFxLowPass>();
    else if (kind == "highpass")
        backend->processor = std::make_unique<NyxFxHighPass>();
    else if (kind == "bandpass")
        backend->processor = std::make_unique<NyxFxBandPass>();
    else if (kind == "eq")
        backend->processor = std::make_unique<NyxFxEqualizer>();
    else if (kind == "delay")
        backend->processor = std::make_unique<NyxFxDelay>();
    else if (kind == "reverb")
        backend->processor = std::make_unique<NyxFxReverb>();
    else if (kind == "distortion")
        backend->processor = std::make_unique<NyxFxDistortion>();
    else if (kind == "compressor")
        backend->processor = std::make_unique<NyxFxCompressor>();
    else if (kind == "gate")
        backend->processor = std::make_unique<NyxFxNoiseGate>();
    else if (kind == "chorus")
        backend->processor = std::make_unique<NyxFxChorus>();
    else if (kind == "flanger")
        backend->processor = std::make_unique<NyxFxFlanger>();
    else if (kind == "tremolo")
        backend->processor = std::make_unique<NyxFxTremolo>();
    else if (kind == "width")
        backend->processor = std::make_unique<NyxFxStereoWidth>();
    else if (kind == "faust") {
        QFile file(effect->m_path);
        if (!file.open(QIODevice::ReadOnly) || file.size() > 65536)
            return fail("Cannot read Faust FX program (maximum 64 KiB)");
        QString message;
        backend->processor =
            compileFaustProgram(QString::fromUtf8(file.readAll()), QFileInfo(file).absolutePath(), &message);
        if (!backend->processor)
            return fail(message);
    } else
        return fail("Unknown audio FX type: " + kind);
    if (backend->processor->getNumInputs() != 2 || backend->processor->getNumOutputs() != 2)
        return fail("Audio FX must have exactly two inputs and two outputs (stereo)");
    backend->processor->init(rate);
    backend->processor->buildUserInterface(backend.get());
    if (backend->unsupported)
        return fail("Unsupported Faust FX controls (maximum 128; soundfile widgets unavailable)");
    effect->restoreControls(state);
    effect->m_blend = effect->enabled() ? effect->wet() : 0;
    return effect;
}
void AudioEffect::process(float *stereo, int frames)
{
    if (!m_backend || !m_backend->processor) {
        m_meter.update(stereo, frames, m_rate); // an unavailable FX lets the signal through
        return;
    }
    auto &b = *m_backend;
    for (size_t i = 0; i < m_parameters.size(); ++i)
        *b.zones[i] = float(value(int(i)));
    constexpr int block = 64;
    float left[block], right[block], outLeft[block], outRight[block];
    float *in[] = {left, right}, *out[] = {outLeft, outRight};
    const double goal = enabled() ? wet() : 0;
    const double smooth = 1 - std::exp(-1. / (.008 * m_rate));
    for (int offset = 0; offset < frames; offset += block) {
        const int n = std::min(block, frames - offset);
        for (int i = 0; i < n; ++i) {
            left[i] = std::isfinite(stereo[2 * (offset + i)]) ? stereo[2 * (offset + i)] : 0.f;
            right[i] = std::isfinite(stereo[2 * (offset + i) + 1]) ? stereo[2 * (offset + i) + 1] : 0.f;
        }
        // Run while bypassed too: switching back preserves delay/reverb state.
        b.processor->compute(n, in, out);
        for (int i = 0; i < n; ++i) {
            m_blend += (goal - m_blend) * smooth;
            const double l = std::isfinite(outLeft[i]) ? outLeft[i] : 0;
            const double r = std::isfinite(outRight[i]) ? outRight[i] : 0;
            stereo[2 * (offset + i)] = float(std::clamp(left[i] + m_blend * (l - left[i]), -16., 16.));
            stereo[2 * (offset + i) + 1] = float(std::clamp(right[i] + m_blend * (r - right[i]), -16., 16.));
        }
    }
    m_meter.update(stereo, frames, m_rate);
}
AudioEffectChain::List AudioEffectChain::effects() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return *m_owned;
}
QJsonArray AudioEffectChain::toJson() const
{
    QJsonArray state;
    for (const auto &effect : effects())
        state.append(effect->toJson());
    return state;
}
bool AudioEffectChain::replace(const QJsonArray &state, QString *error, bool keepUnavailable)
{
    if (state.size() > 16) {
        if (error)
            *error = "At most 16 audio FX per layer";
        return false;
    }
    const auto old = effects();
    auto next = std::make_shared<List>();
    next->reserve(size_t(state.size()));
    QSet<QString> ids;
    static const QRegularExpression validId(QStringLiteral("^[A-Za-z0-9_-]{1,64}$"));
    for (const auto &value : state) {
        if (!value.isObject()) {
            if (error)
                *error = "Invalid audio FX entry";
            return false;
        }
        const auto item = value.toObject();
        const auto id = item.value("id").toString();
        if (!id.isEmpty() && (!validId.match(id).hasMatch() || ids.contains(id))) {
            if (error)
                *error = "Invalid or duplicate audio FX id";
            return false;
        }
        std::shared_ptr<AudioEffect> effect;
        for (const auto &entry : old)
            if (!id.isEmpty() && entry->id() == id && entry->kind() == item.value("kind").toString() &&
                entry->path() == item.value("path").toString())
                effect = entry;
        if (!effect)
            effect = AudioEffect::load(item, m_rate, error, keepUnavailable);
        if (!effect)
            return false;
        ids.insert(effect->id());
        next->push_back(std::move(effect));
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    for (int i = 0; i < state.size(); ++i)
        (*next)[size_t(i)]->restoreControls(state[i].toObject());
    const auto retired = std::move(m_owned);
    m_owned = next;
    m_chain.store(m_owned.get());
    const uint64_t epoch = m_epoch.load();
    if (epoch & 1)
        while (m_epoch.load() == epoch)
            std::this_thread::yield();
    m_revision.fetch_add(1);
    return true;
}
void AudioEffectChain::process(float *stereo, int frames, int rate)
{
    if (!stereo || frames <= 0 || rate != m_rate)
        return;
    m_epoch.fetch_add(1);
    const List *chain = m_chain.load();
    const bool on = enabled();
    for (const auto &effect : *chain) {
        if (on) effect->process(stereo, frames);
        else effect->idle(frames, m_rate);
    }
    m_epoch.fetch_add(1);
}
