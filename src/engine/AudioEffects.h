#pragma once
#include "AudioMeter.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class AudioEffect {
public:
    struct Parameter {
        QString key, label;
        double low, high, initial;
    };
    struct Preset {
        const char *key, *label;
        double wet;
    };
    static const std::array<Preset, 13> presets;
    static std::shared_ptr<AudioEffect> load(const QJsonObject &state, int rate, QString *error,
                                             bool keepUnavailable = false);
    ~AudioEffect();
    QString id() const { return m_id; }
    QString kind() const { return m_kind; }
    QString path() const { return m_path; }
    QString name() const;
    QString error() const { return m_error; }
    const std::vector<Parameter> &parameters() const { return m_parameters; }
    double value(int index) const { return m_values[size_t(index)].load(std::memory_order_relaxed); }
    void setValue(int index, double value);
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool v) { m_enabled.store(v, std::memory_order_relaxed); }
    double wet() const { return m_wet.load(std::memory_order_relaxed); }
    void setWet(double v);
    void restoreControls(const QJsonObject &state);
    QJsonObject toJson() const;
    // Exactly one audio reader. The effect is fully prepared at load time.
    void process(float *stereo, int frames);
    // What leaves this effect (after its dry/wet), for the interface
    const AudioMeter &meter() const { return m_meter; }
    void idle(int frames, int rate) { m_meter.silence(frames, rate); } // audio thread: not run, its meter falls

private:
    AudioEffect();
    QString m_id, m_kind, m_path, m_error;
    QJsonObject m_saved;
    std::vector<Parameter> m_parameters;
    std::array<std::atomic<double>, 128> m_values;
    std::atomic<bool> m_enabled{true};
    std::atomic<double> m_wet{1};
    double m_blend = 1;
    int m_rate = 48000;
    struct Backend;
    std::unique_ptr<Backend> m_backend;
    AudioMeter m_meter;
};

// Immutable chain publication. Writers prepare/compile outside the callback and
// retain old objects until its reader finishes. Audio never locks or allocates.
class AudioEffectChain {
public:
    using List = std::vector<std::shared_ptr<AudioEffect>>;
    explicit AudioEffectChain(int rate = 48000) : m_rate(rate) {}
    bool replace(const QJsonArray &state, QString *error = nullptr, bool keepUnavailable = false);
    List effects() const;
    QJsonArray toJson() const;
    uint64_t revision() const { return m_revision.load(); }
    // The master switch of the chain (each FX keeps its own): off, the signal goes through untouched
    bool enabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool on) { m_enabled.store(on, std::memory_order_relaxed); }
    void process(float *stereo, int frames, int rate);

private:
    int m_rate;
    mutable std::mutex m_mutex; // writers / metadata only
    std::shared_ptr<const List> m_owned = std::make_shared<List>();
    std::atomic<const List *> m_chain{m_owned.get()};
    std::atomic<uint64_t> m_epoch{0}, m_revision{1};
    std::atomic<bool> m_enabled{true};
};
