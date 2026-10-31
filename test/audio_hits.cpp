// Hit detection: kicks and hats in the right regions, velocity, the minimum gap, silence and steady sounds quiet.
#include "AudioAnalysis.h"
#include "OnsetAnalysis.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <vector>

namespace {
constexpr int rate = 48000, block = 480; // 10 ms
constexpr double pi = 3.14159265358979323846;

struct Hit {
    double time, velocity;
};

// A decaying sine (a kick) or a burst of noise (a hat) at `at` seconds
struct Burst {
    double at, amplitude;
    bool noise;
    double hz, decay;
};

std::vector<float> render(double seconds, const std::vector<Burst> &bursts, double steadyHz = 0, double steadyAmp = 0, double noiseFloor = 0)
{
    std::vector<float> out(size_t(seconds * rate) * 2, 0.f);
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> uniform(-1.f, 1.f);
    for (size_t i = 0; i < out.size() / 2; ++i) {
        const double t = double(i) / rate;
        double x = steadyAmp * std::sin(2 * pi * steadyHz * t) + noiseFloor * uniform(rng);
        for (const Burst &b : bursts) {
            const double dt = t - b.at;
            if (dt < 0 || dt > b.decay * 8) continue;
            const double env = std::exp(-dt / b.decay);
            // A hat: partials from 6 to 13 kHz with their own phases (a real hat has no energy in the bass); a kick: a sine
            double tone = std::sin(2 * pi * b.hz * dt);
            if (b.noise) {
                tone = 0;
                for (int p = 0; p < 4; ++p) tone += .5 * std::sin(2 * pi * (6000 + 2300 * p) * dt + 1.7 * p * p);
            }
            x += b.amplitude * env * tone;
        }
        out[2 * i] = out[2 * i + 1] = float(x);
    }
    return out;
}

// The hits a region hears (rising edges of the pulse), the audio as the device would deliver it: blocks of 10 ms
std::vector<Hit> hits(const std::vector<float> &audio, double lo, double hi, double sensitivity = 0.5, double gap = 0.08)
{
    AudioAnalysis analysis;
    OnsetDetector detector;
    std::vector<Hit> found;
    double previous = 0;
    for (size_t at = 0; at + block * 2 <= audio.size(); at += block * 2) {
        analysis.process(audio.data() + at, block, rate);
        const double out = detector.step(analysis.onsetFlux(), lo, hi, sensitivity, gap, double(block) / rate);
        if (out > 0 && previous == 0) found.push_back({double(at / 2) / rate, out});
        previous = out;
    }
    return found;
}
} // namespace

int main()
{
    int failures = 0;
    auto check = [&](bool ok, const char *why) {
        if (!ok) { ++failures; std::printf("FAIL audio_hits: %s\n", why); }
    };
    std::vector<Burst> kicks, hats, both, mixedLoudness;
    for (int k = 0; k < 16; ++k) {
        kicks.push_back({1.0 + 0.5 * k, 0.7, false, 60, 0.06});
        hats.push_back({1.25 + 0.5 * k, 0.5, true, 0, 0.012});
        mixedLoudness.push_back({1.0 + 0.5 * k, k % 2 ? 0.15 : 0.8, false, 60, 0.06});
    }
    both = kicks;
    both.insert(both.end(), hats.begin(), hats.end());

    const auto kickAudio = render(10, kicks);
    const auto kickHits = hits(kickAudio, 30, 200);
    check(kickHits.size() >= 15 && kickHits.size() <= 17, "sixteen kicks are heard in the bass");
    bool timed = !kickHits.empty();
    for (size_t k = 0; k < kickHits.size(); ++k) {
        const double expected = 1.0 + 0.5 * double(k);
        timed &= std::abs(kickHits[k].time - expected) < 0.05; // within 50 ms: the 21 ms window and the 10 ms blocks included
    }
    check(timed, "each hit is on its kick, within 50 ms");
    check(hits(kickAudio, 4000, 16000).empty(), "a kick is not heard in the highs");

    const auto bothAudio = render(10, both);
    const auto bass = hits(bothAudio, 30, 200), highs = hits(bothAudio, 4000, 16000), all = hits(bothAudio, 20, 20000);
    check(bass.size() >= 15 && bass.size() <= 17, "kicks only, with hats in the mix: the bass hears the kicks");
    check(highs.size() >= 15 && highs.size() <= 17, "hats only: the highs hear the hats");
    check(all.size() >= 28, "the whole spectrum hears both");

    // Nothing happens: silence, a steady tone, a steady tone with a noise floor
    check(hits(render(8, {}), 20, 20000).empty(), "silence has no hits");
    check(hits(render(8, {}, 440, 0.2), 20, 20000).empty(), "a steady tone has no hits");
    check(hits(render(8, {}, 440, 0.2, 0.02), 20, 20000).empty(), "a steady tone over a noise floor has no hits");

    // Velocity: the hard kicks are louder than the soft ones
    {
        const auto h = hits(render(10, mixedLoudness), 30, 200);
        double hard = 0, soft = 0;
        int nh = 0, ns = 0;
        for (const Hit &x : h) {
            const int k = int(std::lround((x.time - 1.0) / 0.5));
            if (k % 2) { soft += x.velocity; ++ns; } else { hard += x.velocity; ++nh; }
        }
        check(nh >= 7 && ns >= 6, "hard and soft kicks are both heard");
        check(nh && ns && hard / nh > soft / ns * 1.5, "hard hits have a higher velocity than soft ones");
        bool inRange = true;
        for (const Hit &x : h) inRange &= x.velocity > 0 && x.velocity <= 1;
        check(inRange, "velocity is within 0..1");
    }
    // The minimum gap: two kicks 50 ms apart are one hit at 80 ms, two at 40 ms
    {
        std::vector<Burst> pair;
        for (int k = 0; k < 8; ++k) {
            pair.push_back({1.0 + 1.0 * k, 0.7, false, 60, 0.04});
            pair.push_back({1.0 + 1.0 * k + 0.12, 0.7, false, 60, 0.04});
        }
        const auto audio = render(10, pair);
        const auto fast = hits(audio, 30, 200, 0.5, 0.04), slow = hits(audio, 30, 200, 0.5, 0.3);
        check(fast.size() > slow.size() + 3, "a longer minimum gap merges the quick doubles");
        check(slow.size() >= 7 && slow.size() <= 9, "with a 300 ms gap there is one hit a pair");
    }
    // Sensitivity: more of the faint hits are heard when it is raised
    {
        std::vector<Burst> faint;
        for (int k = 0; k < 16; ++k) faint.push_back({1.0 + 0.5 * k, k % 4 == 0 ? 0.6 : 0.12, false, 60, 0.06});
        const auto audio = render(10, faint);
        check(hits(audio, 30, 200, 1.0).size() >= hits(audio, 30, 200, 0.0).size(), "a higher sensitivity hears at least as many hits");
    }
    // The pulse: a few tens of milliseconds, then 0 again
    {
        AudioAnalysis analysis;
        OnsetDetector detector;
        const auto audio = render(4, {{2.0, 0.8, false, 60, 0.06}});
        int high = 0, blocks = 0;
        bool afterwards = true;
        for (size_t at = 0; at + block * 2 <= audio.size(); at += block * 2, ++blocks) {
            analysis.process(audio.data() + at, block, rate);
            const double out = detector.step(analysis.onsetFlux(), 30, 200, 0.5, 0.08, double(block) / rate);
            if (out > 0) ++high;
            if (blocks > 260 && out != 0) afterwards = false;
        }
        check(high >= 3 && high <= 8, "a hit is a pulse of a few blocks (about 40 ms)");
        check(afterwards, "and the output returns to 0");
    }
    // Reset and a restarting analysis do not leave the detector stuck, and invalid regions are silent
    {
        OnsetDetector detector;
        OnsetFlux none;
        check(detector.step(none, 30, 200, 0.5, 0.08, 0.01) == 0, "no flux: no hit");
        AudioAnalysis a;
        const auto audio = render(2, {});
        for (size_t at = 0; at + block * 2 <= audio.size(); at += block * 2) a.process(audio.data() + at, block, rate);
        check(detector.step(a.onsetFlux(), 200, 30, 0.5, 0.08, 0.01) == 0, "an inverted region hears nothing");
        a.reset();
        check(a.onsetFlux().newest == 0, "reset clears the flux");
    }
    if (!failures) std::printf("Audio hits: kicks, hats, velocity, gap, sensitivity, silence passed\n");
    return failures ? 1 : 0;
}
