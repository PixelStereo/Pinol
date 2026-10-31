// An interaction's filters as a list (as a layer's effects): applied in their order, each on or off — Remap, the filters
// over time, Curve, Math, Clamp, Steps, Threshold, Toggle, Delay, Speed; none: the value as it comes; each filter's
// settings reached as values (presets, snapshots, OSC).
// Standalone CTest area: nyxmap_tests --area interaction_filters.
#include "EngineInternal.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <cmath>
#include <cstdio>

int runInteractionFilterTests(Engine &e)
{
    int failures = 0;
    auto check = [&](bool ok, const char *what) {
        if (ok) return;
        std::printf("FAIL interaction_filters: %s\n", what);
        ++failures;
    };
    auto near = [](double a, double b, double tol = 1e-6) { return std::abs(a - b) < tol; };
    auto stage = [](InteractionFilter kind, double a = qQNaN(), double b = qQNaN(), MathOp op = MathOp::Multiply) {
        InteractionStage s = InteractionStage::made(kind);
        if (!std::isnan(a)) s.a = a;
        if (!std::isnan(b)) s.b = b;
        s.op = op;
        return s;
    };
    // One value through a chain (no time), the result before the master level's 0..1 clamp is the same as after
    auto through = [](const std::vector<InteractionStage> &filters, double x) {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = filters;
        InteractionProcessor p;
        p.input(it, x);
        return *p.step(it, 1.0 / 60);
    };

    // Math, each operation on x with a
    auto math = [&](MathOp op, double a, double x) { return through({stage(InteractionFilter::Math, a, 0, op)}, x); };
    check(near(math(MathOp::Add, 0.25, 0.5), 0.75) && near(math(MathOp::Subtract, 0.25, 0.5), 0.25), "+ and −");
    check(near(math(MathOp::Multiply, 0.5, 0.8), 0.4) && near(math(MathOp::Divide, 4, 0.8), 0.2), "× and ÷");
    check(near(math(MathOp::Divide, 0, 0.8), 0.8), "÷ 0 leaves the value");
    check(near(math(MathOp::Modulo, 0.25, 0.6), 0.1) && near(math(MathOp::Power, 2, 0.5), 0.25), "modulo and power");
    check(near(math(MathOp::Min, 0.3, 0.6), 0.3) && near(math(MathOp::Max, 0.3, 0.1), 0.3), "min and max");
    check(near(math(MathOp::Round, 0.25, 0.4), 0.5) && near(math(MathOp::Floor, 0.25, 0.4), 0.25) &&
              near(math(MathOp::Ceil, 0.25, 0.3), 0.5), "floor, ceil, round to a step");
    check(near(math(MathOp::OneMinus, 0, 0.2), 0.8), "1 − x inverts");
    check(near(math(MathOp::Multiply, 3, 0.5), 1.5), "as it comes out: no range of its own");

    // Order matters, and a value may leave 0..1 between filters
    check(near(through({stage(InteractionFilter::Math, 3, 0, MathOp::Multiply), stage(InteractionFilter::Math, 1.2, 0, MathOp::Subtract)}, 0.5), 0.3),
          "× 3 then − 1.2: 0.3 (1.5 kept between them)");
    check(near(through({stage(InteractionFilter::Math, 1.2, 0, MathOp::Subtract), stage(InteractionFilter::Math, 3, 0, MathOp::Multiply)}, 0.5), -2.1),
          "the other order: −2.1");
    InteractionStage off = stage(InteractionFilter::Math, 0, 0, MathOp::OneMinus);
    off.enabled = false;
    check(near(through({off}, 0.2), 0.2), "a filter off lets the value through");

    // Clamp, Steps, Curve
    check(near(through({stage(InteractionFilter::Clamp, 0.2, 0.6)}, 0.9), 0.6) && near(through({stage(InteractionFilter::Clamp, 0.2, 0.6)}, 0.1), 0.2),
          "clamp");
    check(near(through({stage(InteractionFilter::Steps, 3)}, 0.3), 0.5) && near(through({stage(InteractionFilter::Steps, 3)}, 0.2), 0.0),
          "three steps: 0, 0.5, 1");
    InteractionStage inverted = stage(InteractionFilter::Curve);
    inverted.curve.keys[0].v = 1;
    inverted.curve.keys[1].v = 0;
    check(near(through({inverted}, 0.25), 0.75), "a curve");

    // Threshold, with its margin: no flicker around it
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Threshold, 0.5, 0.2)};
        InteractionProcessor p;
        auto at = [&](double x) {
            p.input(it, x);
            return *p.step(it, 1.0 / 60);
        };
        check(at(0.3) == 0 && at(0.55) == 0 && at(0.65) == 1 && at(0.45) == 1 && at(0.35) == 0, "threshold with a margin");
    }
    // Delay: the value of half a second ago
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Delay, 0.5)};
        InteractionProcessor p;
        p.input(it, 0.2);
        p.step(it, 1.0 / 60);
        p.input(it, 0.9);
        double y = 0;
        for (int k = 0; k < 15; ++k) y = *p.step(it, 1.0 / 60); // 0.25 s
        check(near(y, 0.2), "still the value of before");
        for (int k = 0; k < 30; ++k) y = *p.step(it, 1.0 / 60); // 0.75 s
        check(near(y, 0.9), "then the new one, half a second later");
    }
    // Speed: how fast it moves (1 for the full range in a second)
    {
        Interaction it = Interaction::made(InteractionSource::Osc);
        it.filters = {stage(InteractionFilter::Speed, 1, 0.001)};
        InteractionProcessor p;
        double y = 0;
        for (int k = 0; k <= 30; ++k) { // 0 → 0.25 in half a second: half the full scale
            p.input(it, 0.25 * k / 30.0);
            y = *p.step(it, 1.0 / 60);
        }
        check(near(y, 0.5, 0.02), "half the full range per second");
        for (int k = 0; k < 30; ++k) y = *p.step(it, 1.0 / 60);
        check(y < 0.02, "still: no speed");
    }

    // Hit: an Audio source that is a trigger at each hit of a frequency region, set and saved with its sensitivity and gap
    {
        Interaction it = Interaction::made(InteractionSource::Audio);
        it.audioFeature = Interaction::AudioFeature::Hit;
        it.audioLow = 30;
        it.audioHigh = 200;
        it.audioSensitivity = 0.8;
        it.audioGap = 0.12;
        const Interaction again = Interaction::fromJson(it.toJson());
        check(again.audioFeature == Interaction::AudioFeature::Hit && near(again.audioSensitivity, 0.8) && near(again.audioGap, 0.12) &&
                  near(again.audioLow, 30) && near(again.audioHigh, 200),
              "a Hit source is written and read back with its sensitivity and gap");
        check(it.label().contains("hits"), "and named for what it hears");
        auto junk = it.toJson();
        junk["audio_sensitivity"] = 9.0;
        junk["audio_gap"] = -3.0;
        const Interaction clamped = Interaction::fromJson(junk);
        check(near(clamped.audioSensitivity, 1.0) && near(clamped.audioGap, 0.04), "a hand-edited sensitivity and gap are kept in range");
        InteractionProcessor p;
        check(p.hit(OnsetFlux{}, it, 0.01) == 0, "a processor with no flux hears no hit");
    }

    // Envelope: a trigger in, an ADSR out
    {
        auto envelope = [](double attack, double decay, double sustain, double release) {
            InteractionStage s = InteractionStage::made(InteractionFilter::Envelope);
            s.a = attack;
            s.b = decay;
            s.c = sustain;
            s.d = release;
            return s;
        };
        auto run = [](Interaction &it, InteractionProcessor &p, double x, double seconds) { // the level at the end
            double y = 0;
            const int steps = std::max(1, int(seconds * 60));
            for (int k = 0; k < steps; ++k) {
                p.input(it, x);
                y = *p.step(it, 1.0 / 60);
            }
            return y;
        };
        {
            // An impact: it fires on the press and ends by itself, the key still held
            Interaction it = Interaction::made(InteractionSource::Keyboard);
            it.filters = {envelope(0.05, 0.2, 0, 0.3)};
            InteractionProcessor p;
            check(near(run(it, p, 0.0, 0.1), 0.0), "an envelope is silent until triggered");
            const double up = run(it, p, 1.0, 0.04); // two frames into its 50 ms attack
            check(up > 0.45 && up <= 1.0, "attack rises on the press");
            check(run(it, p, 1.0, 0.5) < 0.02, "a pulse (sustain 0) ends by itself while the key is held");
            check(run(it, p, 1.0, 0.5) < 0.02, "and stays down until the next press");
            run(it, p, 0.0, 0.1);
            check(run(it, p, 1.0, 0.07) > 0.5, "released and pressed again: it fires again");
        }
        {
            // ADSR: it holds its sustain while the key is down, releases when it is up
            Interaction it = Interaction::made(InteractionSource::Keyboard);
            it.filters = {envelope(0.01, 0.1, 0.5, 0.4)};
            InteractionProcessor p;
            const double held = run(it, p, 1.0, 1.0);
            check(near(held, 0.5, 0.01), "sustain holds while the trigger is held");
            const double releasing = run(it, p, 0.0, 0.1);
            check(releasing > 0.05 && releasing < 0.5, "release falls, not at once");
            check(run(it, p, 0.0, 0.6) == 0.0, "and ends exactly at 0");
        }
        {
            // Velocity: the level the trigger came in at scales it; a retrigger goes on from where it is
            Interaction it = Interaction::made(InteractionSource::Midi);
            it.filters = {envelope(0.02, 0.5, 0.8, 0.5)};
            InteractionProcessor p;
            const double hit = run(it, p, 0.4, 0.05);
            check(hit <= 0.4 + 1e-9 && hit > 0.3, "the trigger's level is the envelope's peak");
            run(it, p, 0.0, 0.05);
            const double before = run(it, p, 0.0, 0.01);
            const double again = run(it, p, 0.4, 1.0 / 60);
            check(again >= before * 0.8, "a retrigger does not jump");
        }
        {
            Interaction it = Interaction::made(InteractionSource::Keyboard);
            it.filters = {envelope(0.01, 0.2, 0.3, 0.4)};
            const Interaction again = Interaction::fromJson(it.toJson());
            check(again.filters == it.filters && again.filters[0].kind == InteractionFilter::Envelope &&
                      interactionFilterKey(InteractionFilter::Envelope) == "envelope" && interactionFilterFromKey("envelope") == InteractionFilter::Envelope,
                  "an envelope is written and read back");
        }
    }

    // Remap: a range to another (either way round), held at its ends; Toggle: on, off at each press
    check(near(through({InteractionStage::remap(0, 16, 100, 300)}, 4), 150), "remap into the target's units");
    check(near(through({InteractionStage::remap(0, 16, 100, 300)}, 20), 300) && near(through({InteractionStage::remap(0, 16, 100, 300)}, -3), 100),
          "held at its ends");
    check(near(through({InteractionStage::remap(0, 1, 1, 0)}, 0.25), 0.75), "Out the other way round inverts");
    check(near(through({}, 42.5), 42.5), "no filter: the value as it comes");
    {
        // What comes out of each filter (the meters): in the order of the chain, an off one passing it on
        Interaction it = Interaction::made(InteractionSource::Osc);
        InteractionStage off = stage(InteractionFilter::Math, 100, 0, MathOp::Add);
        off.enabled = false;
        it.filters = {InteractionStage::remap(0, 16, 0, 1), stage(InteractionFilter::Math, 10, 0, MathOp::Multiply), off};
        InteractionProcessor p;
        p.input(it, 4);
        p.step(it, 1.0 / 60);
        const auto &outs = p.stageOutputs();
        check(outs.size() == 3 && near(outs[0], 0.25) && near(outs[1], 2.5) && near(outs[2], 2.5), "each filter's output");
    }
    {
        Interaction it = Interaction::made(InteractionSource::Keyboard);
        it.filters = {stage(InteractionFilter::Toggle, 0.5)};
        InteractionProcessor p;
        std::vector<double> outs;
        for (double x : {0.0, 1.0, 0.0, 1.0, 0.0}) {
            p.input(it, x);
            outs.push_back(*p.step(it, 1.0 / 60));
        }
        check(outs == std::vector<double>({0, 1, 1, 0, 0}), "toggle: each press the other state");
        const Interaction again = Interaction::fromJson(it.toJson());
        check(again.filters == it.filters, "written and read back");
        Interaction r = it;
        r.filters = {InteractionStage::remap(-2, 5, 10, 20)};
        const Interaction back = Interaction::fromJson(r.toJson());
        check(back.filters.size() == 1 && near(back.filters[0].a, -2) && near(back.filters[0].b, 5) && near(back.filters[0].c, 10) &&
                  near(back.filters[0].d, 20), "a Remap's four ends written and read back");
    }

    // In the engine: each filter's settings are values (fade, preset, OSC), the list one value set at once
    e.newProject();
    e.setFadesManual(true);
    Interaction it = Interaction::made(InteractionSource::Osc);
    it.oscAddress = "/f";
    it.param = "opacity";
    it.filters = {stage(InteractionFilter::Math, 0.5, 0, MathOp::Multiply), stage(InteractionFilter::Clamp, 0, 1)};
    const quint64 id = e.addInteraction(it);
    const QString prefix = QStringLiteral("interaction/%1/").arg(id);
    ParamInfo info;
    check(e.parameterInfo(0, prefix + "filters/1/a", &info) && info.label.contains("Math") && e.parameterInfo(0, prefix + "filters/2/b", &info) &&
              !e.parameterInfo(0, prefix + "filters/1/b", nullptr) && e.parameterInfo(0, prefix + "filters/2/enable", nullptr),
          "each filter's settings as values");
    check(e.setParameter(0, prefix + "filters/1/a", 0.25) && near(e.interactions().back().filters[0].a, 0.25), "a setting written");
    const QString chain = QString::fromUtf8(QJsonDocument(Interaction::filtersToJson({stage(InteractionFilter::Steps, 5)})).toJson(QJsonDocument::Compact));
    check(e.setParameter(0, prefix + "filters", chain) && e.interactions().back().filters.size() == 1 &&
              e.interactions().back().filters[0].kind == InteractionFilter::Steps,
          "the list set at once");
    check(e.parameterInfo(0, prefix + "filters/1/a", &info) && info.label.contains("Steps") && !e.parameterInfo(0, prefix + "filters/2/a", nullptr),
          "its values made again");

    // A snapshot holds the list and each filter's values, and puts them back
    Engine::Snapshot shot;
    shot.name = "Filters";
    shot.fade = 0;
    shot.interactions = e.captureInteractions();
    const QJsonObject held = shot.interactions.at(0).toObject().value("params").toObject();
    check(held.contains("filters") && held.contains("filters/1/a") && held.contains("filters/1/enable"), "a snapshot holds them");
    const int si = e.addSnapshot(shot);
    Interaction changed = e.interactions().back();
    changed.filters = {stage(InteractionFilter::Average), stage(InteractionFilter::Delay)};
    e.setInteraction(changed);
    e.recallSnapshot(si);
    e.advanceFades(0.1);
    check(e.interactions().back().filters.size() == 1 && e.interactions().back().filters[0].kind == InteractionFilter::Steps &&
              near(e.interactions().back().filters[0].a, 5),
          "recalled: the list and its values");

    e.setFadesManual(false);
    if (!failures) std::printf("interaction_filters: OK\n");
    return failures;
}
