#pragma once
#include <QString>
#include <array>
#include <atomic>
#include <deque>
#include <memory>
#include <string>
#include <vector>

// Sample-accurate DSP. Only load() performs I/O/decoding/allocation. mix() is
// owned by one audio callback; controls and visual input are atomically published.
class AudioGenerator {
public:
    ~AudioGenerator();
    enum Control {
        Frequency,
        Ratio,
        Index,
        Gain,
        GrainMs,
        Density,
        Pitch,
        Position,
        Attack,
        Decay,
        Sustain,
        Release,
        Gate,
        Count
    };
    struct Parameter {
        const char *key, *label;
        double low, high, initial;
    };
    static const std::array<Parameter, Count> parameters;
    static bool isRecipe(const QString &path);
    static std::unique_ptr<AudioGenerator> load(const QString &path, QString *error, int rate = 48000);
    double value(int i) const { return m_values[size_t(i)].load(std::memory_order_relaxed); }
    void setValue(int i, double v);
    bool granular() const { return m_kind == 1; }
    bool sonification() const;
    bool external() const { return m_kind == 5; }
    bool noise() const { return m_kind == 3 || m_kind == 4; }
    int parameterCount() const { return int(m_parameters.size()); }
    const Parameter &parameter(int i) const { return m_parameters[size_t(i)]; }
    void setColor(float r, float g, float b);
    std::array<float, 3> color() const { return {m_color[0].load(), m_color[1].load(), m_color[2].load()}; } // what it last saw
    float mix(float *stereo, int frames, int rate, bool playing, double speed, float gain);

private:
    AudioGenerator();
    int m_kind = 0;
    std::array<std::atomic<double>, 128> m_values;
    std::vector<Parameter> m_parameters;
    std::deque<std::string> m_names;
    std::vector<float> m_sample; // immutable after load
    std::array<std::atomic<float>, 3> m_color{{0, 0, 0}};
    struct Backend;
    std::unique_ptr<Backend> m_backend;
    double m_readPosition = 0, m_level = 0;
};
